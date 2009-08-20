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

#include "glamo.h"
#include "glamo-dri2.h"
#include "glamo-kms-exa.h"


typedef struct {
	PixmapPtr pPixmap;
} GlamoDRI2BufferPrivateRec, *GlamoDRI2BufferPrivatePtr;


#ifdef USE_DRI2_1_1_0

static DRI2BufferPtr glamoCreateBuffer(DrawablePtr pDraw,
                                       unsigned int attachment,
                                       unsigned int format)
{
	DRI2BufferPtr buffer;
	return buffer;
}

#else

static DRI2BufferPtr glamoCreateBuffer(DrawablePtr pDraw,
                                       unsigned int *attachments, int count)
{
	ScreenPtr pScreen = pDraw->pScreen;
	DRI2BufferPtr buffers;
	int i;
	GlamoDRI2BufferPrivatePtr privates;
	PixmapPtr pPixmap, pDepthPixmap;

	buffers = xcalloc(count, sizeof *buffers);
	if ( buffers == NULL ) return NULL;
	privates = xcalloc(count, sizeof *privates);
	if ( privates == NULL ) {
		xfree(buffers);
		return NULL;
	}

	pDepthPixmap = NULL;
	/* For each attachment */
	for ( i=0; i<count; i++ ) {

		if ( attachments[i] == DRI2BufferFrontLeft ) {

			/* Front left buffer - just dig out the pixmap */
			if ( pDraw->type == DRAWABLE_PIXMAP ) {
				pPixmap = (PixmapPtr)pDraw;
			} else {
				pPixmap = (*pScreen->GetWindowPixmap)(
							(WindowPtr)pDraw);
			}
			pPixmap->refcnt++;

		} else {

			/* Anything else - create a new pixmap */
			pPixmap = (*pScreen->CreatePixmap)(pScreen,
			                                   pDraw->width,
			                                   pDraw->height,
			                                   pDraw->depth,
			                                   0);

		}

		if ( attachments[i] == DRI2BufferDepth ) pDepthPixmap = pPixmap;

		/* Set up the return data structure */
		buffers[i].attachment = attachments[i];
		buffers[i].pitch = pPixmap->devKind;
		buffers[i].cpp = pPixmap->drawable.bitsPerPixel / 8;
		buffers[i].driverPrivate = &privates[i];
		buffers[i].flags = 0;
		privates[i].pPixmap = pPixmap;

	}

	return buffers;
}

#endif

#ifdef USE_DRI2_1_1_0

static void glamoDestroyBuffer(DrawablePtr pDraw,
                               DRI2BufferPtr buffer)
{
	ScreenPtr pScreen = pDraw->pScreen;
	int i;
	GlamoDRI2BufferPrivatePtr private;

	private = buffer.driverPrivate;
	(*pScreen->DestroyPixmap)(private->pPixmap);

	if ( buffer ) {
		xfree(buffer.driverPrivate);
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
		(*pScreen->DestroyPixmap)(private->pPixmap);
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

	dri2info.version = 1;
	dri2info.fd = pGlamo->drm_fd;
	dri2info.deviceName = p;
	dri2info.driverName = "glamo";

#ifdef USE_DRI2_1_1_0
	dri2info.CreateBuffer = glamoCreateBuffer;
	dri2info.DestroyBuffer = glamoDestroyBuffer;
#else
	dri2info.CreateBuffers = glamoCreateBuffers;
	dri2info.DestroyBuffers = glamoDestroyBuffers;
#endif
	dri2info.CopyRegion = glamoCopyRegion;

	if ( !DRI2ScreenInit(pScreen, &dri2info) ) return;
}


void driCloseScreen(ScreenPtr pScreen)
{
	DRI2CloseScreen(pScreen);
}
