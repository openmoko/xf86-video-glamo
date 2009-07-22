/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 * Author: Alan Hourihane <alanh@tungstengraphics.com>
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"

#include "driver.h"

#include "dri2.h"

extern unsigned int
driGetPixmapHandle(PixmapPtr pPixmap, unsigned int *flags);

void
driLock(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    if (!pGlamo->lock_held)
	DRM_LOCK(pGlamo->drm_fd, pGlamo->lock, pGlamo->context, 0);

    pGlamo->lock_held = 1;
}

void
driUnlock(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    if (pGlamo->lock_held)
	DRM_UNLOCK(pGlamo->drm_fd, pGlamo->lock, pGlamo->context);

    pGlamo->lock_held = 0;
}

static void
driBeginClipNotify(ScreenPtr pScreen)
{
    driLock(pScreen);
}

static void
driEndClipNotify(ScreenPtr pScreen)
{
    driUnlock(pScreen);
}

struct __DRILock
{
    unsigned int block_header;
    drm_hw_lock_t lock;
    unsigned int next_id;
};

#define DRI2_SAREA_BLOCK_HEADER(type, size) (((type) << 16) | (size))
#define DRI2_SAREA_BLOCK_LOCK		0x0001

void
driScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    DRI2InfoRec dri2info;
    const char *driverName;
    unsigned int sarea_handle;
    struct __DRILock *DRILock;
    void *p;

    dri2info.version = 1;
    dri2info.fd = pGlamo->drm_fd;
    dri2info.driverSareaSize = sizeof(struct __DRILock);
    dri2info.driverName = "i915";      /* FIXME */
    dri2info.getPixmapHandle = driGetPixmapHandle;
    dri2info.beginClipNotify = driBeginClipNotify;
    dri2info.endClipNotify = driEndClipNotify;

    p = DRI2ScreenInit(pScreen, &dri2info);
    if (!p)
	return;

    DRILock = p;
    DRILock->block_header =
	DRI2_SAREA_BLOCK_HEADER(DRI2_SAREA_BLOCK_LOCK, sizeof *DRILock);
    pGlamo->lock = &DRILock->lock;
    pGlamo->context = 1;
    DRILock->next_id = 2;
    driLock(pScreen);

    DRI2Connect(pScreen, &pGlamo->drm_fd, &driverName, &sarea_handle);
}

void
driCloseScreen(ScreenPtr pScreen)
{
    driUnlock(pScreen);
    DRI2CloseScreen(pScreen);
}
