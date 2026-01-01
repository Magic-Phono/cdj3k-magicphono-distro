#ifndef _GPIODRV_H_
#define	_GPIODRV_H_

#include <linux/ioctl.h>

/* デバイスファイル名 */
#define	GPIODRV_DEVFILE			"/dev/gpiodrv"

/* gpio計算マクロ
 * ... 例：GPIO1_3 の場合、 gpio = G2M_GPIO_NR(1, 3)
 */
#ifndef G2M_GPIO_NR
#define G2M_GPIO_0		(496)
#define G2M_GPIO_1		(467)
#define G2M_GPIO_2		(452)
#define G2M_GPIO_3		(436)
#define G2M_GPIO_4		(418)
#define G2M_GPIO_5		(392)
#define G2M_GPIO_6		(360)
#define G2M_GPIO_7		(356)
#define G2M_GPIO_NR(bank, nr)		(G2M_GPIO_##bank + (nr))
#endif

/* ioctl() コマンド定義 */
#define	GPIODRV_IOC_MAGIC			0xF1
#define	GPIODRV_IOC_SETISR			_IOW(GPIODRV_IOC_MAGIC, 1, int)		/* 割り込みセット */
#define	GPIODRV_IOC_CLRISR			_IOW(GPIODRV_IOC_MAGIC, 2, int)		/* 割り込みクリア */
#define	GPIODRV_IOC_ENABLEIRQ		_IO(GPIODRV_IOC_MAGIC, 3)			/* 割り込み有効 */
#define	GPIODRV_IOC_DISABLEIRQ		_IO(GPIODRV_IOC_MAGIC, 4)			/* 割り込み無効 */

/* 割り込みトリガー
 * ... 呼び出し側(アプリ)で、<linux/interrupt.h>をインクルードさせるのが大変そうなので
 *     定義した。
 */
#define GPIODRV_TRIGGER_NONE		0x00000000
#define GPIODRV_TRIGGER_RISING		0x00000001
#define GPIODRV_TRIGGER_FALLING		0x00000002
#define	GPIODRV_TRIGGER_BOTH		(GPIODRV_TRIGGER_RISING | GPIODRV_TRIGGER_FALLING)
#define GPIODRV_TRIGGER_HIGH		0x00000004
#define GPIODRV_TRIGGER_LOW			0x00000008
#define	GPIODRV_TRIGGER_LEVEL		(GPIODRV_TRIGGER_HIGH | GPIODRV_TRIGGER_LOW)

/* 割り込み情報 */
struct gpioirq_inf {
	int				gpio;		/* IMX_GPIO_NR(bank, nr) */
	unsigned long	trigger;	/* GPIODRV_TRIGGER_xxxx */
};

#endif	/* _GPIODRV_H_ */
