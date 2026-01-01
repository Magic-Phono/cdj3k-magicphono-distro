#ifndef FLIP_CONTROL_H
#define FLIP_CONTROL_H

#include <X11/Xdefs.h> /* for Bool */
#include "xf86.h"
#include "shadow.h"

Bool	StartFlipMode(int width, int height, int depth, int bpp);
void	StopFlipMode(void);

/***********************************************************************
 * Shadow stuff
 ***********************************************************************/
void 	*FlippedWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
		 CARD32 *size, void *closure);
void	FlippedShadowUpdateAll(ScreenPtr pScreen, shadowBufPtr pBuf, void *pDst);
void	FlippedShadowUpdatePacked(ScreenPtr pScreen, shadowBufPtr pBuf);

#endif
