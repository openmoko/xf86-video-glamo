/*
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
#include "glamo-cmdq.h"
#include "glamo-draw.h"
#include "glamo-engine.h"

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

/********************************
 * exa entry points declarations
 ********************************/

static Bool
GLAMODrawExaInit(ScrnInfoPtr pScrn, size_t mem_start, size_t mem_size);

Bool
GLAMOExaPrepareSolid(PixmapPtr      pPixmap,
		     int            alu,
		     Pixel          planemask,
		     Pixel          fg);

void
GLAMOExaSolid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2);

void
GLAMOExaDoneSolid(PixmapPtr pPixmap);

void
GLAMOExaCopy(PixmapPtr pDstPixmap,
	     int    srcX,
	     int    srcY,
	     int    dstX,
	     int    dstY,
	     int    width,
	     int    height);

void
GLAMOExaDoneCopy(PixmapPtr pDstPixmap);

Bool
GLAMOExaCheckComposite(int op,
	       PicturePtr   pSrcPicture,
	       PicturePtr   pMaskPicture,
	       PicturePtr   pDstPicture);


Bool
GLAMOExaPrepareComposite(int                op,
			 PicturePtr         pSrcPicture,
			 PicturePtr         pMaskPicture,
			 PicturePtr         pDstPicture,
			 PixmapPtr          pSrc,
			 PixmapPtr          pMask,
			 PixmapPtr          pDst);

void
GLAMOExaComposite(PixmapPtr pDst,
		 int srcX,
		 int srcY,
		 int maskX,
		 int maskY,
		 int dstX,
		 int dstY,
		 int width,
		 int height);

Bool
GLAMOExaPrepareCopy(PixmapPtr       pSrcPixmap,
		    PixmapPtr       pDstPixmap,
		    int             dx,
		    int             dy,
		    int             alu,
		    Pixel           planemask);

void
GLAMOExaDoneComposite(PixmapPtr pDst);


Bool
GLAMOExaUploadToScreen(PixmapPtr pDst,
		       int x,
		       int y,
		       int w,
		       int h,
		       char *src,
		       int src_pitch);
Bool
GLAMOExaDownloadFromScreen(PixmapPtr pSrc,
			   int x,  int y,
			   int w,  int h,
			   char *dst,
			   int dst_pitch);

void
GLAMOExaWaitMarker (ScreenPtr pScreen, int marker);

static void
GLAMOBlockHandler(pointer blockData, OSTimePtr timeout, pointer readmask)
{
	ScreenPtr pScreen = (ScreenPtr) blockData;

	exaWaitSync(pScreen);
}

static void
GLAMOWakeupHandler(pointer blockData, int result, pointer readmask)
{
}

size_t
GLAMODrawInit(ScrnInfoPtr pScrn, size_t mem_start, size_t mem_size)
{
    size_t mem_used;

    mem_used = GLAMOCMDQInit(pScrn, mem_start, mem_size);

	if (!mem_used) {
        GLAMODrawFini(pScrn);
        return FALSE;
    }

    if (!GLAMODrawExaInit(pScrn, mem_start + mem_used, mem_size - mem_used))
        return 0;

    return mem_size;
}

void
GLAMODrawFini(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    GLAMOCMDQFini(pScrn);
    if (pGlamo->exa) {
        exaDriverFini(pGlamo->pScreen);
        free(pGlamo->exa);
        pGlamo->exa = NULL;
    }
}

Bool
GLAMODrawEnable(ScrnInfoPtr pScrn)
{
    GlamoPtr pGlamo = GlamoPTR(pScrn);

	GLAMOEngineEnable(pGlamo, GLAMO_ENGINE_2D);
	GLAMOEngineReset(pGlamo, GLAMO_ENGINE_2D);
    if (!GLAMOCMDQEnable(pScrn)) {
        GLAMODrawDisable(pScrn);
        return FALSE;
    }
	GLAMOEngineWait(pGlamo, GLAMO_ENGINE_ALL);

    return TRUE;
}

void
GLAMODrawDisable(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    GLAMOCMDQDisable(pScrn);
	GLAMOEngineDisable(pGlamo, GLAMO_ENGINE_2D);
}

