#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include "xf86.h"
#include "shadow.h"

/* for visuals */
#include "fb.h"

#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include <drm/rcar_du_drm.h>

#include "flip_control.h"

/* -------------------------------------------------------------------- */
/* Structure Declaration for flip control                               */

#define FRAME_BUFFER_NUM	(3)

typedef struct {
	uint32_t	handle;
	uint32_t	fb_id;
	int			index;
	int			width;
	int			height;
	int			bpp;		/* bit per pixel */
	int			pitch;
	void		*map;
	int			size;
} FrameBuffer, *FrameBufferPtr;

typedef struct
{
	pthread_t			thread;
	pthread_mutex_t		mutex;
	pthread_cond_t		cv;
	int					canceled;
	int					fd;
	drmModeResPtr		mres;
	drmModeConnectorPtr	conn;
	drmModeCrtc			crtc;
	drmModeEncoderPtr	enc;
	FrameBuffer 		fb[FRAME_BUFFER_NUM];
	FrameBufferPtr		draw_fb;
	FrameBufferPtr		unused_fbs[FRAME_BUFFER_NUM];
	FrameBufferPtr		disp_fb;
	FrameBufferPtr		wait_fb;
} DrmMode, *DrmModePtr;

typedef struct
{
	DrmModePtr drm;
	FrameBufferPtr fb;
} FlipItem, *FlipItemPtr;


/* -------------------------------------------------------------------- */
/* Global variables for flip control                                    */
static DrmModePtr g_drm = NULL;

static void
ShowFrameBuffer(const char* label, const DrmModePtr drm)
{
#if 0
	xf86DrvMsg(-1, X_INFO, "%s draw=%d, wait=%d, disp=%d, unused=[%d,%d,%d]\n", label,
		drm->draw_fb ? drm->draw_fb->index : -1,
		drm->wait_fb ? drm->wait_fb->index : -1,
		drm->disp_fb ? drm->disp_fb->index : -1,
		drm->unused_fbs[0] ? drm->unused_fbs[0]->index : -1,
		drm->unused_fbs[1] ? drm->unused_fbs[1]->index : -1,
		drm->unused_fbs[2] ? drm->unused_fbs[2]->index : -1);
#endif
}


static FrameBufferPtr
PopUnusedFrameBuffer(DrmModePtr drm)
{
	FrameBufferPtr fb = NULL;
	/* This function must be called in the state of the critical section by drm->mutex */
	if (drm->unused_fbs[0]) {
		fb = drm->unused_fbs[0];
		drm->unused_fbs[0] = drm->unused_fbs[1];
		drm->unused_fbs[1] = drm->unused_fbs[2];
		drm->unused_fbs[2] = NULL;
	}
	
	return fb;
}

static void
PushUnusedFrameBuffer(DrmModePtr drm, FrameBufferPtr fb)
{
	/* This function must be called in the state of the critical section by drm->mutex */
	if (fb) {
		for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
			if (drm->unused_fbs[i] == NULL) {
				drm->unused_fbs[i] = fb;
				break;
			}
		}
	}
}

static void
ClearUnusedFrameBuffers(DrmModePtr drm)
{
	for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
		drm->unused_fbs[i] = NULL;
	}
}

static int
RequestPageFlip(DrmModePtr drm, FrameBufferPtr fb)
{
	struct rcar_du_page_flip page_flip;
	memset(&page_flip, 0, sizeof(page_flip));
	page_flip.crtc_id = drm->crtc.crtc_id;
	page_flip.fb_id = fb->fb_id;
	page_flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
	FlipItemPtr flipitem = (FlipItemPtr)malloc(sizeof(FlipItem));
	flipitem->drm = drm;
	flipitem->fb = fb;
	page_flip.user_data = (uint64_t)flipitem;

	drm->wait_fb = fb;
	ShowFrameBuffer("RequestPageFlip", drm);
//	xf86DrvMsg(-1, X_DEBUG, "call DRM_IOCTL_RCAR_DU_PAGE_FLIP crtc_id=%d, fb_id=%d, fb=%p\n", page_flip.crtc_id, page_flip.fb_id, fb->index);

	int ret = drmIoctl(drm->fd, DRM_IOCTL_RCAR_DU_PAGE_FLIP, &page_flip);
	if (ret < 0) {
		xf86DrvMsg(-1, X_ERROR, "DRM_IOCTL_RCAR_DU_PAGE_FLIP error !! errno=%d, ret=%d, fb=%d\n", errno, ret, fb->index);
	}

	return ret;
}

