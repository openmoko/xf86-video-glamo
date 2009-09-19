/*
 * EXA via DRI for the SMedia Glamo3362 X.org Driver
 *
 * Copyright 2009 Thomas White <taw@bitwiz.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 *
 * The contents of this file are based on glamo-draw.c, to which the following
 * notice applies:
 *
 * Copyright  2007 OpenMoko, Inc.
 *
 * This driver is based on Xati,
 * Copyright  2003 Eric Anholt
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "glamo-log.h"
#include "glamo.h"
#include "glamo-regs.h"
#include "glamo-kms-exa.h"
#include "glamo-drm.h"

#include <drm/glamo_drm.h>
#include <drm/glamo_bo.h>
#include <drm/glamo_bo_gem.h>
#include <xf86drm.h>


#if GLAMO_TRACE_FALL
	#define GLAMO_FALLBACK(x)              \
	do {                                   \
		ErrorF("%s: ", __FUNCTION__);  \
		ErrorF x;                      \
		return FALSE;                  \
	} while (0)
#else
	#define GLAMO_FALLBACK(x) return FALSE
#endif


static const CARD8 GLAMOSolidRop[16] = {
    /* GXclear      */      0x00,         /* 0 */
    /* GXand        */      0xa0,         /* src AND dst */
    /* GXandReverse */      0x50,         /* src AND NOT dst */
    /* GXcopy       */      0xf0,         /* src */
    /* GXandInverted*/      0x0a,         /* NOT src AND dst */
    /* GXnoop       */      0xaa,         /* dst */
    /* GXxor        */      0x5a,         /* src XOR dst */
    /* GXor         */      0xfa,         /* src OR dst */
    /* GXnor        */      0x05,         /* NOT src AND NOT dst */
    /* GXequiv      */      0xa5,         /* NOT src XOR dst */
    /* GXinvert     */      0x55,         /* NOT dst */
    /* GXorReverse  */      0xf5,         /* src OR NOT dst */
    /* GXcopyInverted*/     0x0f,         /* NOT src */
    /* GXorInverted */      0xaf,         /* NOT src OR dst */
    /* GXnand       */      0x5f,         /* NOT src OR NOT dst */
    /* GXset        */      0xff,         /* 1 */
};


static const CARD8 GLAMOBltRop[16] = {
    /* GXclear      */      0x00,         /* 0 */
    /* GXand        */      0x88,         /* src AND dst */
    /* GXandReverse */      0x44,         /* src AND NOT dst */
    /* GXcopy       */      0xcc,         /* src */
    /* GXandInverted*/      0x22,         /* NOT src AND dst */
    /* GXnoop       */      0xaa,         /* dst */
    /* GXxor        */      0x66,         /* src XOR dst */
    /* GXor         */      0xee,         /* src OR dst */
    /* GXnor        */      0x11,         /* NOT src AND NOT dst */
    /* GXequiv      */      0x99,         /* NOT src XOR dst */
    /* GXinvert     */      0x55,         /* NOT dst */
    /* GXorReverse  */      0xdd,         /* src OR NOT dst */
    /* GXcopyInverted*/     0x33,         /* NOT src */
    /* GXorInverted */      0xbb,         /* NOT src OR dst */
    /* GXnand       */      0x77,         /* NOT src OR NOT dst */
    /* GXset        */      0xff,         /* 1 */
};


unsigned int driGetPixmapHandle(PixmapPtr pPixmap, unsigned int *flags)
{
	struct glamo_exa_pixmap_priv *priv;

	priv = exaGetPixmapDriverPrivate(pPixmap);
	if (!priv) {
		FatalError("NO PIXMAP PRIVATE\n");
		return 0;
	}

	return priv->bo->handle;
}


static Bool GlamoKMSExaPrepareSolid(PixmapPtr pPix, int alu, Pixel pm, Pixel fg)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	CARD16 op, pitch;
	FbBits mask;
	struct glamo_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);

	if (pPix->drawable.bitsPerPixel != 16) {
		GLAMO_FALLBACK(("Only 16bpp is supported\n"));
	}

	mask = FbFullMask(16);
	if ((pm & mask) != mask) {
		GLAMO_FALLBACK(("Can't do planemask 0x%08x\n",
		               (unsigned int)pm));
	}

	op = GLAMOSolidRop[alu] << 8;
	pitch = pPix->devKind;

	GlamoDRMAddCommandBO(pGlamo, GLAMO_REG_2D_DST_ADDRL, priv->bo);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_PITCH, pitch & 0x7ff);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_HEIGHT, pPix->drawable.height);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_PAT_FG, fg);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_COMMAND2, op);

	return TRUE;
}


