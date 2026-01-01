/*
 * subucom SPI user interface
 *
 * Copyright (C) 2015-2017 Pioneer DJ Corp.
 * Copyright (C) 2011-2014 Pioneer Corp.
 * Yasurnori Shibata <yasunori.shibata@pioneerdj.com>
 *
 * Based on spidev.c by Andrea Paterniani
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/spi/spi.h>

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <linux/sched.h>

#include "subucom_spi.h"

	//#define DEBUG_RX_MEMCMP_NONE
	//#define	DEBUG_UPDATE_PRINT
	//#define	LOG_PRINTK

	//"ver0.01 20181119a\n"	//xINT first ENABLE-->DISABLE"
	//"ver0.02 20181120a\n"	//SPI_MODE_3 | SPI_LSB_FIRST ... 1WORD 8bits
	//"ver0.03 20181120b\n"	//spi_transfer ... 1WORD 8bits
	//"ver0.05 20181120d\n"		/* 整理 Drivers(カーネル) → デバイスadapter（ユーザ）→アプリ */
	//"ver0.07 20181121a\n"		/* update時用 write対応 xINTを使うように */
	//"ver0.09a 20181122a\n"	/* JOG LCD SPIによる初期設定 実装開始版 */
	//"ver0.10a 20181126 "		/* JOG LCD SPIによる初期設定 実装 */
	//"ver0.11 20181127 "		/* PANEL f/w update 対応初期版 */
	//"ver0.12 20181203 "		/* 余分なprintkコメント DEBUG_UPDATE_PRINT を未定義にする */
#define VERSION_STRING "ver0.13 20181204 "		/* SPI clock .dtsで 1MHzに統一 */	/* modeprobe時に GP_REQ=HIGHであれば CLOCKを下げること可能 */
#define JIFFIES_TEST			0

//#define ENB_SPICLOCK_SEL							/* updateの時 CLOCK 分ける場合 定義 */
//#define SPICLOCK_UPDATE					(1000000)	/* 1MHz /400KHz /100KHz*/

#define SUBUCOM_SPI_DATA_SIZE_NORMAL	(64)	/* normal mode... bytes tx:64   rx:64 */
#define SUBUCOM_SPI_DATA_SIZE_UPDATE	(1036)	/* update mode... bytes tx:1036 (rx:16)*/
#define	SUBUCOM_SPI_DATA_RXLEN_UPDATE	(1036)	/* update resp... bytes tx:1036 (rx:16)*/

#define SUBUCOM_SPI_TIMER_INTERVAL	1			/* Kernel Features-->Timer frequency 1000HZ  1:2ms 2:3ms 3:4ms */
#define SUBUCOM_SPI_BITS_PER_WORD	32			/* bits */
#define JOGLCD_SPI_BITS_PER_WORD	8			/* bits */

#define SUBUCOM_SPI_TRANSFER_SIZE_NORMAL	SUBUCOM_SPI_DATA_SIZE_NORMAL		/* bytes */
#define SUBUCOM_SPI_TRANSFER_SIZE_UPDATE	SUBUCOM_SPI_DATA_SIZE_UPDATE		/* bytes */

#define SUBUCOM_SPI_TX_MULTIPLEX_NUM	1
#define SUBUCOM_SPI_TX_PARTITION_NUM	1

#define	DRV_NAME			"subucom_spi"

#define N_SPI_MINORS			3

/* kthread priority */
#define SPI_THREAD_PRIORITY		98	/* SCHED_FIFO,SCHED_RR: 0(low) to 99(high) */


struct subucom_tx_part {
	uint16_t	part_num;	/* number of partitions */
	uint16_t	part_id;	/* current partitions */

	int32_t		updated;
	uint32_t	bytes;
	uint32_t	transfer_bytes;
	int32_t		transfer_len;		/* size_t */

	uint8_t		*buf;
	uint8_t		*bufnew;

	struct subucom_ioc_transfer ioc;
};

#define GPIO_NUM_G2M_JLCD_SEL   496
#define GPIO_NUM_G2M_PNL_xINT   393
#define GPIO_NUM_G2M_PNL_WRITE  395
#define GPIO_NUM_G2M_PNL_REQ    397
#define	SUBCPU_BUSYWAIT_USEC	(50)		//udelay
#define	CNT_SUBCPU_BUSYWAIT		(15)		//750usec CNT(15) X USEC(50)
#define	UPDATE_SPIWAIT_USEC		(1800)		//1800usec (1.8ms)

#define	JLCD_SEL_JOG			(1)
#define	JLCD_SEL_PANEL			(0)

#define	PNL_xINT_DISABLE		(1)
#define	PNL_xINT_ENABLE			(0)
#define	PNL_xINT_DISABLE		(1)

static DECLARE_BITMAP(  minors, N_SPI_MINORS);

static uint8_t			_debug = 0;
static int32_t			majorNumber = 0;
static struct class		*subucom_spi_class = NULL;

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static uint32_t DEFtmstat	= SUBUCOM_TIMER_OFF;		//2018.11.13 アプリがタイマ開始するはず
module_param( DEFtmstat, uint, S_IRUGO);
MODULE_PARM_DESC( DEFtmstat, "timer status default setting");

static uint32_t		SZbuf_norm = SUBUCOM_SPI_DATA_SIZE_NORMAL;
module_param( SZbuf_norm, uint, S_IRUGO);
MODULE_PARM_DESC( SZbuf_norm, "normal data size in SPI message");

static uint32_t		SZbufsize = SUBUCOM_SPI_DATA_SIZE_UPDATE;
module_param( SZbufsize, uint, S_IRUGO);
MODULE_PARM_DESC( SZbufsize, "maximum data size in SPI message");

static uint32_t		txmulti = SUBUCOM_SPI_TX_MULTIPLEX_NUM;
module_param( txmulti, uint, S_IRUGO);
MODULE_PARM_DESC( txmulti, "maximum multiplex number in SPI Tx data");

static uint32_t		txpart = SUBUCOM_SPI_TX_PARTITION_NUM;
module_param( txpart, uint, S_IRUGO);
MODULE_PARM_DESC( txpart, "maximum partition number in SPI Tx data");

static void subucom_spi_timer_func( unsigned long data );
static void subucom_txrx_done( void );

static const uint16_t joglcd_reg_wdata[][2] = {
  /*Reg#                   Value */
 { 0x01, /*Driver output*/ 0x2100 },/* dummy write for new kernel */
 { 0xF100, /*Wait100ms  */ 0xF100 },/* wait for new kernel */
 { 0x01, /*Driver output*/ 0x2100 },
 { 0x02, /*LCD driver AC*/ 0x0200 },
 { 0x03, /*Power cont   */ 0x7184 },
 { 0xF100, /*Wait100ms  */ 0xF100 },
 { 0x04, /*color        */ 0x0447 },
 { 0x05, /*?            */ 0xB484 },
 { 0x0A, /*cont bright  */ 0x4008 },
 { 0xF040, /*Wait40ms   */ 0xF040 },
 { 0x0B, /*?            */ 0xD400 },
 { 0x0D, /*Power cont   */ 0x0235 },
 { 0xF100, /*Wait100ms  */ 0xF100 },
 { 0x0E, /*Power cont   */ 0x3000 },
 { 0xF100, /*Wait100ms  */ 0xF100 },
 { 0x0F, /*Gate scan pos*/ 0x0000 },
 { 0x16, /*H porch      */ 0x9f80 },
 { 0x17, /*V porch      */ 0x2212 },
 { 0xF100, /*Wait100ms  */ 0xF100 },
 { 0x1E, /*Power cont   */ 0x005F },
 { 0xF100, /*Wait100ms  */ 0xF100 },
 { 0x30, /*Gamma        */ 0x0000 },
 { 0x31, /*Gamma        */ 0x0407 },
 { 0x32, /*Gamma        */ 0x0000 },
 { 0x33, /*Gamma        */ 0x0000 },
 { 0x34, /*Gamma        */ 0x0505 },
 { 0x35, /*Gamma        */ 0x0003 },
 { 0x36, /*Gamma        */ 0x0707 },
 { 0x37, /*Gamma        */ 0x0000 },
 { 0x3A, /*Gamma        */ 0x0904 },
 { 0x3B, /*Gamma        */ 0x0904 },
 { 0xFFFF, /*data end   */ 0xFFFF },
};
static int32_t	spithread_pid = 0;

