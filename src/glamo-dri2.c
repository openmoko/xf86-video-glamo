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
	DRI2BufferPtr buffer;
	struct glamo_dri2_buffer_priv *private;
	PixmapPtr pixmap;
	struct glamo_exa_pixmap_priv *driver_priv;
	int r;

	buffer = xcalloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	private = xcalloc(1, sizeof(*private));
	if (private == NULL) {
		xfree(buffer);
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
		xfree(buffer);
		xfree(private);
		return NULL;
	}
	r = glamo_gem_name_buffer(driver_priv->bo, &buffer->name);
	if (r) {
		fprintf(stderr, "Couldn't name buffer: %d %s\n",
		        r, strerror(r));
		xfree(buffer);
		xfree(private);
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
	int i;
	DRI2BufferPtr buffers;
	struct glamo_dri2_buffer_priv *privates;

	buffers = xcalloc(count, sizeof *buffers);
	if ( buffers == NULL ) return NULL;
	privates = xcalloc(count, sizeof *privates);
	if ( privates == NULL ) {
		xfree(buffers);
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
			xfree(buffers);
			xfree(privates);
			return NULL;
		}
		r = glamo_gem_name_buffer(driver_priv->bo, &buffers[i].name);
		if (r) {
			fprintf(stderr, "Couldn't name buffer: %d %s\n",
				r, strerror(r));
			xfree(buffers);
			xfree(privates);
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
		xfree(buffer->driverPrivate);
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
		xfree(buffers[0].driverPrivate);
		xfree(buffers);
	}
}

#endif


static void glamoCopyRegion(DrawablePtr pDraw, RegionPtr pRegion,
                            DRI2BufferPtr pDestBuffer, DRI2BufferPtr pSrcBuffer)
{
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

	dri2info.version = DRI2INFOREC_VERSION;
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