static void GlamoKMSExaSolid(PixmapPtr pPix, int x1, int y1, int x2, int y2)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_X, x1);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_Y, y1);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_RECT_WIDTH, x2 - x1);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_RECT_HEIGHT, y2 - y1);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_COMMAND3, 0);
}


static void GlamoKMSExaDoneSolid(PixmapPtr pPix)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	GlamoDRMDispatch(pGlamo);
	exaMarkSync(pGlamo->pScreen);
}


static Bool GlamoKMSExaPrepareCopy(PixmapPtr pSrc, PixmapPtr pDst, int dx, int dy,
                                   int alu, Pixel pm)
{
	ScrnInfoPtr pScrn = xf86Screens[pSrc->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	FbBits mask;
	CARD16 src_pitch, dst_pitch;
	CARD16 op;
	struct glamo_exa_pixmap_priv *priv_src;
	struct glamo_exa_pixmap_priv *priv_dst;

	priv_src = exaGetPixmapDriverPrivate(pSrc);
	priv_dst = exaGetPixmapDriverPrivate(pDst);

	if (pSrc->drawable.bitsPerPixel != 16 ||
	    pDst->drawable.bitsPerPixel != 16) {
		GLAMO_FALLBACK(("Only 16bpp is supported"));
	}

	mask = FbFullMask(16);
	if ((pm & mask) != mask) {
		GLAMO_FALLBACK(("Can't do planemask 0x%08x",
				(unsigned int) pm));
	}

	src_pitch = pSrc->devKind;
	dst_pitch = pDst->devKind;
	op = GLAMOBltRop[alu] << 8;

	GlamoDRMAddCommandBO(pGlamo, GLAMO_REG_2D_SRC_ADDRL, priv_src->bo);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_SRC_PITCH, src_pitch & 0x7ff);

	GlamoDRMAddCommandBO(pGlamo, GLAMO_REG_2D_DST_ADDRL, priv_dst->bo);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_PITCH, dst_pitch & 0x7ff);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_HEIGHT, pDst->drawable.height);

	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_COMMAND2, op);

	return TRUE;
}


static void GlamoKMSExaCopy(PixmapPtr pDst, int srcX, int srcY,
                            int dstX, int dstY, int width, int height)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_SRC_X, srcX);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_SRC_Y, srcY);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_X, dstX);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_Y, dstY);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_RECT_WIDTH, width);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_RECT_HEIGHT, height);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_COMMAND3, 0);
}


static void GlamoKMSExaDoneCopy(PixmapPtr pDst)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	GlamoDRMDispatch(pGlamo);
	exaMarkSync(pGlamo->pScreen);
}


/* Generate an integer token which can be used for synchronisation later.
 * We do this by putting the most recently used buffer object into a list,
 * and returning the index into that list.
 * To make things a little more exciting, the list is a ring buffer. */
static int GlamoKMSExaMarkSync(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	unsigned int idx;

	idx = pGlamo->exa_marker_index;
	pGlamo->exa_buffer_markers[idx] = pGlamo->last_buffer_object;

	pGlamo->exa_marker_index = (idx+1) % NUM_EXA_BUFFER_MARKERS;

	return 1;
}


static void GlamoKMSExaWaitMarker(ScreenPtr pScreen, int idx)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	struct glamo_bo *bo;

	bo = pGlamo->exa_buffer_markers[idx];

	if ( bo ) {
		glamo_bo_wait(bo);
	} else {

		struct drm_glamo_gem_wait_rendering args;

		args.handle = 0;
		args.have_handle = 0;
		drmCommandWriteRead(pGlamo->bufmgr->fd,
				    DRM_GLAMO_GEM_WAIT_RENDERING,
				    &args, sizeof(args));
	}
}


static void *GlamoKMSExaCreatePixmap(ScreenPtr screen, int size, int align)
{
	ScrnInfoPtr pScrn = xf86Screens[screen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	struct glamo_exa_pixmap_priv *new_priv;

	new_priv = xcalloc(1, sizeof(struct glamo_exa_pixmap_priv));
	if (!new_priv)
		return NULL;

	/* See GlamoKMSExaModifyPixmapHeader() below */
	if (size == 0)
		return new_priv;

	/* Dive into the kernel (via libdrm) to allocate some VRAM */
	new_priv->bo = glamo_bo_open(pGlamo->bufmgr, 0, size, align,
	                             GLAMO_GEM_DOMAIN_VRAM, 0);
	if (!new_priv->bo) {
		xfree(new_priv);
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		           "Failed to create pixmap\n");
		return NULL;
	}

	return new_priv;
}