//******************************************************************************
//* name: subucom_spi_complete( )
//******************************************************************************
static void subucom_spi_complete( void *arg )
{
	complete( arg);
}
//******************************************************************************
//* from subucom_spi_sync_read()/subucom_spi_sync_write()/subucom_spi_sync_rw
//*    +---- this...
//*
//* name : subucom_spi_sync()		//ssize_t
//*           SYNC SPI write/read
//******************************************************************************
static int32_t
subucom_spi_sync( struct subucom_spi_dev *priv, struct spi_message *message )
{
	DECLARE_COMPLETION_ONSTACK( done);
	int32_t	status;

	message->complete = subucom_spi_complete;
	message->context = &done;

	spin_lock_irq( &priv->spi_lock);
	if (priv->spi == NULL) {
		status = -ESHUTDOWN;
	}
	else {
#ifdef LOG_PRINTK
printk( "\n spi_async [subucom_spi_sync] @subucom_spi.c\n");
#endif
		status = spi_async( priv->spi, message);
	}
	spin_unlock_irq(&priv->spi_lock);

	if (status == 0) {
		wait_for_completion( &done);
#ifdef LOG_PRINTK
printk( " end wait_for_completion [subucom_spi_sync] @subucom_spi.c\n");
#endif
		status = message->status;
	}
	message->context = NULL;
	return status;
}
//******************************************************************************
//* from application
//*    +---- read()
//*             +---- this... @TIMER OFF
//*
//* name : subucom_spi_sync_read()
//******************************************************************************
static inline ssize_t
subucom_spi_sync_read(struct subucom_spi_dev *priv, uint8_t *tx_buf, uint8_t *rx_buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf	= tx_buf,
		.rx_buf	= rx_buf,
		.len	= len,
	};
	struct spi_message m;

	if (priv == NULL) {
		return -EIO;
	}

	spi_message_init( &m);
	spi_message_add_tail( &t, &m);

	if (subucom_spi_sync( priv, &m) == 0) {
		return m.actual_length;
	}
	return 0;
}
//******************************************************************************
//* from application
//*    +-- write()
//*           +-- subucom_spi_write( )
//*                  +-- this...
//*
//* name : subucom_spi_sync_write()
//******************************************************************************
static inline ssize_t
subucom_spi_sync_write(struct subucom_spi_dev *priv, u8 *tx_buf, u8 *rx_buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf	= tx_buf,
		.rx_buf	= rx_buf,
		.len	= len,
	};
	struct spi_message m;
	int32_t	status;

	if (priv == NULL) {
		return -EIO;
	}

	spi_message_init( &m);
	spi_message_add_tail( &t, &m);

	status = subucom_spi_sync( priv, &m);

#ifdef LOG_PRINTK
printk( "[subucom_spi_sync_write] bits_per:%d status=%d [%02x,%02x...%02x,%02x] mode=%d len=%d\n",
		priv->spi->bits_per_word,
		status,
		tx_buf[0], tx_buf[1], tx_buf[len - 2], tx_buf[ len - 1],
		priv->pnl_req, (int)len);
#endif

	return status;
}
//******************************************************************************
//* from subucom_spi_thread()
//*	        for loop ...wait_event_interruptible()
//*            +-- subucom_spi_transfer
//*                   +-- this...
//*
//* name : subucom_spi_sync_rw()
//******************************************************************************
static inline int32_t
subucom_spi_sync_rw(struct subucom_spi_dev *priv, u8 *tx_buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf	= tx_buf,
		.rx_buf	= priv->rx_buf,
		.len	= len,
	};
	struct spi_message m;
	int32_t	status;

	if (priv == NULL) {
		return -EIO;
	}

	spi_message_init( &m);
	spi_message_add_tail( &t, &m);

	status = subucom_spi_sync( priv, &m);

if (priv->timer_interval>=500) {
	printk( "[subucom_spi_sync_rw] bits_per:%d status=%d [%02x,%02x...%02x,%02x] mode=%d len=%d\n",
		priv->spi->bits_per_word,
		status,
		tx_buf[0], tx_buf[1], tx_buf[len - 2], tx_buf[ len - 1],
		priv->pnl_req, (int)len);
}

	return status;
}

/*-------------------------------------------------------------------------*/

static inline void
subucom_spi_clear_buffers(struct subucom_spi_dev *priv)
{
	mutex_lock( &priv->transfer_lock);
	memset( priv->tx_buf, 0, SZbufsize * txmulti * txpart);
	memset( priv->rx_buf, 0, SZbufsize);
	mutex_unlock( &priv->transfer_lock);

	mutex_lock( &priv->tx_buf_lock);
	memset( priv->tx_bufnew, 0, SZbufsize * txmulti * txpart);
	mutex_unlock( &priv->tx_buf_lock);

	mutex_lock( &priv->rx_buf_lock);
	memset( priv->rx_bufold, 0, SZbufsize);
	mutex_unlock( &priv->rx_buf_lock);
//printk( "[subucom_spi_clear_buffers] @subucom_spi.c\n");
}

