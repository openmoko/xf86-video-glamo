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


struct glamo_exa_pixmap_priv {
	struct glamo_bo *bo;
};


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


/* Submit the prepared command sequence to the kernel */
static void GlamoDRMDispatch(GlamoPtr pGlamo)
{
	drm_glamo_cmd_buffer_t cmdbuf;
	int r;

	cmdbuf.buf = (char *)pGlamo->cmdq_drm;
	cmdbuf.bufsz = pGlamo->cmdq_drm_used * 2;	/* -> bytes */
	cmdbuf.nobjs = pGlamo->cmdq_obj_used;
	cmdbuf.objs = pGlamo->cmdq_objs;
	cmdbuf.obj_pos = pGlamo->cmdq_obj_pos;

	r = drmCommandWrite(pGlamo->drm_fd, DRM_GLAMO_CMDBUF,
	                    &cmdbuf, sizeof(cmdbuf));
	if ( r != 0 ) {
		xf86DrvMsg(pGlamo->pScreen->myNum, X_ERROR,
		           "DRM_GLAMO_CMDBUF failed\n");
	}

	/* Reset counts to zero for the next sequence */
	pGlamo->cmdq_obj_used = 0;
	pGlamo->cmdq_drm_used = 0;
}


static inline void GlamoDRMAddCommand(GlamoPtr pGlamo, uint16_t reg,
                                      uint16_t val)
{
	if ( pGlamo->cmdq_drm_used == pGlamo->cmdq_drm_size ) {
		xf86DrvMsg(pGlamo->pScreen->myNum, X_INFO,
		           "Forced command cache flush.\n");
		GlamoDRMDispatch(pGlamo);
	}

	/* Record command */
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = reg;
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = val;
}


static inline void GlamoDRMAddCommandBO(GlamoPtr pGlamo, uint16_t reg,
                                        struct glamo_bo *bo)
{
	if ( pGlamo->cmdq_drm_used == pGlamo->cmdq_drm_size ) {
		xf86DrvMsg(pGlamo->pScreen->myNum, X_INFO,
		           "Forced command cache flush.\n");
		GlamoDRMDispatch(pGlamo);
	}

	/* Record object position */
	pGlamo->cmdq_objs[pGlamo->cmdq_obj_used] = bo->handle;
	/* -> bytes */
	pGlamo->cmdq_obj_pos[pGlamo->cmdq_obj_used] = pGlamo->cmdq_drm_used * 2;
	pGlamo->cmdq_obj_used++;

	/* Record command */
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = reg;
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = 0x0000;
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = reg+2;
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = 0x0000;
}


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
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_ID1, 0);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_ID2, 0);

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
	CARD32 src_offset, dst_offset;
	CARD16 src_pitch, dst_pitch;
	CARD16 op;
	struct glamo_exa_pixmap_priv *priv_src;
	struct glamo_exa_pixmap_priv *priv_dst;

	priv_src = exaGetPixmapDriverPrivate(pSrc);
	priv_dst = exaGetPixmapDriverPrivate(pDst);

	if (pSrc->drawable.bitsPerPixel != 16 ||
	    pDst->drawable.bitsPerPixel != 16)
		GLAMO_FALLBACK(("Only 16bpp is supported"));

	mask = FbFullMask(16);
	if ((pm & mask) != mask) {
		GLAMO_FALLBACK(("Can't do planemask 0x%08x",
				(unsigned int) pm));
	}

	src_offset = exaGetPixmapOffset(pSrc);
	src_pitch = pSrc->devKind;

	dst_offset = exaGetPixmapOffset(pDst);
	dst_pitch = pDst->devKind;

	op = GLAMOBltRop[alu] << 8;

	GlamoDRMAddCommandBO(pGlamo, GLAMO_REG_2D_SRC_ADDRL, priv_src->bo);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_SRC_PITCH, src_pitch & 0x7ff);

	GlamoDRMAddCommandBO(pGlamo, GLAMO_REG_2D_DST_ADDRL, priv_dst->bo);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_PITCH, dst_pitch & 0x7ff);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_DST_HEIGHT, pDst->drawable.height);

	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_COMMAND2, op);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_ID1, 0);
	GlamoDRMAddCommand(pGlamo, GLAMO_REG_2D_ID2, 0);

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