static void
PageFlipHandler(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data)
{
	FlipItem flipitem = *(FlipItemPtr)user_data;
	free(user_data);

	DrmModePtr drm = flipitem.drm;
	pthread_mutex_lock(&drm->mutex);
	PushUnusedFrameBuffer(drm, drm->disp_fb);	/* Diplay buffer to be unused buffer */
	drm->disp_fb = flipitem.fb;			/* Wait display buffer to display buffer in next frame */
	drm->wait_fb = NULL;
	ShowFrameBuffer("PageFlipHandler", drm);
	pthread_mutex_unlock(&drm->mutex);
}

static void*
FlipThread(void *arg)
{
	int ret = 0;
	xf86DrvMsg(-1, X_INFO, "FlipThread started.\n");
	DrmModePtr drm = (DrmModePtr)arg;
	
	while(TRUE) {
		pthread_mutex_lock(&drm->mutex);

		/* Wait for display or cancel request */
		while((drm->draw_fb == NULL) && (!drm->canceled))
			ret = pthread_cond_wait(&drm->cv, &drm->mutex);
		
		/* Cancel request */
		if (drm->canceled) {
			xf86DrvMsg(-1, X_INFO, "Flip thread stop requested !!\n");
			pthread_mutex_unlock(&drm->mutex);
			break;
		/* Display request */
		} else if (drm->draw_fb) {
			FrameBufferPtr fb = drm->draw_fb;
			drm->draw_fb = NULL;
			pthread_mutex_unlock(&drm->mutex);
			
			ret = RequestPageFlip(drm, fb);
			if (ret == 0) {
				struct pollfd p = { .fd = drm->fd, .events = POLLIN };
				
				do {
					ret = poll(&p, 1, -1);
				} while((ret == -1) && (errno == EINTR || errno == EAGAIN));

				if (ret < 0) {
					xf86DrvMsg(-1, X_ERROR, "poll() failed with %d\n", errno);
				} else if (ret == 0) {
					/* no events */
				} else {
					drmEventContext ev;
					memset(&ev, 0, sizeof(ev));
					ev.version = 2;
					ev.page_flip_handler = PageFlipHandler;
					drmHandleEvent(drm->fd, &ev);	/* call page_flip_handler in this function. */
				}
			} else {
				xf86DrvMsg(-1, X_ERROR, "RequestPageFlip error !! ret=%d\n", ret);
			}
		} else {
			pthread_mutex_unlock(&drm->mutex);
		}
	}

	xf86DrvMsg(-1, X_INFO, "FlipThread exit.\n");
	pthread_exit(0);
}

static Bool
StartFlipThread(DrmModePtr drm)
{
	pthread_attr_t attr;

	memset(&drm->mutex, 0, sizeof(drm->mutex));
	pthread_mutex_init(&drm->mutex, NULL);
	pthread_cond_init(&drm->cv, NULL);
	drm->canceled = FALSE;

	pthread_attr_init(&attr);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&drm->thread, &attr, FlipThread, (void*)drm);

	if (!drm->thread) {
		xf86DrvMsg(-1, X_ERROR, "StartFlipThread() error !! errno=%d\n", errno);
		return FALSE;
	} else {
		return TRUE;
	}
}

static void
StopFlipThread(DrmModePtr drm)
{
	if (drm->thread > 0) {
		pthread_mutex_lock(&drm->mutex);
		drm->canceled = TRUE;
		pthread_cond_signal(&drm->cv);
		pthread_mutex_unlock(&drm->mutex);
		pthread_join(drm->thread, NULL);
	}
}

