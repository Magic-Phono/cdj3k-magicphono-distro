/*
 * subucom_spi.h
 *
 * Copyright (C) 2015-2017 Pioneer DJ Corp.
 * Copyright (C) 2011-2014 Pioneer Corp.
 * Yasurnori Shibata <yasunori.shibata@pioneerdj.com>
 *
 * Based on spidev.h by Andrea Paterniani
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _SUBUCOM_SPI_H
#define _SUBUCOM_SPI_H

#include <linux/types.h>

#define SUBUCOM_TIMER_ON		1
#define SUBUCOM_TIMER_OFF		0

#define SUBUCOM_IOC_MAGIC		'p'

struct subucom_ioc_transfer {
	__u8		multi_id;	/* multiplex index */
	__u8		_reserved;
	__u8		part_id;	/* partition index */
	__u8		part_num;	/* number of partitions */
	__u32		bytes;		/* bytes */
	__u64		tx_buf;		/* Tx buffer pointer */
};

#define SUBUCOM_MSGSIZE(N) \
	((((N)*(sizeof (struct subucom_ioc_transfer))) < (1 << _IOC_SIZEBITS)) \
	? ((N)*(sizeof (struct subucom_ioc_transfer))) : 0)

/* Transmit Tx message */
#define SUBUCOM_IOC_MESSAGE(N)	_IOW(SUBUCOM_IOC_MAGIC, 0, char[SUBUCOM_MSGSIZE(N)])

/* Read / Write timer status */
#define SUBUCOM_IOC_RD_TIMER_STATUS	_IOR(SUBUCOM_IOC_MAGIC, 1, __u8)
#define SUBUCOM_IOC_WR_TIMER_STATUS	_IOW(SUBUCOM_IOC_MAGIC, 1, __u8)

/* Read / Write timer interval msec */
#define SUBUCOM_IOC_RD_TIMER_INTERVAL	_IOR(SUBUCOM_IOC_MAGIC, 2, __u32)
#define SUBUCOM_IOC_WR_TIMER_INTERVAL	_IOW(SUBUCOM_IOC_MAGIC, 2, __u32)

/* Read / Write SPI device word length (1..N) */
#define SUBUCOM_IOC_RD_BITS_PER_WORD	_IOR(SUBUCOM_IOC_MAGIC, 3, __u16)
#define SUBUCOM_IOC_WR_BITS_PER_WORD	_IOW(SUBUCOM_IOC_MAGIC, 3, __u16)

/* Read / Write SPI Rx bytes */
#define SUBUCOM_IOC_RD_RX_BYTES		_IOR(SUBUCOM_IOC_MAGIC, 4, __u32)
#define SUBUCOM_IOC_WR_RX_BYTES		_IOW(SUBUCOM_IOC_MAGIC, 4, __u32)

/* Read / Write limit wait times(usec) PANEL busy */
#define SUBUCOM_IOC_RD_BUSYWAIT_UTIME	_IOR(SUBUCOM_IOC_MAGIC, 6, __u16)
#define SUBUCOM_IOC_WR_BUSYWAIT_UTIME	_IOW(SUBUCOM_IOC_MAGIC, 6, __u16)

#define SUBUCOM_IOC_WR_DEBUG			_IOW(SUBUCOM_IOC_MAGIC, 7, __u8)

#define SUBUCOM_GPIO_RD_PNL_REQ			_IOR(SUBUCOM_IOC_MAGIC, 10, __u32)
#define SUBUCOM_GPIO_WR_PNL_REQ			_IOW(SUBUCOM_IOC_MAGIC, 10, __u32)

#define SUBUCOM_GPIO_RD_PNL_WRITE		_IOR(SUBUCOM_IOC_MAGIC, 11, __u32)
#define SUBUCOM_GPIO_WR_PNL_xINT		_IOW(SUBUCOM_IOC_MAGIC, 12, __u32)

/* Read / Write multiplex number of SPI Tx data */
#if 0
#define SUBUCOM_IOC_RD_TX_MULTIPLEX	_IOR(SUBUCOM_IOC_MAGIC, 5, __u8)
#define SUBUCOM_IOC_WR_TX_MULTIPLEX	_IOW(SUBUCOM_IOC_MAGIC, 5, __u8)
#endif

#ifndef swab16
#define swab16(x) ((__u16)(					\
	(((__u16)(x) & (__u16)0x00ffU) << 8) |			\
	(((__u16)(x) & (__u16)0xff00U) >> 8)))
#endif

#ifndef swab32
#define swab32(x) ((__u32)(					\
	(((__u32)(x) & (__u32)0x000000ffUL) << 24) |		\
	(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |		\
	(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |		\
	(((__u32)(x) & (__u32)0xff000000UL) >> 24)))
#endif

#ifdef __KERNEL__

struct subucom_spi_dev {
	dev_t				devt;
	spinlock_t			spi_lock;
	struct spi_device	*spi;

	struct list_head	device_entry;

	struct timer_list	spi_timer;
	wait_queue_head_t	spi_wait;
	uint8_t				timer_status;
	uint32_t			timer_interval;		//unsigned

	struct task_struct	*spi_thread;
	struct task_struct	*dbg_thread;
	int32_t				event_flg;			//int /* dummy event flag */

	uint32_t			users;				//unsigned

	uint32_t			bytes_per_word;		//unsigned
	struct mutex		transfer_lock;

	int32_t				rx_detected;		//int
	uint32_t			rx_bytes;			//unsigned
	struct mutex		rx_buf_lock;
	uint8_t				*rx_buf;
	uint8_t				*rx_bufold;

	uint16_t			tx_multi_num;	/* number of multiplexes */
	uint16_t			tx_multi_id;	/* current multiplex */
	struct subucom_tx_part
						*tx;
	struct mutex		tx_buf_lock;
	uint8_t				*tx_buf;
	uint8_t				*tx_bufnew;

	struct mutex		buffer_lock;
	uint8_t				*buffer;

	struct subucom_ioc_transfer	ioc;

	struct completion	done;
	uint16_t			max_cntbusy;

	uint8_t				pnl_req;		/* PANEL(SUBcom) GPIO REQ status bit */
};

#endif /* __KERNEL__ */

#endif /* _SUBUCOM_SPI_H */
