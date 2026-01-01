// SPDX-License-Identifier: GPL-2.0
/*
 * u_uac2.h
 *
 * Utility definitions for UAC2 function
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 */

#ifndef U_UAC2_H
#define U_UAC2_H

#include <linux/usb/composite.h>
#ifdef CONFIG_PDJ
#include "u_audio.h"
#endif

#define UAC2_DEF_PCHMASK 0x3
#define UAC2_DEF_PSRATE 48000
#define UAC2_DEF_PSSIZE 2
#define UAC2_DEF_CCHMASK 0x3
#define UAC2_DEF_CSRATE 64000
#define UAC2_DEF_CSSIZE 2
#define UAC2_DEF_REQ_NUM 2

struct f_uac2_opts {
	struct usb_function_instance	func_inst;
	int				p_chmask;
	int				p_srate;
	int				p_ssize;
	int				c_chmask;
#ifdef CONFIG_PDJ
	int				c_srate[UAC_MAX_RATES];
	int				c_srate_active;
#else
	int				c_srate;
#endif
	int				c_ssize;
	int				req_number;
	bool				bound;

	struct mutex			lock;
	int				refcnt;
};

#ifdef CONFIG_PDJ
#define UAC_RATE_ATTRIBUTE(name)					\
static ssize_t f_uac_opts_##name##_show(struct config_item *item,	\
					 char *page)			\
{									\
	struct f_uac2_opts *opts = to_f_uac2_opts(item);			\
	int result = 0;							\
	int i;								\
									\
	mutex_lock(&opts->lock);					\
	page[0] = '\0';							\
	for (i = 0; i < UAC_MAX_RATES; i++) {				\
		if (opts->name[i] == 0)					\
			continue;					\
		result += sprintf(page + strlen(page), "%u,",		\
				opts->name[i]);				\
	}								\
	if (strlen(page) > 0)						\
		page[strlen(page) - 1] = '\n';				\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_uac_opts_##name##_store(struct config_item *item,	\
					  const char *page, size_t len)	\
{									\
	struct f_uac2_opts *opts = to_f_uac2_opts(item);			\
	char *split_page = NULL;					\
	int ret = -EINVAL;						\
	char *token;							\
	u32 num;							\
	int i;								\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	i = 0;								\
	memset(opts->name, 0x00, sizeof(opts->name));			\
	split_page = kstrdup(page, GFP_KERNEL);				\
	while ((token = strsep(&split_page, ",")) != NULL) {		\
		ret = kstrtou32(token, 0, &num);			\
		if (ret)						\
			goto end;					\
									\
		opts->name[i++] = num;					\
		opts->name##_active = num;				\
		ret = len;						\
	};								\
									\
end:									\
	kfree(split_page);						\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
CONFIGFS_ATTR(f_uac_opts_, name)
#endif

#endif