Bool
StartFlipMode(int width, int height, int depth, int bpp)
{
	int ret;

	xf86DrvMsg(-1, X_INFO, "StartFlipMode() called. width=%d, height=%d, depth=%d, bpp=%d\n", width, height, depth, bpp);

	if (g_drm != NULL) {
		xf86DrvMsg(-1, X_INFO, "Flip Mode Started.\n");
		return TRUE;
	}

	g_drm = (DrmModePtr)xnfcalloc(sizeof(DrmMode), 1);
	if (g_drm == NULL) {
		xf86DrvMsg(-1, X_ERROR, "xnfcalloc() failed.\n");
		return FALSE;
	}

	g_drm->fd = drmOpen("rcar-du", NULL);
	if (g_drm->fd < 0) {
		xf86DrvMsg(-1, X_ERROR, "drmOpen() failed !! errno=%d\n", errno);
		goto fail;
	}

	ret = drmDropMaster(g_drm->fd);
	if (ret != 0) {
		xf86DrvMsg(-1, X_ERROR, "drmDropMaster() failed !! errno=%d\n", errno);
		goto fail;
	}

	g_drm->mres = drmModeGetResources(g_drm->fd);
	if (!g_drm->mres) {
		xf86DrvMsg(-1, X_ERROR, "drmModeGetResources() failed !! errno=%d\n", errno);
		goto fail;
	}

	xf86DrvMsg(-1, X_DEBUG, "drmModeGetResources() count_fbs=%d, count_crtcs=%d, count_encoders=%d, count_connectors=%d\n",
			g_drm->mres->count_fbs, g_drm->mres->count_crtcs, g_drm->mres->count_encoders, g_drm->mres->count_connectors);
	xf86DrvMsg(-1, X_DEBUG, "drmModeGetResources() min_width=%d, max_width=%d, min_height=%d, max_height=%d\n",
			g_drm->mres->min_width, g_drm->mres->max_width, g_drm->mres->min_height, g_drm->mres->max_height);

	int target_connector = DRM_MODE_CONNECTOR_LVDS;
	for (int i = 0; i < g_drm->mres->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(g_drm->fd, g_drm->mres->connectors[i]);
		if (conn == NULL)
			continue;

		xf86DrvMsg(-1, X_DEBUG, "conn->connector_type=%d\n", conn->connector_type);
		if (conn->connection == DRM_MODE_CONNECTED) {
			if (conn->count_modes > 0 && conn->connector_type == target_connector) {
				xf86DrvMsg(-1, X_DEBUG, "Connector is found !! count_modes=%d, type=%d\n", conn->count_modes, conn->connector_type);
				g_drm->conn = conn;
				break;
			}
		}
		
		if (conn)
			drmModeFreeConnector(conn);
	}

	if (!g_drm->conn) {
		xf86DrvMsg(-1, X_ERROR, "Target connector is NOT found !!\n");
		goto fail;
	}

	xf86DrvMsg(-1, X_DEBUG, "drm->conn->count_encoders=%d\n", g_drm->conn->count_encoders);
	for (int i = 0; i < g_drm->conn->count_encoders; i++) {
		g_drm->enc = drmModeGetEncoder(g_drm->fd, g_drm->conn->encoders[i]);
		if (g_drm->enc) {
			uint32_t crtc_id = g_drm->enc->crtc_id;
			uint32_t possible_crtcs = g_drm->enc->possible_crtcs;
			if (g_drm->enc->encoder_type == DRM_MODE_ENCODER_NONE) {
				xf86DrvMsg(-1, X_INFO, "drm->enc->encoder_type=NONE, crtc_id=%d, possible_crtcs=0x%08x\n", crtc_id, possible_crtcs);
			} else if (g_drm->enc->encoder_type == DRM_MODE_ENCODER_DAC) {
				xf86DrvMsg(-1, X_INFO, "drm->enc->encoder_type=DAC, crtc_id=%d, possible_crtcs=0x%08x\n", crtc_id, possible_crtcs);
			} else if (g_drm->enc->encoder_type == DRM_MODE_ENCODER_LVDS) {
				xf86DrvMsg(-1, X_INFO, "drm->enc->encoder_type=LVDS, crtc_id=%d, possible_crtcs=0x%08x\n", crtc_id, possible_crtcs);
			} else {
				xf86DrvMsg(-1, X_INFO, "drm->enc->encoder_type=%d, crtc_id=%d, possible_crtcs=0x%08x\n", g_drm->enc->encoder_type, crtc_id, possible_crtcs);
			}
			break;
		}
	}

	for (int i = 0; i < g_drm->mres->count_crtcs; i++) {
		drmModeCrtc *crtc = drmModeGetCrtc(g_drm->fd, g_drm->mres->crtcs[i]);
		if (crtc == NULL)
			continue;
		
		if ((1 << i) & g_drm->enc->possible_crtcs)
		{
			g_drm->crtc = *crtc;	/* copy */
			xf86DrvMsg(-1, X_INFO, "crtc->crtc_id=%d, drm->enc->possible_crtcs=0x%08x\n", crtc->crtc_id, g_drm->enc->possible_crtcs);
			xf86DrvMsg(-1, X_INFO, "width=%d, height=%d\n", g_drm->crtc.width, g_drm->crtc.height);
			{
				drmModeModeInfoPtr mode = &g_drm->crtc.mode;
				xf86DrvMsg(-1, X_DEBUG, "hdisplay=%d, hsync_start=%d, hsync_end=%d, htotal=%d, hskew=%d\n",
						mode->hdisplay, mode->hsync_start, mode->hsync_end, mode->htotal, mode->hskew);
				xf86DrvMsg(-1, X_DEBUG, "vdisplay=%d, vsync_start=%d, vsync_end=%d, vtotal=%d, vscan=%d\n",
						mode->vdisplay, mode->vsync_start, mode->vsync_end, mode->vtotal, mode->vscan);
				xf86DrvMsg(-1, X_DEBUG, "vrefresh=%d, flags=%d, type=%d, name=%s, clock=%d\n",
						mode->vrefresh, mode->flags, mode->type, mode->name, mode->clock);
			}
		}

		drmModeFreeCrtc(crtc);
	}

	uint64_t value;
	if (drmGetCap(g_drm->fd, DRM_CAP_DUMB_BUFFER, &value) < 0) {
		xf86DrvMsg(-1, X_ERROR, "drmGetCap() DRM_CAP_DUMB_BUFFER error !! errno=%d\n", errno);
		goto fail;
	} else {
		if (!value) {
			xf86DrvMsg(-1, X_ERROR, "DRM_CAP_DUMB_BUFFER is not supported !!\n");
			goto fail;
		} else {
			xf86DrvMsg(-1, X_INFO, "DRM_CAP_DUMB_BUFFER is supported !!\n");
		}
	}

	for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
		struct drm_mode_create_dumb creq;
		memset(&creq, 0, sizeof(creq));
		creq.width = width;
		creq.height = height;
		creq.bpp = bpp;
		
		ret = drmIoctl(g_drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
		if (ret < 0) {
			xf86DrvMsg(-1, X_ERROR, "drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB) error !! errno=%d\n", errno);
			goto fail;
		}
		
		g_drm->fb[i].handle = creq.handle;

		uint32_t fb;
		ret = drmModeAddFB(g_drm->fd, creq.width, creq.height, depth, creq.bpp, creq.pitch, creq.handle, &fb);
		if (ret < 0) {
			xf86DrvMsg(-1, X_ERROR, "drmModeAddFB error !! errno=%d\n", errno);
			goto fail;
		}

		xf86DrvMsg(-1, X_DEBUG, "drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB) width=%d, height=%d, bpp=%d, flags=0x%08x, handle=0x%08x, fb=%d\n",
				creq.width, creq.height, creq.bpp, creq.flags, creq.handle, fb);
		g_drm->fb[i].fb_id = fb;

		/* prepare buffer for memory mapping */
		struct drm_mode_map_dumb mreq;
		memset(&mreq, 0, sizeof(mreq));
		mreq.handle = creq.handle;
		ret = drmIoctl(g_drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
		if (ret < 0) {
			xf86DrvMsg(-1, X_ERROR, "drmIoctl(DRM_IOCTL_MODE_MAP_DUMB) error !! errno=%d\n", errno);
			goto fail;
		}

		/* perform actual memory mapping */
		void *map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, g_drm->fd, mreq.offset);
		if (map) {
			g_drm->fb[i].width = creq.width;
			g_drm->fb[i].height = creq.height;
			g_drm->fb[i].pitch = creq.pitch;
			g_drm->fb[i].bpp = creq.bpp;
			g_drm->fb[i].map = map;
			g_drm->fb[i].index = i;
			g_drm->fb[i].size = creq.size;
			xf86DrvMsg(-1, X_INFO, "call drmModeAddFB fb_id=%d, width=%d, height=%d, bpp=%d, map=%p, size=%d\n",
				g_drm->fb[i].fb_id, g_drm->fb[i].width, g_drm->fb[i].height, g_drm->fb[i].bpp, g_drm->fb[i].map, g_drm->fb[i].size);
		} else {
			xf86DrvMsg(-1, X_ERROR, "mmap() error !! errno=%d\n", errno);
			goto fail;
		}
	}

	g_drm->draw_fb = NULL;
	ClearUnusedFrameBuffers(g_drm);
	g_drm->disp_fb = NULL;
	g_drm->wait_fb = NULL;
	PushUnusedFrameBuffer(g_drm, &g_drm->fb[0]);
	PushUnusedFrameBuffer(g_drm, &g_drm->fb[1]);
	PushUnusedFrameBuffer(g_drm, &g_drm->fb[2]);

	if (StartFlipThread(g_drm)) {
		xf86DrvMsg(-1, X_INFO, "StartFlipMode() success.\n");
		return TRUE;
	} else {
		goto fail;
	}
fail:
	StopFlipMode();
	return FALSE;
}