static inline void
subucom_spi_init_ids(struct subucom_spi_dev *priv)
{
	struct subucom_tx_part	*tx;
	uint32_t	i;

	priv->tx_multi_id = 0;
	for (i = 0, tx = priv->tx; i < txmulti; ++i, ++tx) {
		tx->part_id = 0;
		tx->updated = 0;
	}
}
//******************************************************************************
//* from APP
//*    +-- ioctl( SUBUCOM_IOC_WR_BITS_PER_WORD,,,)
//*           +-- subucom_spi_ioctl()               OR   subucom_spi_probe()
//*                  |
//*                  +-- this...
//*
//* name : subucom_spi_setup_bytes_per_word()
//******************************************************************************
static inline void 
subucom_spi_setup_bytes_per_word( struct subucom_spi_dev *priv )
{
	priv->bytes_per_word = priv->spi->bits_per_word / 8;
	if (priv->spi->bits_per_word % 8) {
		++priv->bytes_per_word;
	}
}
//******************************************************************************
//* from subucom_spi_setup_tx()
//*    +-- subucom_spi_setup_length()
//*           +-- this... subucom_spi_calc_length
//*
//* subucom_spi_write() or subucom_spi_read()
//*    +-- this... subucom_spi_calc_length
//*
//* name : subucom_spi_calc_length()
//******************************************************************************
static inline size_t
subucom_spi_calc_length( struct subucom_spi_dev *priv, size_t bytes )
{
#if 1
	size_t	len	= priv->bytes_per_word ? ((bytes + priv->bytes_per_word - 1) / priv->bytes_per_word) : 0;
	len	*= priv->bytes_per_word;

//	pr_info("subucom_spi_calc_length() bytes: %d -> len: %d\n", bytes, len); /* debug print */
	return	len;
#else
	size_t len = 0;

	if (priv->bytes_per_word) {
		len = bytes / priv->bytes_per_word;
		if (bytes % priv->bytes_per_word)
			++len;
	}

//	pr_info("subucom_spi_calc_length() return %d\n", len);	/* debug print */
	return len;
#endif
}
//******************************************************************************
//* from subucom_spi_setup_tx  OR subucom_spi_probe()  OR ioctl( SUBUCOM_IOC_WR_RX_BYTES )
//*    +-- this... subucom_spi_setup_length()
//*
//* name : subucom_spi_setup_length()
//******************************************************************************
static inline void
subucom_spi_setup_length( struct subucom_spi_dev *priv, u16 multi_id )
{
	struct subucom_tx_part *tx = priv->tx + multi_id;
	size_t len;

	//
	//送信,受信 同クロックで並列して実施の場合
	//長いレングス側で送受信transfer_lenを決める    EP122は 64 tx:64 rx 1036 tx:16 rx なのでこのままで問題なさそう...
	//
	tx->transfer_bytes = (tx->bytes > priv->rx_bytes) ? tx->bytes : priv->rx_bytes;
	len	= subucom_spi_calc_length( priv, tx->transfer_bytes);

//printk( "[subucom_spi_setup_length] tx->bytes:%d tx->transfer_bytes:%d rx_bytes:%d transfer_len:%d @subucom_spi.c\n",
//	tx->bytes, tx->transfer_bytes, priv->rx_bytes, (int)len);

	mutex_lock( &priv->transfer_lock);
	tx->transfer_len = len;
	mutex_unlock( &priv->transfer_lock);
}
//******************************************************************************
//* from subucom_spi_thread()
//*	        for(;;) loop wait_event_interruptible()  <--- Cyclic TMOUT
//*            +-- subucom_spi_transfer
//*                   +-- this...
//*
//* name : subucom_spi_setup_tx
//******************************************************************************
static inline struct subucom_tx_part *
subucom_spi_setup_tx( struct subucom_spi_dev *priv )
{
	struct subucom_tx_part *tx = priv->tx + priv->tx_multi_id;

	/* update parameters */
	if (tx->updated & 0x1) {
		tx->part_num = tx->ioc.part_num;
		tx->bytes	 = tx->ioc.bytes;
		subucom_spi_setup_length( priv, priv->tx_multi_id);

		tx->part_id	= tx->part_num;

#if 0	/* TODO: clear other buffers */
		if (tx->part_num > 1) {
			mutex_lock(&priv->tx_buf_lock);
			memset(tx->buf + SZbufsize, 0, SZbufsize * (tx->part_num - 1));
			mutex_unlock(&priv->tx_buf_lock);
		}
#endif
	}

	/* copy from bufnew to buf */
	if (tx->updated) {
		int offset, shift = 0;

		mutex_lock( &priv->tx_buf_lock);

		do {
			if (tx->updated & 0x1) {
				offset = shift * SZbufsize;
				memcpy( tx->buf + offset, tx->bufnew + offset, SZbufsize);
#ifdef	DEBUG_UPDATE_PRINT
if (tx->bytes > 2) {
	printk("[subucom_spi_setup_tx] bufnew[%02x,%02x...%02x,%02x]\n",
	tx->bufnew[0],tx->bufnew[1], tx->bufnew[tx->bytes - 2],tx->bufnew[tx->bytes - 1]);	/* debug print */

	printk("[subucom_spi_setup_tx] buf[%02x,%02x...%02x,%02x]\n",
	tx->buf[0],tx->buf[1], tx->buf[tx->bytes - 2],tx->buf[tx->bytes - 1]);	/* debug print */
}
#endif
			}
			tx->updated >>= 1;
			++shift;
		} while (tx->updated);

		mutex_unlock( &priv->tx_buf_lock);
	}

	/* update part_id */
	if (++tx->part_id >= tx->part_num)
		tx->part_id = 0;

	/* update tx_multi_id */
	if (++priv->tx_multi_id >= priv->tx_multi_num)
		priv->tx_multi_id = 0;

	return tx;
}

static inline void
subucom_spi_check_rxbuf(struct subucom_spi_dev *priv)
{
#if 0 /* TODO: temporally */
	/* eliminate zero data */
	if (priv->rx_buf[2] == 0 && priv->rx_buf[3] == 0)
		return;
#endif

	/* ここは MODE=0 の場合のチェックを入れたほうが良い？ */

#ifndef DEBUG_RX_MEMCMP_NONE
	/* compare to old data */
	if (memcmp( priv->rx_bufold, priv->rx_buf, priv->rx_bytes)) {
#endif

		mutex_lock( &priv->rx_buf_lock);
		memcpy( priv->rx_bufold, priv->rx_buf, priv->rx_bytes);
		priv->rx_detected = 1;
		mutex_unlock( &priv->rx_buf_lock);
		//pr_info("subucom_spi_check_rxbuf() :0x%08x\n", *(u32 *)priv->rx_buf);	/* debug print */

#ifndef DEBUG_RX_MEMCMP_NONE
	}
#endif
}

//******************************************************************************
//* from subucom_spi_transfer()
//*         +-- this...
//*
//* subucom_check_ready
//******************************************************************************
static int8_t subucom_check_ready( struct subucom_spi_dev *priv )
{
	int8_t	subucom_status = 0;
	int32_t	cnt_retry = 0;
	int32_t	retry_limit	= priv->max_cntbusy;
//printk( "-->enable PNL-xINT\n");

	priv->pnl_req	= (uint8_t)gpio_get_value( GPIO_NUM_G2M_PNL_REQ);	//0:normal 1:update
	if (priv->pnl_req) {	//update mode?
		priv->rx_bytes = SUBUCOM_SPI_DATA_RXLEN_UPDATE;
	}
	gpio_set_value( GPIO_NUM_G2M_PNL_xINT, PNL_xINT_ENABLE);

	//check READY SUBCPU ... GPIO Rpin GP_WRITE
	cnt_retry++;
	subucom_status = (int8_t)gpio_get_value( GPIO_NUM_G2M_PNL_WRITE);

	while ((!subucom_status) && (cnt_retry <= retry_limit)){
			//usleep_range( SUBCPU_BUSYWAIT_USEC, (SUBCPU_BUSYWAIT_USEC+10));
		udelay( SUBCPU_BUSYWAIT_USEC);
		cnt_retry++;
		subucom_status = (int8_t)gpio_get_value( GPIO_NUM_G2M_PNL_WRITE);
	}

//printk( "[subucom_check_ready] cnt:%d return:%d\n", cnt_retry, subucom_status);
	return( subucom_status);
}
//******************************************************************************
//* from subucom_spi_transfer()
//*         +-- this...
//*
//* subucom_txrx_done
//******************************************************************************
static void subucom_txrx_done( void )
{
//printk( "-->disable PNL-xINT\n");
	gpio_set_value( GPIO_NUM_G2M_PNL_xINT, PNL_xINT_DISABLE);
}
//******************************************************************************
//* from Cyclic TMOUT
//*         +-- subucom_spi_thread
//*                +-- this... subucom_spi_transfer()
//******************************************************************************
static int32_t subucom_spi_transfer(struct subucom_spi_dev *priv)
{
	struct subucom_tx_part	*tx;
	int32_t		status;
	uint8_t		*tx_buf;
	int8_t		ready;

	if (!priv) {
		return -EIO;
	}

	ready = subucom_check_ready( priv);
	if (ready) {
		/*subucom_txrx_done( );*/			/* xINT disable */
//printk( "[subucom_spi_transfer] SUBCPU ready\n");

		/* setup tx data */
		tx = subucom_spi_setup_tx( priv);
		if (tx->transfer_bytes <= SZbufsize) {

			tx_buf = tx->buf + (SZbufsize * tx->part_id);

			/* transfer data */
			mutex_lock( &priv->transfer_lock);
									//transfer_len ... calc&set from subucom_spi_setup_tx() --> subucom_spi_setup_length()
			status = subucom_spi_sync_rw( priv, tx_buf, tx->transfer_len);
			if (status) {
				dev_err( &priv->spi->dev, "SPI r/w failed\n");
				status = -EIO;
			}
			mutex_unlock(&priv->transfer_lock);

			/* check rx data */
			if (status == 0) {
				subucom_spi_check_rxbuf( priv);
			}
		}
		else {
			return -EINVAL;
		}
	}
	else {
		status = -EBUSY;
	}
	subucom_txrx_done( );

	return status;
}

/*-------------------------------------------------------------------------*/

#if JIFFIES_TEST
static void jiffies_test(unsigned long l)
{
	pr_info("msecs_to_jiffies(%ld) -> %ld\n", l, msecs_to_jiffies(l));
}
#endif




