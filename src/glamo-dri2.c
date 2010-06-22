/*
 * DRI for the SMedia Glamo3362 X.org Driver
 *
 * Modified: 2009 by Thomas White <taw@bitwiz.org.uk>
 *
 * Based on dri2.c from xf86-video-modesetting, to which the following
 * notice applies:
 *
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
 *
 * Also based partially on xf86-video-ati, to which the following
 * notice applies:
 *
 * Copyright 2008 Kristian Høgsberg
 * Copyright 2008 Jérôme Glisse
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, VA LINUX SYSTEMS AND/OR
 * THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include <xf86_OSproc.h>
#include <xf86drm.h>
#include <dri2.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glamo_bo_gem.h>

#include "glamo.h"
#include "glamo-dri2.h"
#include "glamo-kms-exa.h"


struct glamo_dri2_buffer_priv {
	PixmapPtr pixmap;
	unsigned int attachment;
};


#if DRI2INFOREC_VERSION >= 3

static DRI2BufferPtr glamoCreateBuffer(DrawablePtr drawable,
                                       unsigned int attachment,
                                       unsigned int format)
{
	ScreenPtr pScreen = drawable->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	DRI2BufferPtr buffer;
	struct glamo_dri2_buffer_priv *private;
	PixmapPtr pixmap;
	struct glamo_exa_pixmap_priv *driver_priv;
	int r;

	buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	private = calloc(1, sizeof(*private));
	if (private == NULL) {
		free(buffer);
		return NULL;
	}

	if ( attachment == DRI2BufferFrontLeft ) {
		if ( drawable->type == DRAWABLE_PIXMAP ) {
			pixmap = (PixmapPtr)drawable;
		} else {
			pixmap = pScreen->GetWindowPixmap((WindowPtr)drawable);
		}
		pixmap->refcnt++;
	} else {
		pixmap = pScreen->CreatePixmap(pScreen,
		                           drawable->width,
		                           drawable->height,
		                           (format != 0)?format:drawable->depth,
		                           0);
	}
	exaMoveInPixmap(pixmap);
	driver_priv = exaGetPixmapDriverPrivate(pixmap);
	if ( !driver_priv ) {
		free(buffer);
		free(private);
		return NULL;
	}
	r = glamo_gem_name_buffer(driver_priv->bo, &buffer->name);
	if (r) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		           "Couldn't name buffer: %d %s\n",
		           r, strerror(r));
		free(buffer);
		free(private);
		return NULL;
	}
	buffer->attachment = attachment;
	buffer->pitch = pixmap->devKind;
	buffer->cpp = pixmap->drawable.bitsPerPixel / 8;
	buffer->driverPrivate = private;
	buffer->format = format;
	buffer->flags = 0;
	private->pixmap = pixmap;
	private->attachment = attachment;

	return buffer;
}

#else

static DRI2BufferPtr glamoCreateBuffers(DrawablePtr drawable,
                                        unsigned int *attachments, int count)
{
	ScreenPtr pScreen = drawable->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	int i;
	DRI2BufferPtr buffers;
	struct glamo_dri2_buffer_priv *privates;

	buffers = calloc(count, sizeof *buffers);
	if ( buffers == NULL ) return NULL;
	privates = calloc(count, sizeof *privates);
	if ( privates == NULL ) {
		free(buffers);
		return NULL;
	}

	/* For each attachment */
	for ( i=0; i<count; i++ ) {

		PixmapPtr pixmap;
		struct glamo_exa_pixmap_priv *driver_priv;
		int r;

		if ( attachments[i] == DRI2BufferFrontLeft ) {
			if ( drawable->type == DRAWABLE_PIXMAP ) {
				pixmap = (PixmapPtr)drawable;
			} else {
				pixmap = pScreen->GetWindowPixmap(
				                           (WindowPtr)drawable);
			}
			pixmap->refcnt++;
		} else {
			pixmap = pScreen->CreatePixmap(pScreen,
				           drawable->width,
				           drawable->height,
				           drawable->depth,
				           0);
		}
		exaMoveInPixmap(pixmap);
		driver_priv = exaGetPixmapDriverPrivate(pixmap);
		if ( !driver_priv ) {
			free(buffers);
			free(privates);
			return NULL;
		}
		r = glamo_gem_name_buffer(driver_priv->bo, &buffers[i].name);
		if (r) {
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			           "Couldn't name buffer: %d %s\n",
			           r, strerror(r));
			free(buffers);
			free(privates);
			return NULL;
		}
		buffers[i].attachment = attachments[i];
		buffers[i].pitch = pixmap->devKind;
		buffers[i].cpp = pixmap->drawable.bitsPerPixel / 8;
		buffers[i].driverPrivate = &privates[i];
		buffers[i].format = drawable->depth;
		buffers[i].flags = 0;
		privates[i].pixmap = pixmap;
		privates[i].attachment = attachments[i];

	}

	return buffers;
}