static int GlamoKMSExaMarkSync(ScreenPtr pScreen)
{
	return 1;
}


static void GlamoKMSExaWaitMarker(ScreenPtr pScreen, int marker)
{
//	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
//	GlamoPtr pGlamo = GlamoPTR(pScrn);

//	GLAMOEngineWait(pGlamo, GLAMO_ENGINE_ALL);
	// FIXME
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


static void GlamoKMSExaDestroyPixmap(ScreenPtr screen, void *driverPriv)
{
	struct glamo_exa_pixmap_priv *driver_priv = driverPriv;

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

	if ( glamo_bo_map(driver_priv->bo, 1) ) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				"%s: bo map failed\n", __FUNCTION__);
		return FALSE;
	}
	pPix->devPrivate.ptr = driver_priv->bo->virtual;

	return TRUE;
}


static void GlamoKMSExaFinishAccess(PixmapPtr pPix, int index)
{
	ScreenPtr screen = pPix->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[screen->myNum];
	struct glamo_exa_pixmap_priv *driver_priv;

	driver_priv = exaGetPixmapDriverPrivate(pPix);
	if (!driver_priv) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				"%s: no driver private?\n", __FUNCTION__);
		return;
	}

	if (!driver_priv->bo) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				"%s: no buffer object?\n", __FUNCTION__);
		return;
	}

	glamo_bo_unmap(driver_priv->bo);
	pPix->devPrivate.ptr = NULL;
}


static Bool GlamoKMSExaModifyPixmapHeader(PixmapPtr pPix, int width, int height,
                                          int depth, int bitsPerPixel,
                                          int devKind, pointer pPixData)
{
	ScreenPtr screen = pPix->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[screen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	struct glamo_exa_pixmap_priv *priv;

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
		FatalError("NO PIXMAP PRIVATE\n");
		return FALSE;
	}

	if ( priv->bo == NULL ) {

		int size;

		/* This pixmap has no associated buffer object.
		 * It's time to create one */
		size = (width * height * depth) / 8;
		if ( size == 0 ) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			           "Zero-sized pixmap in ModifyPixmapHeader\n");
		}
		priv->bo = glamo_bo_open(pGlamo->bufmgr, 0, size, 2,
		                         GLAMO_GEM_DOMAIN_VRAM, 0);

		if ( priv->bo == NULL ) {
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
				   "Failed to create buffer object"
				   " in ModifyPixmapHeader.\n");
			return FALSE;
		}

	}

	return FALSE;
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

	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
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
	exa->CheckComposite = NULL;//GlamoKMSExaCheckComposite;
	exa->PrepareComposite = NULL;//GlamoKMSExaPrepareComposite;
	exa->Composite = NULL;//GlamoKMSExaComposite;
	exa->DoneComposite = NULL;//GlamoKMSExaDoneComposite;

	exa->DownloadFromScreen = NULL;
	exa->UploadToScreen = NULL;
	exa->UploadToScratch = NULL;

	exa->MarkSync = GlamoKMSExaMarkSync;
	exa->WaitMarker = GlamoKMSExaWaitMarker;

	/* Prepare temporary buffers */
	pGlamo->cmdq_objs = malloc(1024);
	pGlamo->cmdq_obj_pos = malloc(1024);
	pGlamo->cmdq_obj_used = 0;
	pGlamo->cmdq_drm_used = 0;
	pGlamo->cmdq_drm_size = 4 * 1024;
	pGlamo->cmdq_drm = malloc(pGlamo->cmdq_drm_size);
	if ( !pGlamo->cmdq_drm ) return;

	/* Tell EXA that we're going to take care of memory
	 * management ourselves. */
	exa->flags = EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS;
	exa->PrepareAccess = GlamoKMSExaPrepareAccess;
	exa->FinishAccess = GlamoKMSExaFinishAccess;
	exa->CreatePixmap = GlamoKMSExaCreatePixmap;
	exa->DestroyPixmap = GlamoKMSExaDestroyPixmap;
	exa->PixmapIsOffscreen = GlamoKMSExaPixmapIsOffscreen;
	exa->ModifyPixmapHeader = GlamoKMSExaModifyPixmapHeader;

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