void StopFlipMode(void)
{
	if (!g_drm)
		return;

	StopFlipThread(g_drm);

	if (g_drm->conn)
		drmModeFreeConnector(g_drm->conn);
	
	if (g_drm->enc)
		drmModeFreeEncoder(g_drm->enc);
	
	if (g_drm->mres)
		drmModeFreeResources(g_drm->mres);

	for (int i = 0; i < FRAME_BUFFER_NUM; i++) {
		if (g_drm->fb[i].map) {
			munmap(g_drm->fb[i].map, g_drm->fb[i].size);
		}

		if (g_drm->fb[i].fb_id > 0) {
			/* delete framebuffer */
			drmModeRmFB(g_drm->fd, g_drm->fb[i].fb_id);
		}

		if (g_drm->fb[i].handle > 0) {
			/* delete dumb buffer */
			struct drm_mode_destroy_dumb dreq;
			memset(&dreq, 0, sizeof(dreq));
			dreq.handle = g_drm->fb[i].handle;
			if (0 != drmIoctl(g_drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq)) {
				xf86DrvMsg(-1, X_ERROR, "drmIoctl(DRM_IOCTL_MODE_DESTROY_DUMB) error !! handle=0x%08x, errno=%d\n", dreq.handle, errno);
			}
		}
	}

	if (g_drm->fd >= 0) {
		drmClose(g_drm->fd);
	}

	free((void*)g_drm);
	g_drm = NULL;
	
	xf86DrvMsg(-1, X_INFO, "StopFlipMode() success.\n");
}