static void GlamoKMSExaDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	struct glamo_exa_pixmap_priv *driver_priv = driverPriv;
	int i;

	/* We're about to (probably) delete a buffer object, so zip through
	 * the list of EXA wait markers and delete any references. */
	for ( i=0; i<NUM_EXA_BUFFER_MARKERS; i++ ) {
		if ( pGlamo->exa_buffer_markers[i] == driver_priv->bo ) {
			pGlamo->exa_buffer_markers[i] = NULL;
		}
	}
	if ( pGlamo->last_buffer_object == driver_priv->bo ) {
		pGlamo->last_buffer_object = NULL;
	}

	if (driver_priv->bo)
		glamo_bo_unref(driver_priv->bo);

	xfree(driver_priv);
}


static Bool GlamoKMSExaPixmapIsOffscreen(PixmapPtr pPix)
{
	struct glamo_exa_pixmap_priv *driver_priv;

	driver_priv = exaGetPixmapDriverPrivate(pPix);
	if (driver_priv && driver_priv->bo)
		return TRUE;

	return FALSE;
}


static Bool GlamoKMSExaPrepareAccess(PixmapPtr pPix, int index)
{
	ScreenPtr screen = pPix->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[screen->myNum];
	struct glamo_exa_pixmap_priv *driver_priv;

	driver_priv = exaGetPixmapDriverPrivate(pPix);
	if (!driver_priv) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				"%s: no driver private?\n", __FUNCTION__);
		return FALSE;
	}

	if (!driver_priv->bo) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				"%s: no buffer object?\n", __FUNCTION__);
		return TRUE;
	}

	/* Return as quickly as possible if we have a mapping already */
	if ( driver_priv->bo->virtual ) {
		pPix->devPrivate.ptr = driver_priv->bo->virtual;
		glamo_bo_wait(driver_priv->bo);
		return TRUE;
	}

	if ( glamo_bo_map(driver_priv->bo, 1) ) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				"%s: bo map failed\n", __FUNCTION__);
		return FALSE;
	}
	pPix->devPrivate.ptr = driver_priv->bo->virtual;
	glamo_bo_wait(driver_priv->bo);

	return TRUE;
}


static void GlamoKMSExaFinishAccess(PixmapPtr pPix, int index)
{
	/* Leave the mapping intact for fast restoration of access later */
	pPix->devPrivate.ptr = NULL;
}


/* This essentially does the job of ModifyPixmapHeader, for the occasions
 * when we need to update the properties of the screen pixmap. */
Bool GlamoKMSExaMakeFullyFledged(PixmapPtr pPix, int width, int height,
                                 int depth, int bitsPerPixel, int devKind)
{
	ScreenPtr screen = pPix->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[screen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	struct glamo_exa_pixmap_priv *priv;
	int new_size;

	if (depth <= 0) depth = pPix->drawable.depth;
	if (bitsPerPixel <= 0) bitsPerPixel = pPix->drawable.bitsPerPixel;
	if (width <= 0) width = pPix->drawable.width;
	if (height <= 0) height = pPix->drawable.height;
	if (width <= 0 || height <= 0 || depth <= 0) return FALSE;

	miModifyPixmapHeader(pPix, width, height, depth,
	                     bitsPerPixel, devKind, NULL);

	priv = exaGetPixmapDriverPrivate(pPix);
	if (!priv) {
		/* This should never, ever, happen */
		FatalError("Fledgeling pixmap had no driver private!\n");
		return FALSE;
	}

	new_size = (width * height * depth) / 8;
	if ( new_size == 0 ) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "Fledgeling pixmap would still have zero size!"
		           " %ix%i %i bpp depth=%i\n", width, height,
		           bitsPerPixel, depth);
		new_size = 1;
	}

	if ( priv->bo == NULL ) {

		/* This pixmap has no associated buffer object.
		 * It's time to create one */
		priv->bo = glamo_bo_open(pGlamo->bufmgr, 0, new_size, 2,
		                         GLAMO_GEM_DOMAIN_VRAM, 0);
		if ( priv->bo == NULL ) {
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				   "Couldn't create buffer object for"
				   " fledgeling pixmap!\n");
			return FALSE;
		}

	} else {

		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		           "Fledgeling pixmap already had a buffer object!\n");

	}

	return TRUE;
}