//******************************************************************************
//* name: subucom_spi_timer_start( )
//******************************************************************************
static inline void
subucom_spi_timer_start( struct subucom_spi_dev *priv, uint32_t timeout )
{
	struct timer_list *timer = &priv->spi_timer;
	timer->data	= (unsigned long)priv;
	timer->expires	= jiffies + msecs_to_jiffies( timeout);
	timer->function	= subucom_spi_timer_func;
	add_timer( timer);

	if (!priv->timer_status) {
		subucom_spi_clear_buffers( priv);
		subucom_spi_init_ids( priv);
	}
	priv->timer_status = SUBUCOM_TIMER_ON;
}
//******************************************************************************
//* name: subucom_spi_timer_stop( )
//******************************************************************************
static inline void
subucom_spi_timer_stop(struct subucom_spi_dev *priv)
{
	del_timer_sync( &priv->spi_timer);
	priv->timer_status = SUBUCOM_TIMER_OFF;
}
//******************************************************************************
//* from TIMER  ...expires = jiffies+msecs_to_jiffies( timeout)
//*
//* name: subucom_spi_timer_func( )
//******************************************************************************
static void subucom_spi_timer_func( unsigned long data )
{
	struct subucom_spi_dev	*priv = (struct subucom_spi_dev *)data;

//	pr_info("subucom_spi_timer_func() called...\n");	/* debug print */
#if 0	/* delay to restart timer */
	subucom_spi_timer_start(priv, priv->timer_interval);
#endif
	wake_up_interruptible( &priv->spi_wait);
	priv->event_flg = 1;
}

//******************************************************************************
//* from subucom_spi_probe( )
//*         +---- subucom_spi_thread_setup( )
//*                  +---- kthread_create( ),kthread_bind( ),wake_up_process( )
//*                           +---- this...
//*
//* name: subucom_spi_thread( )
//******************************************************************************
static bool  pm_ready = false;
static int32_t subucom_spi_thread(void *data)
{
	struct subucom_spi_dev *priv = (struct subucom_spi_dev *)data;
	int32_t	status = 0;
//	cpu_set_t	cpu_set;

	/* set kthread priority */
	struct sched_param param = { .sched_priority = SPI_THREAD_PRIORITY };

	//struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1 };
	status = sched_setscheduler( current, SCHED_FIFO, &param);
pr_info("[subucom_spi_thread]sched_setscheduler:%d pid:%d pri:%d\n", status, spithread_pid, param.sched_priority);	/* debug print */

//	CPU_ZERO( &cpu_set);
//	CPU_SET( 5, &cpu_set);
//	status = sched_setaffinity( spithread_pid, sizeof(cpu_set_t), &cpu_set);
//printk("[subucom_spi_thread] sched_setaffinity:%d\n", status);

	priv->event_flg = 0;

	for (;;) {

		if(!pm_ready){
			msleep(200);
			continue;
		}

		status = wait_event_interruptible_timeout( priv->spi_wait,
						  priv->event_flg != 0 || kthread_should_stop(),
						  msecs_to_jiffies(1000) );
		if (kthread_should_stop())
			break;

		if (status == 0) {
			if (priv->timer_status == SUBUCOM_TIMER_OFF) {
				continue;
			}
			subucom_spi_timer_stop(priv);
printk("[INFO]:%s wait_event_interruptible_timeout() Detected TIMEOUT!\n",__func__);
			msleep(10);
		}
		else if(status < 0) {
printk("[INFO]:%s wait_event_interruptible_timeout():%d\n",__func__, status);
			continue;
		}

		priv->event_flg = 0;

		/* delay to restart timer */
//pr_info("%s is calling subucom_spi_timer_start()...\n", __func__); /* debug print */
		subucom_spi_timer_start( priv, priv->timer_interval);

		status = subucom_spi_transfer( priv);
	}
	return 0;
}


//******************************************************************************
//* name : subucom_spi_thread_setup
//******************************************************************************
static int32_t subucom_spi_thread_setup(struct subucom_spi_dev *priv)
{
	struct task_struct *thread;

//#if 1
printk( "[subucom_spi_thread_setup] @subucom_spi.c\n");

	thread = kthread_create( subucom_spi_thread, priv, "subucom_spi%d.%d",
				priv->spi->master->bus_num, priv->spi->chip_select);
	if (IS_ERR(thread)) {
		dev_dbg(&priv->spi->dev, "creating SPI thread failed\n");
		return PTR_ERR(thread);
	}

//pr_info("%s() cpu_bit_bitmap[0][0]: %lu\n", __func__, cpu_bit_bitmap[0][0]); /* debug print */
//pr_info("%s() cpu_bit_bitmap[1][0]: %lu\n", __func__, cpu_bit_bitmap[1][0]); /* debug print */
//pr_info("%s() cpu_all_bits[0]: %lu\n", __func__, cpu_all_bits[0]); /* debug print */
	kthread_bind( thread, 5);
	wake_up_process( thread);

	spithread_pid = thread->pid;
printk( "[!!subucom_spi_thread!!] pid:%d\n", spithread_pid);

//#else
//	thread = kthread_run(subucom_spi_thread, priv, "subucom_spi%d.%d",
//			     priv->spi->master->bus_num, priv->spi->chip_select);
//	if (IS_ERR(thread)) {
//		dev_dbg(&priv->spi->dev, "creating SPI thread failed\n");
//		return PTR_ERR(thread);
//	}
//#endif

	priv->spi_thread = thread;
	return 0;
}

//******************************************************************************
//* name : subucom_spi_thread_kill
//******************************************************************************
static void subucom_spi_thread_kill(struct subucom_spi_dev *priv)
{
	kthread_stop(priv->spi_thread);
	priv->spi_thread = NULL;

	if (priv->dbg_thread != NULL) {
		kthread_stop(priv->dbg_thread);
		priv->dbg_thread = NULL;
	}
}

/*-------------------------------------------------------------------------*/