/***********************************************************************
 * Shadow stuff
 ***********************************************************************/
void *
FlippedWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
		 CARD32 *size, void *closure)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	if (!g_drm)
		return NULL;

	if (!pScrn->vtSema)
		return NULL;

	FrameBufferPtr fb = g_drm->draw_fb;
	*size = fb->pitch;

	return ((CARD8 *)fb->map + row * fb->pitch + offset);
}

void
FlippedShadowUpdateAll(ScreenPtr pScreen, shadowBufPtr pBuf, void *pDst)
{
	PixmapPtr pShadow = pBuf->pPixmap;
	FbBits *shaBase;
	FbStride shaStride;
	int shaBpp;
	_X_UNUSED int shaXoff, shaYoff;

	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	fbGetDrawable(&pShadow->drawable, shaBase, shaStride, shaBpp, shaXoff, shaYoff);
//	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
//	"%s, src=%p, shaStride=%d, shaBpp=%d, pScrn->virtualY=%d, pDst=%p\n", __func__, shaBase, shaStride, shaBpp, pScrn->virtualY, pDst);
	const void* src = shaBase;
	size_t len = (shaStride * shaBpp / 8) * pScrn->virtualY;
	memcpy(pDst, (const void*)shaBase, len);
}

void
FlippedShadowUpdatePacked(ScreenPtr pScreen, shadowBufPtr pBuf)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	if (!g_drm)
		return;

	pthread_mutex_lock(&g_drm->mutex);
	FrameBufferPtr fb;

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s\n", __func__);
	/* First Draw after page flipping */
	if (g_drm->draw_fb == NULL) {
		fb = PopUnusedFrameBuffer(g_drm);
		if (!fb) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Error !! unused frame buffer is NOT exists.\n");
			pthread_mutex_unlock(&g_drm->mutex);
		} else {
			pthread_mutex_unlock(&g_drm->mutex);

			/* Full screen drawing */
			FlippedShadowUpdateAll(pScreen, pBuf, fb->map);

			pthread_mutex_lock(&g_drm->mutex);
			g_drm->draw_fb = fb;
			ShowFrameBuffer("Draw 1st after page flip", g_drm);
			pthread_cond_signal(&g_drm->cv);
			pthread_mutex_unlock(&g_drm->mutex);
		}
	/* 2nd, 3rd ... Draw */
	} else {
		fb = g_drm->draw_fb;
	
		/* Line drawing */
		shadowUpdatePacked(pScreen, pBuf);

		pthread_mutex_unlock(&g_drm->mutex);
	}
}