static Bool
GLAMODrawExaInit(ScrnInfoPtr pScrn, size_t mem_start, size_t mem_size)
{
    GlamoPtr pGlamo = GlamoPTR(pScrn);

	Bool success = FALSE;
	ExaDriverPtr exa;

	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			"EXA hardware acceleration initialising\n");

	exa = pGlamo->exa = exaDriverAlloc();
    if(!exa)
        return FALSE;

    pGlamo->exa->memoryBase = pGlamo->fbstart;
    pGlamo->exa->memorySize = mem_size + mem_start;
    /* TODO: Initalize offScreenBase based on the current screen resolution
     * (width*height*depth) */
    pGlamo->exa->offScreenBase = mem_start;

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->PrepareSolid = GLAMOExaPrepareSolid;
	exa->Solid = GLAMOExaSolid;
	exa->DoneSolid = GLAMOExaDoneSolid;

	exa->PrepareCopy = GLAMOExaPrepareCopy;
	exa->Copy = GLAMOExaCopy;
	exa->DoneCopy = GLAMOExaDoneCopy;

	exa->CheckComposite = GLAMOExaCheckComposite;
	exa->PrepareComposite = GLAMOExaPrepareComposite;
	exa->Composite = GLAMOExaComposite;
	exa->DoneComposite = GLAMOExaDoneComposite;

	exa->DownloadFromScreen = GLAMOExaDownloadFromScreen;
	exa->UploadToScreen = GLAMOExaUploadToScreen;

	/*glamos->exa.MarkSync = GLAMOExaMarkSync;*/
	exa->WaitMarker = GLAMOExaWaitMarker;

	exa->pixmapOffsetAlign = 2;
	exa->pixmapPitchAlign = 2;

	exa->maxX = 640;
	exa->maxY = 640;

	exa->flags = EXA_OFFSCREEN_PIXMAPS;

	RegisterBlockAndWakeupHandlers(GLAMOBlockHandler,
				       GLAMOWakeupHandler,
				       pGlamo->pScreen);

	success = exaDriverInit(pGlamo->pScreen, exa);
	if (success) {
		ErrorF("Initialized EXA acceleration\n");
	} else {
		ErrorF("Failed to initialize EXA acceleration\n");
        free(pGlamo->exa);
        pGlamo->exa = NULL;
	}

	return success;
}

Bool
GLAMOExaPrepareSolid(PixmapPtr      pPix,
		     int            alu,
		     Pixel          pm,
		     Pixel          fg)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	CARD32 offset;
    CARD16 op, pitch;
	FbBits mask;
	RING_LOCALS;

	if (pPix->drawable.bitsPerPixel != 16)
		GLAMO_FALLBACK(("Only 16bpp is supported\n"));

    mask = FbFullMask(16);
	if ((pm & mask) != mask)
		GLAMO_FALLBACK(("Can't do planemask 0x%08x\n",
				(unsigned int) pm));
	op = GLAMOSolidRop[alu] << 8;
	offset = exaGetPixmapOffset(pPix);
	pitch = exaGetPixmapPitch(pPix);

	BEGIN_CMDQ(16);
	OUT_REG(GLAMO_REG_2D_DST_ADDRL, offset & 0xffff);
	OUT_REG(GLAMO_REG_2D_DST_ADDRH, (offset >> 16) & 0x7f);
	OUT_REG(GLAMO_REG_2D_DST_PITCH, pitch & 0x7ff);
	OUT_REG(GLAMO_REG_2D_DST_HEIGHT, pPix->drawable.height);
	OUT_REG(GLAMO_REG_2D_PAT_FG, fg);
	OUT_REG(GLAMO_REG_2D_COMMAND2, op);
	OUT_REG(GLAMO_REG_2D_ID1, 0);
	OUT_REG(GLAMO_REG_2D_ID2, 0);
	END_CMDQ();

	return TRUE;
}

void
GLAMOExaSolid(PixmapPtr pPix, int x1, int y1, int x2, int y2)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	RING_LOCALS;

	BEGIN_CMDQ(10);
	OUT_REG(GLAMO_REG_2D_DST_X, x1);
	OUT_REG(GLAMO_REG_2D_DST_Y, y1);
	OUT_REG(GLAMO_REG_2D_RECT_WIDTH, x2 - x1);
	OUT_REG(GLAMO_REG_2D_RECT_HEIGHT, y2 - y1);
	OUT_REG(GLAMO_REG_2D_COMMAND3, 0);
	END_CMDQ();
}

void
GLAMOExaDoneSolid(PixmapPtr pPix)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
    GLAMODispatchCMDQ(pGlamo);
	exaMarkSync(pGlamo->pScreen);
}