#endif

#if DRI2INFOREC_VERSION >= 3

static void glamoDestroyBuffer(DrawablePtr pDraw,
                               DRI2BufferPtr buffer)
{
	ScreenPtr pScreen = pDraw->pScreen;
	struct glamo_dri2_buffer_priv *private;

	private = buffer->driverPrivate;
	pScreen->DestroyPixmap(private->pixmap);

	if ( buffer ) {
		free(buffer->driverPrivate);
	}
}

#else

static void glamoDestroyBuffers(DrawablePtr pDraw,
                                DRI2BufferPtr buffers, int count)
{
	ScreenPtr pScreen = pDraw->pScreen;
	int i;

	for ( i=0; i<count; i++ ) {
		GlamoDRI2BufferPrivatePtr private;
		private = buffers[i].driverPrivate;
		pScreen->DestroyPixmap(private->pixmap);
	}

	if ( buffers ) {
		free(buffers[0].driverPrivate);
		free(buffers);
	}
}

#endif


static void glamoCopyRegion(DrawablePtr drawable, RegionPtr region,
                            DRI2BufferPtr dst_buffer, DRI2BufferPtr src_buffer)
{
	struct glamo_dri2_buffer_priv *src_private;
	struct glamo_dri2_buffer_priv *dst_private;
	ScreenPtr pScreen = drawable->pScreen;
	PixmapPtr src_pixmap;
	PixmapPtr dst_pixmap;
	RegionPtr copy_clip;
	GCPtr gc;

	src_private = src_buffer->driverPrivate;
	dst_private = dst_buffer->driverPrivate;
	src_pixmap = src_private->pixmap;
	dst_pixmap = dst_private->pixmap;

	if (src_private->attachment == DRI2BufferFrontLeft) {
		src_pixmap = (PixmapPtr)drawable;
	}
	if (dst_private->attachment == DRI2BufferFrontLeft) {
		dst_pixmap = (PixmapPtr)drawable;
	}

	gc = GetScratchGC(drawable->depth, pScreen);
	copy_clip = REGION_CREATE(pScreen, NULL, 0);
	REGION_COPY(pScreen, copy_clip, region);
	gc->funcs->ChangeClip(gc, CT_REGION, copy_clip, 0);
	ValidateGC(&dst_pixmap->drawable, gc);
	gc->ops->CopyArea(&src_pixmap->drawable, &dst_pixmap->drawable, gc,
		         0, 0, drawable->width, drawable->height, 0, 0);
	FreeScratchGC(gc);
}


void driScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	DRI2InfoRec dri2info;
	char *p;
	struct stat sbuf;
	dev_t d;
	int i;

	fstat(pGlamo->drm_fd, &sbuf);
	d = sbuf.st_rdev;
	p = pGlamo->drm_devname;
	for ( i=0; i<DRM_MAX_MINOR; i++ ) {
		sprintf(p, DRM_DEV_NAME, DRM_DIR_NAME, i);
		if ( stat(p, &sbuf) == 0 && sbuf.st_rdev == d ) break;
	}
	if ( i == DRM_MAX_MINOR ) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "[glamo-dri] Failed to find name of DRM device\n");
		return;
	}
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "[glamo-dri] Name of DRM device is '%s'\n", p);

#if DRI2INFOREC_VERSION >= 3
	dri2info.version = 3;
#else
	dri2info.version = 2;
#endif
	dri2info.fd = pGlamo->drm_fd;
	dri2info.deviceName = p;
	dri2info.driverName = "glamo";

#if DRI2INFOREC_VERSION >= 3
	dri2info.CreateBuffer = glamoCreateBuffer;
	dri2info.DestroyBuffer = glamoDestroyBuffer;
#else
	dri2info.CreateBuffers = glamoCreateBuffers;
	dri2info.DestroyBuffers = glamoDestroyBuffers;
#endif
	dri2info.CopyRegion = glamoCopyRegion;

	if ( !DRI2ScreenInit(pScreen, &dri2info) ) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		           "[glamo-dri] DRI2 initialisation failed\n");
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		           "[glamo-dri] DRI2 initialisation succeeded\n");
	}
}


void driCloseScreen(ScreenPtr pScreen)
{
	DRI2CloseScreen(pScreen);
}
