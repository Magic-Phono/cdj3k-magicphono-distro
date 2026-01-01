/*
 * rcar_du_drm.h  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_DRM_H__
#define __RCAR_DU_DRM_H__

struct rcar_du_vmute {
	int crtc_id;	/* CRTCs ID */
	int on;		/* Vmute function ON/OFF */
};

/* DRM_IOCTL_RCAR_DU_SET_SCRSHOT:VSPD screen shot */
struct rcar_du_screen_shot {
	unsigned long	buff;
	unsigned int	buff_len;
	unsigned int	crtc_id;
	unsigned int	fmt;
	unsigned int	width;
	unsigned int	height;
};

/* DRM_RCAR_DU_PAGE_FLIP */
struct rcar_du_page_flip {
	__u32 crtc_id;
	__u32 fb_id;
	__u32 flags;
	__u32 sequence;
	__u64 user_data;
};

/* rcar-du + vspd specific ioctls */
#define DRM_RCAR_DU_SET_VMUTE		0
#define DRM_RCAR_DU_SCRSHOT		4
#define DRM_RCAR_DU_PAGE_FLIP		8

#define DRM_IOCTL_RCAR_DU_SET_VMUTE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_RCAR_DU_SET_VMUTE, \
		struct rcar_du_vmute)

#define DRM_IOCTL_RCAR_DU_SCRSHOT \
	DRM_IOW(DRM_COMMAND_BASE + DRM_RCAR_DU_SCRSHOT, \
		struct rcar_du_screen_shot)

#define DRM_IOCTL_RCAR_DU_PAGE_FLIP \
	DRM_IOW(DRM_COMMAND_BASE + DRM_RCAR_DU_PAGE_FLIP, \
		struct rcar_du_page_flip)

#endif /* __RCAR_DU_DRM_H__ */