Bool
GLAMOExaPrepareCopy(PixmapPtr       pSrc,
		    PixmapPtr       pDst,
		    int             dx,
		    int             dy,
		    int             alu,
		    Pixel           pm)
{
	ScrnInfoPtr pScrn = xf86Screens[pSrc->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

    RING_LOCALS;

    FbBits mask;

    CARD32 src_offset, dst_offset;
    CARD16 src_pitch, dst_pitch;
    CARD16 op;

	if (pSrc->drawable.bitsPerPixel != 16 ||
	    pDst->drawable.bitsPerPixel != 16)
		GLAMO_FALLBACK(("Only 16bpp is supported"));

	mask = FbFullMask(16);
	if ((pm & mask) != mask) {
		GLAMO_FALLBACK(("Can't do planemask 0x%08x",
				(unsigned int) pm));
	}

	src_offset = exaGetPixmapOffset(pSrc);
	src_pitch = exaGetPixmapPitch(pSrc);

	dst_offset = exaGetPixmapOffset(pDst);
	dst_pitch = exaGetPixmapPitch(pDst);

	op = GLAMOBltRop[alu] << 8;

    BEGIN_CMDQ(20);
    OUT_REG(GLAMO_REG_2D_SRC_ADDRL, src_offset & 0xffff);
	OUT_REG(GLAMO_REG_2D_SRC_ADDRH, (src_offset >> 16) & 0x7f);
	OUT_REG(GLAMO_REG_2D_SRC_PITCH, src_pitch & 0x7ff);

	OUT_REG(GLAMO_REG_2D_DST_ADDRL, dst_offset & 0xffff);
	OUT_REG(GLAMO_REG_2D_DST_ADDRH, (dst_offset >> 16) & 0x7f);
	OUT_REG(GLAMO_REG_2D_DST_PITCH, dst_pitch & 0x7ff);
	OUT_REG(GLAMO_REG_2D_DST_HEIGHT, pDst->drawable.height);

	OUT_REG(GLAMO_REG_2D_COMMAND2, op);
	OUT_REG(GLAMO_REG_2D_ID1, 0);
	OUT_REG(GLAMO_REG_2D_ID2, 0);
	END_CMDQ();


	return TRUE;
}

void
GLAMOExaCopy(PixmapPtr       pDst,
	      int    srcX,
	      int    srcY,
	      int    dstX,
	      int    dstY,
	      int    width,
	      int    height)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	RING_LOCALS;
	BEGIN_CMDQ(14);

	OUT_REG(GLAMO_REG_2D_SRC_X, srcX);
	OUT_REG(GLAMO_REG_2D_SRC_Y, srcY);
	OUT_REG(GLAMO_REG_2D_DST_X, dstX);
	OUT_REG(GLAMO_REG_2D_DST_Y, dstY);
	OUT_REG(GLAMO_REG_2D_RECT_WIDTH, width);
	OUT_REG(GLAMO_REG_2D_RECT_HEIGHT, height);
	OUT_REG(GLAMO_REG_2D_COMMAND3, 0);
	END_CMDQ();
}

void
GLAMOExaDoneCopy(PixmapPtr pDst)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
    GLAMODispatchCMDQ(pGlamo);
	exaMarkSync(pGlamo->pScreen);
}

Bool
GLAMOExaCheckComposite(int op,
		       PicturePtr   pSrcPicture,
		       PicturePtr   pMaskPicture,
		       PicturePtr   pDstPicture)
{
	return FALSE;
}

Bool
GLAMOExaPrepareComposite(int                op,
			 PicturePtr         pSrcPicture,
			 PicturePtr         pMaskPicture,
			 PicturePtr         pDstPicture,
			 PixmapPtr          pSrc,
			 PixmapPtr          pMask,
			 PixmapPtr          pDst)
{
	return FALSE;
}

void
GLAMOExaComposite(PixmapPtr pDst,
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

void
GLAMOExaDoneComposite(PixmapPtr pDst)
{
}

Bool
GLAMOExaUploadToScreen(PixmapPtr pDst,
		       int x,
		       int y,
		       int w,
		       int h,
		       char *src,
		       int src_pitch)
{
    ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    int bpp, i;
    CARD8 *dst_offset;
    int dst_pitch;

    exaWaitSync(pScrn->pScreen);

    bpp = pDst->drawable.bitsPerPixel / 8;
    dst_pitch = exaGetPixmapPitch(pDst);
    dst_offset = pGlamo->exa->memoryBase + exaGetPixmapOffset(pDst)
                    + x*bpp + y*dst_pitch;

    for (i = 0; i < h; i++) {
        memcpy(dst_offset, src, w*bpp);
        dst_offset += dst_pitch;
        src += src_pitch;
    }

    return TRUE;
}

Bool
GLAMOExaDownloadFromScreen(PixmapPtr pSrc,
			   int x,  int y,
			   int w,  int h,
			   char *dst,
			   int dst_pitch)
{
    ScrnInfoPtr pScrn = xf86Screens[pSrc->drawable.pScreen->myNum];
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    int bpp, i;
    CARD8 *dst_offset, *src;
    int src_pitch;

    exaWaitSync(pScrn->pScreen);

    bpp = pSrc->drawable.bitsPerPixel / 8;
    src_pitch = exaGetPixmapPitch(pSrc);
    src = pGlamo->exa->memoryBase + exaGetPixmapOffset(pSrc) +
            x*bpp + y*src_pitch;
    dst_offset = (unsigned char*)dst;

    for (i = 0; i < h; i++) {
        memcpy(dst_offset, src, w*bpp);
        dst_offset += dst_pitch;
        src += src_pitch;
    }

    return TRUE;
}

void
GLAMOExaWaitMarker (ScreenPtr pScreen, int marker)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
    GLAMODispatchCMDQ(pGlamo);
	GLAMOEngineWait(pGlamo, GLAMO_ENGINE_ALL);
}