Bool GlamoKMSExaCheckComposite(int op,
                               PicturePtr pSrcPicture,
                               PicturePtr pMaskPicture,
                               PicturePtr pDstPicture)
{
	return FALSE;
}


Bool GlamoKMSExaPrepareComposite(int op,
                                 PicturePtr pSrcPicture,
                                 PicturePtr pMaskPicture,
                                 PicturePtr pDstPicture,
                                 PixmapPtr pSrc,
                                 PixmapPtr pMask,
                                 PixmapPtr pDst)
{
	return FALSE;
}


void GlamoKMSExaComposite(PixmapPtr pDst,
                          int srcX,
                          int srcY,
                          int maskX,
                          int maskY,
                          int dstX,
                          int dstY,
                          int width,
                          int height)
{
}


void GlamoKMSExaDoneComposite(PixmapPtr pDst)
{
}


void GlamoKMSExaClose(ScrnInfoPtr pScrn)
{
	exaDriverFini(pScrn->pScreen);
}


void GlamoKMSExaInit(ScrnInfoPtr pScrn)
{
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	Bool success = FALSE;
	ExaDriverPtr exa;
	int i;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "EXA hardware acceleration initialising\n");

	exa = exaDriverAlloc();
	if ( !exa ) return;
	pGlamo->exa = exa;

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;
	exa->memoryBase = 0;
	exa->memorySize = 0;
	exa->offScreenBase = 0;
	exa->pixmapOffsetAlign = 2;
	exa->pixmapPitchAlign = 2;
	exa->maxX = 640;
	exa->maxY = 640;

	/* Solid fills */
	exa->PrepareSolid = GlamoKMSExaPrepareSolid;
	exa->Solid = GlamoKMSExaSolid;
	exa->DoneSolid = GlamoKMSExaDoneSolid;

	/* Blits */
	exa->PrepareCopy = GlamoKMSExaPrepareCopy;
	exa->Copy = GlamoKMSExaCopy;
	exa->DoneCopy = GlamoKMSExaDoneCopy;

	/* Composite (though these just cause fallback) */
	exa->CheckComposite = GlamoKMSExaCheckComposite;
	exa->PrepareComposite = GlamoKMSExaPrepareComposite;
	exa->Composite = GlamoKMSExaComposite;
	exa->DoneComposite = GlamoKMSExaDoneComposite;

	exa->DownloadFromScreen = NULL;
	exa->UploadToScreen = NULL;
	exa->UploadToScratch = NULL;

	exa->MarkSync = GlamoKMSExaMarkSync;
	exa->WaitMarker = GlamoKMSExaWaitMarker;

	/* Prepare temporary buffers */
	GlamoDRMInit(pGlamo);
	pGlamo->last_buffer_object = NULL;
	for ( i=0; i<NUM_EXA_BUFFER_MARKERS; i++ ) {
		pGlamo->exa_buffer_markers[i] = NULL;
	}
	pGlamo->exa_marker_index = 0;
	if ( !pGlamo->cmdq_drm ) return;

	/* Tell EXA that we're going to take care of memory
	 * management ourselves. */
	exa->flags = EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS;
#ifdef EXA_MIXED_PIXMAPS
	exa->flags |= EXA_MIXED_PIXMAPS;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using mixed mode pixmaps\n");
#endif
	exa->PrepareAccess = GlamoKMSExaPrepareAccess;
	exa->FinishAccess = GlamoKMSExaFinishAccess;
	exa->CreatePixmap = GlamoKMSExaCreatePixmap;
	exa->DestroyPixmap = GlamoKMSExaDestroyPixmap;
	exa->PixmapIsOffscreen = GlamoKMSExaPixmapIsOffscreen;
	exa->ModifyPixmapHeader = NULL;

	/* Hook up with libdrm */
	pGlamo->bufmgr = glamo_bo_manager_gem_ctor(pGlamo->drm_fd);

	success = exaDriverInit(pScrn->pScreen, exa);
	if (success) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"Initialized EXA acceleration\n");
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			"Failed to initialize EXA acceleration\n");
		xfree(pGlamo->exa);
		pGlamo->exa = NULL;
	}
}