//******************************************************************************
//* from APP
//*    +-- read()
//*           +-- this...
//*
//* name : subucom_spi_read
//******************************************************************************
static ssize_t
subucom_spi_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct subucom_spi_dev	*priv;
	ssize_t			status = 0;
	unsigned long	missing;
	int8_t			ready;

	priv = filp->private_data;

	if (priv->timer_status == SUBUCOM_TIMER_ON) {	/* TIMER ON */
		if (count > SZbufsize) {
			return -EMSGSIZE;
		}
		mutex_lock(&priv->rx_buf_lock);
		missing = copy_to_user(buf, priv->rx_bufold, count);
		if (missing == count)
			status = -EFAULT;
		else
			status = count - missing;
		priv->rx_detected = 0;
//printk("[subucom_spi_read]: rx_detected=0\n");
		mutex_unlock(&priv->rx_buf_lock);
	}
	else {						/* TIMER OFF */
		size_t len;
		u8 *tmp;

		mutex_lock(&priv->buffer_lock);
		priv->buffer = kzalloc(2 * count * sizeof(u8), GFP_KERNEL);
		if (!priv->buffer) {
			mutex_unlock(&priv->buffer_lock);
			return -ENOMEM;
		}
		tmp = priv->buffer + (count * sizeof(u8));
		memset( tmp, 0, count);													//DUMMY write command
		len = subucom_spi_calc_length( priv, count);

		ready = subucom_check_ready( priv);
		if (ready) {
//printk( "[subucom_spi_read] SUBCPU ready\n");

			status = subucom_spi_sync_read( priv, tmp, priv->buffer, len);
#ifdef LOG_PRINTK
printk( "subucom_spi_sync_read() status:%d len:%d\n", (int)status, (int)len);	/* debug print */
//pr_info("subucom_spi_write() status: %d\n", status);	/* debug print */
#endif
		}
		else {
			status = -EBUSY;
		}
		subucom_txrx_done( );

//pr_info("subucom_spi_read() status: %d\n", status);	/* debug print */
		if (status > 0) {
			missing = copy_to_user(buf, priv->buffer, count);
			if (missing == count) {
				status = -EFAULT;
				printk( "ERROR copy_to_user:%u subucom_spi_sync_read()\n", (uint32_t)missing);
			}
			else {
				status = count - missing;
			}
		}

		kfree(priv->buffer);
		mutex_unlock(&priv->buffer_lock);
	}
	return status;
}
//******************************************************************************
//* from APP
//*    +-- write()
//*           +-- this...
//*
//* name : subucom_spi_write()
//******************************************************************************
static ssize_t
subucom_spi_write(struct file *filp, const char __user *buf,
		  size_t count, loff_t *f_pos)
{
	struct subucom_spi_dev	*priv;
	ssize_t			status = 0;
	unsigned long	missing;
	int8_t			ready;

	priv = filp->private_data;
	if (!priv) {
		return -EIO;
	}

	if (priv->timer_status == SUBUCOM_TIMER_ON) {	/* TIMER ON */
		/* not supported. */
		return -EFAULT;
	}
	else {						/* TIMER OFF */
		size_t	len;
		u8		*tmp;

//pr_info("subucom_spi_write() count: %d\n", count);	/* debug print */
		mutex_lock( &priv->buffer_lock);
		priv->buffer = kzalloc( 2 * count * sizeof(u8), GFP_KERNEL);
		if (!priv->buffer) {
			mutex_unlock( &priv->buffer_lock);
			return -ENOMEM;
		}
		tmp = priv->buffer + (count * sizeof(u8));

		missing = copy_from_user( priv->buffer, buf, count);
		if (missing == 0) {
			len = subucom_spi_calc_length( priv, count);
//pr_info("subucom_spi_write() len: %d\n", len);	/* debug print */

			ready = subucom_check_ready( priv);
			if (ready) {
//printk( "[subucom_spi_transfer] SUBCPU ready\n");

				status = subucom_spi_sync_write( priv, priv->buffer, tmp, len);
#ifdef LOG_PRINTK
printk( "subucom_spi_write() status:%d len:%d\n", (int)status, (int)len);	/* debug print */
//pr_info("subucom_spi_write() status: %d\n", status);	/* debug print */
#endif
			}
			else {
				status = -EBUSY;
			}
			subucom_txrx_done( );
		}
		else {
			status = -EFAULT;
		}
		kfree( priv->buffer);
		mutex_unlock( &priv->buffer_lock);
	}
	return status;
}
//******************************************************************************
//* from APP
//*    poll( )
//*       +-- this...
//*
//* name : subucom_spi_poll
//******************************************************************************
static unsigned int
subucom_spi_poll( struct file *filp, poll_table *wait )
{
	struct subucom_spi_dev	*priv;
	unsigned int mask = 0;

//printk( "[subucom_spi_poll] in \n");
	priv = filp->private_data;

	if (priv->timer_status == SUBUCOM_TIMER_ON) {	/* TIMER ON */
//printk( "[subucom_spi_poll] SUBUCOM_TIMER_ON\n");
		poll_wait( filp, &priv->spi_wait, wait);
//printk("[subucom_spi_poll]: rx_detected=%d\n", priv->rx_detected);
		if (priv->rx_detected)
			mask |= (POLLIN | POLLRDNORM);
	}
//	else						/* TIMER OFF */
//		mask |= (POLLIN | POLLRDNORM);

//printk( "[subucom_spi_poll] mask:%x\n", mask);
	return mask;
}
//******************************************************************************
//* from APP
//*    +-- ioctl( SUBUCOM_IOC_MESSAGE( ) )
//*           +-- subucom_spi_ioctl()
//*                  +-- this...
//*
//* name : subucom_setup_txbufnew
//******************************************************************************
static int32_t subucom_setup_txbufnew( struct subucom_spi_dev *priv )
{
	uintptr_t	src;
	uint8_t		*dest;
	int32_t		status = -EFAULT;

	struct subucom_ioc_transfer	*ioc= &priv->ioc;
	struct subucom_tx_part		*tx	= priv->tx + ioc->multi_id;

	if ((ioc->multi_id >= priv->tx_multi_num) ||
	    (ioc->part_num > txpart) ||
	    (ioc->part_id >= ioc->part_num) ||
	    (ioc->bytes > SZbufsize)) {
		return -EINVAL;
	}

	dest	= tx->bufnew + (SZbufsize * ioc->part_id);
	src		= (uintptr_t)ioc->tx_buf;
#ifdef	DEBUG_UPDATE_PRINT
printk("[subucom_setup_txbufnew] ioc->bytes=%d\n", ioc->bytes);	/* debug print */
#endif
	mutex_lock( &priv->tx_buf_lock);
	if (!copy_from_user( dest, (const uint8_t __user *)src, ioc->bytes)) {
		memcpy( &tx->ioc, ioc, sizeof( struct subucom_ioc_transfer));
		tx->updated |= 0x1 << ioc->part_id;
		status = 0;

#ifdef	DEBUG_UPDATE_PRINT
if (ioc->bytes > 2) {
	printk("[subucom_setup_txbufnew] [%02x,%02x...%02x,%02x]\n",
	dest[0],dest[1], dest[ioc->bytes - 2],dest[ioc->bytes - 1]);	/* debug print */
}
pr_info("subucom_setup_txbufnew() part_id[%d] multi_id[%d] tx_updated:0x%08x\n", ioc->part_id, ioc->multi_id, tx->updated);	/* debug print */
#endif
	}
	mutex_unlock(&priv->tx_buf_lock);

	return status;
}
//******************************************************************************
//* from APP
//*    +-- ioctl( )
//*           +-- this...
//*
//* name : subucom_spi_ioctl
//******************************************************************************
static long
subucom_spi_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct subucom_spi_dev	*priv;
	struct spi_device		*spi;
	int32_t		err = 0;
	int32_t		retval = 0;
	u32			tmp;
	uint32_t	n_ioc;		//unsigned
	uint16_t	usec;
	int32_t		gp_req;
	int32_t		gp_write;

	if (_IOC_TYPE(cmd) != SUBUCOM_IOC_MAGIC) {
		return -ENOTTY;
	}

	/* Check access direction once here; don't repeat below.
	 * IOC_DIR is from the user perspective, while access_ok is
	 * from the kernel perspective; so they look reversed.		 */
	if (_IOC_DIR(cmd) & _IOC_READ) {
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	}
	if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE) {
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}
	if (err) {
		return -EFAULT;
	}

	/* guard against device removal before, or while,
	 * we issue this ioctl.			 */
	priv = filp->private_data;
	if (err) {
		return -EFAULT;
	}

	spin_lock_irq( &priv->spi_lock);
	spi = spi_dev_get( priv->spi);
	spin_unlock_irq( &priv->spi_lock);
	if (spi == NULL) {
		return -ESHUTDOWN;
	}

	switch (cmd) {
	/* read requests */
	case SUBUCOM_IOC_RD_TIMER_STATUS:
printk( "[subucom_spi_ioctl](R) TIMER STATUS=%d @subucom_spi.c\n", priv->timer_status);
		retval = __put_user( priv->timer_status, (__u8 __user *)arg);
		break;
	case SUBUCOM_IOC_RD_TIMER_INTERVAL:
printk( "[subucom_spi_ioctl](R) TIMER INTERVAL=%d @subucom_spi.c\n", priv->timer_interval);
		retval = __put_user( priv->timer_interval, (__u32 __user *)arg);
		break;
	case SUBUCOM_IOC_RD_BITS_PER_WORD:
		retval = __put_user( spi->bits_per_word, (__u16 __user *)arg);
		break;
	case SUBUCOM_IOC_RD_RX_BYTES:
		retval = __put_user( priv->rx_bytes, (__u32 __user *)arg);
		break;
	case SUBUCOM_IOC_RD_BUSYWAIT_UTIME:
		usec	= (uint16_t)(priv->max_cntbusy * SUBCPU_BUSYWAIT_USEC);
		retval = __put_user( usec, (__u16 __user *)arg);
printk( "[subucom_spi_ioctl](R) PNL BUSY WAIT=%d[usec] cnt=%d @subucom_spi.c\n", usec, priv->max_cntbusy);
		break;
#if 0
	case SUBUCOM_IOC_RD_TX_MULTIPLEX:
		retval = __put_user(priv->tx_multi_num, (__u8 __user *)arg);
		break;
#endif
	case SUBUCOM_IOC_WR_DEBUG:
		retval = __get_user(tmp, (__u8 __user *)arg);
		if (retval == 0) {
			_debug = tmp;
			printk( "[subucom_spi_ioctl] BUSY/READY debug:%d @subucom_spi.c\n", _debug);
		}
		break;
	/* write requests */
	case SUBUCOM_IOC_WR_TIMER_STATUS:
		retval = __get_user(tmp, (__u8 __user *)arg);
printk( "[subucom_spi_ioctl](W) TIMER STATUS=%d @subucom_spi.c\n", tmp);
		if (retval == 0) {
			if (tmp & ~SUBUCOM_TIMER_ON) {
				retval = -EINVAL;
				break;
			}

			if (tmp && !priv->timer_status) {
printk("[INFO]:%s is calling subucom_spi_timer_start()...\n", __func__); /* debug print */
				subucom_spi_timer_start(priv, priv->timer_interval);
			}
			else if (!tmp && priv->timer_status) {
printk("[INFO]:%s is calling subucom_spi_timer_stop()...\n", __func__); /* debug print */
				subucom_spi_timer_stop(priv);
			}
			else {
printk("[WARN]:%s timer status is wrong. tmp:%d timer_status:%d\n",__func__, tmp, priv->timer_status);
			}

			dev_dbg(&spi->dev, "timer status %s\n", tmp ? "ON" : "OFF");
		}
		break;
	case SUBUCOM_IOC_WR_TIMER_INTERVAL:
		retval = __get_user(tmp, (__u32 __user *)arg);
		if (retval == 0) {
			priv->timer_interval = (unsigned long)tmp;
#if JIFFIES_TEST
			jiffies_test(priv->timer_interval);
#endif
printk( "[subucom_spi_ioctl](W) TIMER INTERVAL=%d @subucom_spi.c\n", priv->timer_interval);
		}
		break;
	case SUBUCOM_IOC_WR_BITS_PER_WORD:
		retval = __get_user( tmp, (__u16 __user *)arg);
		if (retval == 0) {
			u16	save = spi->bits_per_word;
			if (priv->timer_status == SUBUCOM_TIMER_ON) {
				retval = -EBUSY;
				break;
			}
			spi->bits_per_word = tmp;
			retval = spi_setup( spi);
			if (retval < 0) {
				spi->bits_per_word = save;
			}
			else {
				dev_dbg(&spi->dev, "%d bits per word\n", tmp);
			}
			subucom_spi_setup_bytes_per_word( priv);
		}
		break;
	case SUBUCOM_IOC_WR_RX_BYTES:
		retval = __get_user(tmp, (__u32 __user *)arg);
		if (retval == 0) {
			int i;
			if (tmp > SZbufsize) {
				retval = -EINVAL;
				break;
			}
			if (priv->timer_status == SUBUCOM_TIMER_ON) {
				retval = -EBUSY;
				break;
			}

			priv->tx->bytes = (u32)tmp;
			priv->rx_bytes = (u32)tmp;
			for (i = 0; i < txmulti; ++i)
				subucom_spi_setup_length( priv, i);
		}
		break;
#if 0
	case SUBUCOM_IOC_WR_TX_MULTIPLEX:
		retval = __get_user(tmp, (__u8 __user *)arg);
		if (retval == 0) {
			if (tmp > txmulti || tmp == 0) {
				retval = -EINVAL;
				break;
			}
			if (priv->timer_status == SUBUCOM_TIMER_ON) {
				retval = -EBUSY;
				break;
			}
			priv->tx_multi_num = (u8)tmp;
			subucom_spi_init_ids(priv);
		}
		break;
#endif
	case SUBUCOM_IOC_WR_BUSYWAIT_UTIME:
		retval = __get_user( tmp, (__u16 __user *)arg);
		if (retval == 0) {
			//timer disable check
			if (priv->timer_status == SUBUCOM_TIMER_OFF) {	/* TIMER OFF */
				usec = tmp;
				priv->max_cntbusy = usec / SUBCPU_BUSYWAIT_USEC;
				if (usec < SUBCPU_BUSYWAIT_USEC) {
					priv->max_cntbusy++;
				}
printk( "[subucom_spi_ioctl] BUSY WAIT=%d[usec] cnt=%d @subucom_spi.c\n", usec, priv->max_cntbusy);
			}
			else {
				retval = -EBUSY;
			}
		}
		break;
	case SUBUCOM_GPIO_WR_PNL_REQ:
		retval = __get_user( tmp, (__u32 __user *)arg);
		if (retval == 0) {
			priv->pnl_req = (uint8_t)tmp;

			if (priv->pnl_req) {	//update mode?
				priv->rx_bytes = SUBUCOM_SPI_DATA_RXLEN_UPDATE;
			} else {
				priv->rx_bytes = SUBUCOM_SPI_DATA_SIZE_NORMAL;
			}

printk( "[subucom_spi_ioctl](W) PNL_REQ=%d @subucom_spi.c\n", priv->pnl_req);
		}
		break;
	case SUBUCOM_GPIO_WR_PNL_xINT:
		retval = __get_user( tmp, (__u32 __user *)arg);
printk( "[subucom_spi_ioctl]case SUBUCOM_GPIO_WR_PNL_xINT __get_user:%d @subucom_spi.c\n", retval);
		if (retval == 0) {
			gpio_set_value( GPIO_NUM_G2M_PNL_xINT, tmp);
printk( "[subucom_spi_ioctl](W) PNL_xINT:%d @subucom_spi.c\n", tmp);
		}
		break;



	case SUBUCOM_GPIO_RD_PNL_REQ:
		gp_req   = gpio_get_value( GPIO_NUM_G2M_PNL_REQ);
		priv->pnl_req	= (uint8_t)gp_req;

		retval = __put_user( gp_req, (__u32 __user *)arg);
printk( "[subucom_spi_ioctl](R) PNL_REQ:%d %d @subucom_spi.c\n", gp_req, retval);
		break;
	case SUBUCOM_GPIO_RD_PNL_WRITE:
		gp_write = gpio_get_value( GPIO_NUM_G2M_PNL_WRITE);
		retval = __put_user( gp_write, (__u32 __user *)arg);
printk( "[subucom_spi_ioctl](R) PNL_WRITE:%d %d @subucom_spi.c\n", gp_write, retval);
		break;

	default:
		/* segmented and/or Tx message I/O request */
		if (_IOC_NR( cmd) != _IOC_NR( SUBUCOM_IOC_MESSAGE( 0))
				|| _IOC_DIR( cmd) != _IOC_WRITE) {
			retval = -ENOTTY;
			break;
		}

		tmp = _IOC_SIZE( cmd);
		if ((tmp % sizeof( struct subucom_ioc_transfer)) != 0) {
			retval = -EINVAL;
			break;
		}
		n_ioc = tmp / sizeof( struct subucom_ioc_transfer);
		if (n_ioc == 0)
			break;

		if (__copy_from_user( &priv->ioc, (void __user *)arg, tmp)) {
			retval = -EFAULT;
			break;
		}

		retval = subucom_setup_txbufnew( priv);
		break;
	}
	spi_dev_put( spi);
	return retval;
}
//******************************************************************************
//* name : subucom_spi_open
//******************************************************************************
static int subucom_spi_open(struct inode *inode, struct file *filp)
{
	struct subucom_spi_dev	*priv;
	int			status = -ENXIO;

	mutex_lock(&device_list_lock);

	list_for_each_entry(priv, &device_list, device_entry)
	{
		if (priv->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {
		++priv->users;
		filp->private_data = priv;
		nonseekable_open(inode, filp);
	} else {
		pr_debug("subucom_spi: nothing for minor %d\n", iminor(inode));
	}

	mutex_unlock(&device_list_lock);

	return status;
}
//******************************************************************************
//* name : subucom_spi_release
//******************************************************************************
static int subucom_spi_release(struct inode *inode, struct file *filp)
{
	struct subucom_spi_dev	*priv;
	int			status = 0;

	mutex_lock(&device_list_lock);

	priv = filp->private_data;
	filp->private_data = NULL;

#if 0 /* TODO: temporally */
	subucom_spi_timer_stop(priv);
#endif

	/* TODO: last close? */
	--priv->users;
	if (!priv->users) {
		int		dofree;

		/* ... after we unbound from the underlying device? */
		spin_lock_irq(&priv->spi_lock);
		dofree = (priv->spi == NULL);
		spin_unlock_irq(&priv->spi_lock);

		if (dofree) {
			kfree(priv->tx);
			kfree(priv->tx_buf);
			kfree(priv->rx_buf);
			kfree(priv);
		}
	}
	mutex_unlock(&device_list_lock);

	return status;
}

static const struct file_operations subucom_spi_fops = {
	.owner		= THIS_MODULE,
	.read		= subucom_spi_read,
	.write		= subucom_spi_write,
	.poll		= subucom_spi_poll,
	.unlocked_ioctl = subucom_spi_ioctl,
	.open		= subucom_spi_open,
	.release	= subucom_spi_release,
};

//******************************************************************************
//* name : joglcd_spi_rw()
//******************************************************************************
static int32_t joglcd_spi_rw(struct subucom_spi_dev *priv, uint8_t *tx_buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf	= tx_buf,
		.rx_buf	= NULL,
		.len	= len,
	};
	struct spi_message m;
	int32_t	status;

	if (priv == NULL) {
		return -EIO;
	}

	spi_message_init( &m);
	spi_message_add_tail( &t, &m);

	/* tcss min 10ns?? 20ns ?? */
	gpio_set_value( GPIO_NUM_G2M_JLCD_SEL, JLCD_SEL_JOG);
	status = subucom_spi_sync( priv, &m);
	/* tcsh min 10nsec */
	gpio_set_value( GPIO_NUM_G2M_JLCD_SEL, JLCD_SEL_PANEL);

#ifdef LOG_PRINTK
printk( "bits_per:%d status=%d [%02x,%02x,%02x] len=%d [joglcd_spi_rw]\n",
            priv->spi->bits_per_word, status, tx_buf[0],tx_buf[1],tx_buf[2],(int)len);
#endif
	return status;
}

//******************************************************************************
//* name : joglcd_param_init_set
//******************************************************************************
static int	joglcd_param_init_set( struct subucom_spi_dev *priv )
{
	int32_t	i, status;
	uint8_t	*sdata;

	if (priv == NULL) {
		return -ENOMEM;
	}
	sdata = priv->tx_buf;
	if (sdata == NULL) {
		return -ENOMEM;
	}

	for (i=0;(joglcd_reg_wdata[i][0] != 0xFFFF) && (joglcd_reg_wdata[i][1] != 0xFFFF); i++) {
		if (joglcd_reg_wdata[i][0] == 0xF040) {
			msleep(40);
			continue;
		}
		if (joglcd_reg_wdata[i][0] == 0xF100) {
			msleep(100);
			continue;
		}
		sdata[0] = 0x70;    //01110000
						    //      SW ... RS=0,RW=0
		sdata[1] = (uint8_t)((joglcd_reg_wdata[i][0] >> 8) & 0x00FF);
		sdata[2] = (uint8_t)(joglcd_reg_wdata[i][0] & 0x00FF);
		status = joglcd_spi_rw( priv, sdata, 3);
		msleep(1);

		sdata[0] = 0x72;    //01110010
						    //      SW ... RS=1,RW=0
		sdata[1] = (uint8_t)((joglcd_reg_wdata[i][1] >> 8) & 0x00FF);
		sdata[2] = (uint8_t)(joglcd_reg_wdata[i][1] & 0x00FF);
		status = joglcd_spi_rw( priv, sdata, 3);
		msleep(1);
	}
	return status;
}
//******************************************************************************
//* name : subucom_spi_probe
//******************************************************************************
static int subucom_spi_probe(struct spi_device *spi)
{
	int32_t		status = 0;
	int32_t		i;
	uint32_t	minor;
	struct subucom_spi_dev	*priv;
	struct subucom_tx_part	*tx;

printk( VERSION_STRING"[subucom_spi_probe]\r\n");
//******************************************************************************
//** ALLOCATE memory
//******************************************************************************
	priv = kzalloc( sizeof(struct subucom_spi_dev), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rx_buf = kzalloc( SZbufsize * sizeof(uint8_t) * 2, GFP_KERNEL);
	if (!priv->rx_buf) {
		kfree( priv);
		return -ENOMEM;
	}
	priv->rx_bufold = priv->rx_buf + SZbufsize;

	priv->tx_buf = kzalloc( SZbufsize * txmulti * txpart * sizeof(uint8_t) * 2, GFP_KERNEL);
	if (!priv->tx_buf) {
		kfree( priv->rx_buf);
		kfree( priv);
		return -ENOMEM;
	}
	priv->tx_bufnew = priv->tx_buf + (SZbufsize * txmulti * txpart);

	priv->tx = kzalloc(txmulti * sizeof( struct subucom_tx_part), GFP_KERNEL);
	if (!priv->tx) {
		kfree( priv->tx_buf);
		kfree( priv->rx_buf);
		kfree( priv);
		return -ENOMEM;
	}

	spin_lock_init( &priv->spi_lock);
	mutex_init( &priv->transfer_lock);
	mutex_init( &priv->tx_buf_lock);
	mutex_init( &priv->rx_buf_lock);
	mutex_init( &priv->buffer_lock);
	init_waitqueue_head( &priv->spi_wait);
	INIT_LIST_HEAD( &priv->device_entry);

printk( "minors:0x%lx [subucom_spi_probe]\n", minors[0]);
	mutex_lock( &device_list_lock);
	minor = find_first_zero_bit( minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		// create Device
		struct device *dev;
		priv->devt = MKDEV( majorNumber, minor);
		dev = device_create( subucom_spi_class, &spi->dev, priv->devt,
				    priv, "subucom_spi%d.%d",
				    spi->master->bus_num, spi->chip_select);

		status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	} else {
		dev_dbg(&spi->dev, "no minor number available!\n");
		status = -ENODEV;
	}

	if (status == 0) {
		set_bit( minor, minors);
		list_add( &priv->device_entry, &device_list);
	}
	mutex_unlock( &device_list_lock);

printk( "maj:%d minor:%d device_create:%d subucom_spi:bus=%d cs=%d [subucom_spi_probe]\n",
			majorNumber, minor, status, spi->master->bus_num, spi->chip_select);

//******************************************************************************
//** GPIO initialize
//******************************************************************************
    gpio_direction_output( GPIO_NUM_G2M_JLCD_SEL, JLCD_SEL_PANEL);

	/* setting GPIO ... handshake */
    gpio_direction_output( GPIO_NUM_G2M_PNL_xINT, PNL_xINT_DISABLE);
    gpio_direction_input( GPIO_NUM_G2M_PNL_WRITE);
    gpio_direction_input( GPIO_NUM_G2M_PNL_REQ);

	gpio_set_value( GPIO_NUM_G2M_PNL_xINT, PNL_xINT_DISABLE);
	priv->pnl_req	= (uint8_t)gpio_get_value( GPIO_NUM_G2M_PNL_REQ);	//0:normal 1:update
printk( "GPIO PNL_WRITE:%d PNL_REQ:%d [subucom_spi_probe]\n", gpio_get_value( GPIO_NUM_G2M_PNL_WRITE), gpio_get_value( GPIO_NUM_G2M_PNL_REQ));

//******************************************************************************
//** JOG LCD SPI initialize
//******************************************************************************
printk( "SPI clock %d[Hz]\n", spi->max_speed_hz);
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = JOGLCD_SPI_BITS_PER_WORD;
	status = spi_setup( spi);
	if (status < 0) {
		printk( "ERROR! MSBfirst 8bits SPI [subucom_spi_probe]\n");
		return status;
	}
	priv->spi = spi;
	subucom_spi_setup_bytes_per_word( priv);
	spi_set_drvdata( spi, priv);
dev_info( &spi->dev, "JLCD SPI device initialized [subucom_spi_probe]\n");

	status = joglcd_param_init_set( priv);

	spin_lock_irq( &priv->spi_lock);
	priv->spi = NULL;
	spi_set_drvdata( spi, NULL);
	spin_unlock_irq( &priv->spi_lock);
	udelay( 100);	/**100usec delay**/

#ifdef ENB_SPICLOCK_SEL							/* updateの時 CLOCK 分ける場合 定義 */
	priv->pnl_req &= (uint8_t)gpio_get_value( GPIO_NUM_G2M_PNL_REQ);	//0:normal 1:update
	if (priv->pnl_req) {
		spi->max_speed_hz = SPICLOCK_UPDATE;
		printk( "G2M_PNL_REQ:%d request SPI clock %d[Hz]\n", priv->pnl_req, spi->max_speed_hz);
	}
#endif

//******************************************************************************
//** JOG LCD SPI initialize
//******************************************************************************
	//	include/linux/spi/spi.h
		//SPI通信 CPOLとCPHAの値によって4つのモードを定義
		//	CPOL=0 クロックは正論理
		//	CPOL=1 クロックは負論理
		//	CPHA=0 0から1に切替わるタイミングで データ取込む
		//	CPHA=1 1から0に切替わるタイミングで データ取込む
		//      u16	mode;
		//SPI_CPHA	0x01			/* clock phase */
		//SPI_CPOL	0x02			/* clock polarity */
		//SPI_MODE_0	( 0 | 0 )			/* (original MicroWire) */
		//SPI_MODE_1	( 0 | SPI_CPHA)
		//SPI_MODE_2	(SPI_CPOL | 0)
		//SPI_MODE_3	(SPI_CPOL | SPI_CPHA)
		//
		//SPI_CS_HIGH	0x04			/* chipselect active high? */
		//SPI_LSB_FIRST	0x08			/* per-word bits-on-wire */
		//SPI_3WIRE		0x10			/* SI/SO signals shared */
		//SPI_LOOP		0x20			/* loopback mode */
		//SPI_NO_CS		0x40			/* 1 dev/bus, no chipselect */
		//SPI_READY		0x80			/* slave pulls low to pause */
		//SPI_TX_DUAL	0x100			/* transmit with 2 wires */
		//SPI_TX_QUAD	0x200			/* transmit with 4 wires */
		//SPI_RX_DUAL	0x400			/* receive with 2 wires */
		//SPI_RX_QUAD	0x800			/* receive with 4 wires */
printk( "SPI clock %d[Hz]\n", spi->max_speed_hz);
printk( "for PANEL-MICRO_COM RE-setup spi_setup LSBfirst 32bits SPI [subucom_spi_probe]\n");
	spi->mode = SPI_MODE_3 | SPI_LSB_FIRST;
	spi->bits_per_word = SUBUCOM_SPI_BITS_PER_WORD;
	status	= spi_setup( spi);
	if (status < 0) {
		printk( "ERROR! LSBfirst 32bits SPI [subucom_spi_probe]\n");
		return status;
	}
	priv->spi = spi;
	subucom_spi_setup_bytes_per_word( priv);
	spi_set_drvdata( spi, priv);
dev_info( &spi->dev, "PANEL SPI device initialized [subucom_spi_probe]\n");
	priv->rx_bytes = SUBUCOM_SPI_TRANSFER_SIZE_NORMAL;

	priv->tx_multi_num	= 1;
	priv->tx_multi_id	= 0;
	tx = priv->tx;
	for (i = 0; i < txmulti; ++i) {
		tx->part_num= 1;
		tx->part_id	= 0;
		tx->updated	= 0;
		tx->bytes	= SUBUCOM_SPI_TRANSFER_SIZE_NORMAL;
		subucom_spi_setup_length( priv, i);
		tx->buf		= priv->tx_buf + (SZbufsize * i * txpart);
		tx->bufnew	= priv->tx_bufnew + (SZbufsize * i * txpart);
		++tx;
	}

	pm_ready = true;
	priv->timer_interval = SUBUCOM_SPI_TIMER_INTERVAL;
	status = subucom_spi_thread_setup( priv);
	if (status == 0) {
		init_timer( &priv->spi_timer);
		if (DEFtmstat) {
			subucom_spi_timer_start( priv, priv->timer_interval);
		}
	}
	init_completion( &priv->done);
	priv->max_cntbusy = CNT_SUBCPU_BUSYWAIT;		//10cnt X 50usec = 500usec

	return status;
}
//******************************************************************************
//* name : subucom_spi_remove
//******************************************************************************
static int subucom_spi_remove(struct spi_device *spi)
{
	struct subucom_spi_dev	*priv = spi_get_drvdata(spi);

	subucom_spi_thread_kill( priv);
	subucom_spi_timer_stop( priv);

	spin_lock_irq( &priv->spi_lock);
	priv->spi = NULL;
	spi_set_drvdata( spi, NULL);
	spin_unlock_irq( &priv->spi_lock);

	mutex_lock( &device_list_lock);
	list_del( &priv->device_entry);
	device_destroy( subucom_spi_class, priv->devt);
	clear_bit( MINOR( priv->devt), minors);

	kfree( priv->tx);
	kfree( priv->tx_buf);
	kfree( priv->rx_buf);
	kfree( priv);
	mutex_unlock( &device_list_lock);

	return 0;
}

static const struct of_device_id subucom_of_match[] = {
	{ .compatible = "renesas,sh-msiof1", },		//pioneerdj,subucom", },
	{ }
};
MODULE_DEVICE_TABLE(of, subucom_of_match);

static const struct spi_device_id subucom_spi_id[] = {
	{ "subucom", 0 },
	{ }
};
MODULE_DEVICE_TABLE( spi, subucom_spi_id);

static int subucom_spi_suspend(struct device *dev)
{
	printk("[%s]\n", __func__ );
	pm_ready = false;
	return 0;
}

static int subucom_spi_resume(struct device *dev)
{
	printk("[%s]\n", __func__ );
	pm_ready = true;
	return 0;
}
static SIMPLE_DEV_PM_OPS(subucom_spi_pm_ops, subucom_spi_suspend, subucom_spi_resume);

static struct spi_driver subucom_spi_driver = {
	.driver  = {
		.name   = DRV_NAME,													//"subucom_spi"
		.of_match_table = subucom_of_match,
																			//.bus	= &spi_bus_type,
		.owner  = THIS_MODULE,
		.pm = &subucom_spi_pm_ops,
	},
	.probe  = subucom_spi_probe,
	.remove = subucom_spi_remove,
};

//******************************************************************************
//* name : subucom_spi_init
//******************************************************************************
static __init int	subucom_spi_init( void )
{
	int32_t	status;
pr_info("subucom SPI user interface\n");

#if JIFFIES_TEST
	jiffies_test(1);		//1ms
	jiffies_test(3);		//3ms
	jiffies_test(5);		//5ms
	jiffies_test(10);		//10ms
	jiffies_test(100);		//100ms
	jiffies_test(1000);		//1000ms
#endif
	_debug = 0;

	majorNumber = register_chrdev( 0, DRV_NAME, &subucom_spi_fops);
	if (majorNumber < 0) {
		printk( "[subucom_spi_init] ERROR=%d register_chrdev @subucom_spi.c\n", majorNumber);
		return majorNumber;
	}
	printk( "[subucom_spi_init] Major=%d register_chrdev @subucom_spi.c\n", majorNumber);

	subucom_spi_class = class_create(THIS_MODULE, DRV_NAME"class");
	if (IS_ERR(subucom_spi_class)) {
		unregister_chrdev( majorNumber, subucom_spi_driver.driver.name);
		printk( "[subucom_spi_init] ERROR class_create @subucom_spi.c\n");
		return PTR_ERR( subucom_spi_class);
	}

	status = spi_register_driver( &subucom_spi_driver);
	if (status < 0) {
		class_destroy( subucom_spi_class);
		unregister_chrdev( majorNumber, DRV_NAME);
		printk( "[subucom_spi_init] ERROR=%d spi_register_driver @subucom_spi.c\n", status);
	}
	return status;
}

//******************************************************************************
//* name : subucom_spi_exit
//******************************************************************************
static __exit void subucom_spi_exit(void)
{
	spi_unregister_driver( &subucom_spi_driver);
	device_destroy( subucom_spi_class, MKDEV( majorNumber, 0));
	class_destroy( subucom_spi_class);
	unregister_chrdev( majorNumber, DRV_NAME);
}

module_init( subucom_spi_init);
module_exit( subucom_spi_exit);

MODULE_AUTHOR("Yasunori Shibata <yasunori.shibata@pioneerdj.com>");
MODULE_DESCRIPTION("subucom SPI user interface");
MODULE_LICENSE("GPL");
