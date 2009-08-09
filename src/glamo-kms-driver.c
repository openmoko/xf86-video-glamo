/*
 * KMS Support for the SMedia Glamo3362 X.org Driver
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
 * The KMS parts of this driver are based on xf86-video-modesetting, to
 * which the following notice applies:
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


#include <sys/types.h>
#include <dirent.h>
#include <stdint.h>

#include <xorg-server.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "xf86.h"
#include "xf86Crtc.h"
#include "xf86str.h"
#include "xf86RAC.h"
#include "xf86drm.h"
#include "micmap.h"

#include "glamo.h"
#include "glamo-kms-driver.h"
#include "glamo-kms-exa.h"
#include "glamo-dri2.h"
#include "glamo-kms-crtc.h"


static const char *fbSymbols[] = {
    "fbPictureInit",
    "fbScreenInit",
    NULL
};


/* Return TRUE if KMS can be used */
Bool GlamoKernelModesettingAvailable()
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir("/sys/bus/platform/devices/glamo-fb.0/");
	if ( !dir ) return FALSE;

	do {

		ent = readdir(dir);

		if ( strncmp(ent->d_name, "drm:controlD", 12) == 0 ) {
			closedir(dir);
			return TRUE;
		}

	} while ( ent );

	closedir(dir);
	return FALSE;
}


void GlamoKMSAdjustFrame(int scrnIndex, int x, int y, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
	xf86OutputPtr output = config->output[config->compat_output];
	xf86CrtcPtr crtc = output->crtc;

	if (crtc && crtc->enabled) {
		crtc->funcs->mode_set(crtc,
		                      pScrn->currentMode,
		                      pScrn->currentMode,
		                      x, y);
		crtc->x = output->initial_x + x;
		crtc->y = output->initial_y + y;
	}
}


static Bool CreateFrontBuffer(ScrnInfoPtr pScrn)
{
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	ScreenPtr pScreen = pScrn->pScreen;
	PixmapPtr rootPixmap = pScreen->GetScreenPixmap(pScreen);
	unsigned int flags;

	pScreen->ModifyPixmapHeader(rootPixmap,
	                            pScrn->virtualX, pScrn->virtualY,
	                            pScrn->depth, pScrn->bitsPerPixel,
	                            pScrn->displayWidth * pScrn->bitsPerPixel/8,
	                            NULL);

	drmModeAddFB(pGlamo->drm_fd,
	             pScrn->virtualX,
	             pScrn->virtualY,
	             pScrn->depth,
	             pScrn->bitsPerPixel,
	             pScrn->displayWidth * pScrn->bitsPerPixel / 8,
	             driGetPixmapHandle(rootPixmap, &flags), &pGlamo->fb_id);

	pScrn->frameX0 = 0;
	pScrn->frameY0 = 0;
	GlamoKMSAdjustFrame(pScrn->scrnIndex,
	                    pScrn->frameX0, pScrn->frameY0,
	                    0);

	return TRUE;
}


static Bool crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	if ( (width == pScrn->virtualX) && (height == pScrn->virtualY) )
		return TRUE;	/* Nothing to do */

	ErrorF("RESIZING TO %dx%d\n", width, height);

	pScrn->virtualX = width;
	pScrn->virtualY = height;

	/* HW dependent - FIXME */
	pScrn->displayWidth = pScrn->virtualX;

	drmModeRmFB(pGlamo->drm_fd, pGlamo->fb_id);

	/* now create new frontbuffer */
	return CreateFrontBuffer(pScrn);
}


static const xf86CrtcConfigFuncsRec crtc_config_funcs = {
	crtc_resize
};


Bool GlamoKMSPreInit(ScrnInfoPtr pScrn, int flags)
{
	xf86CrtcConfigPtr xf86_config;
	GlamoPtr pGlamo;
	rgb defaultWeight = { 0, 0, 0 };
	int max_width, max_height;
	Gamma zeros = { 0.0, 0.0, 0.0 };

	/* Can't do this yet */
	if ( flags & PROBE_DETECT ) {
		ConfiguredMonitor = NULL;
		return TRUE;
	}

	/* Allocate driverPrivate */
	if ( !GlamoGetRec(pScrn) ) return FALSE;
	pGlamo = GlamoPTR(pScrn);
	pGlamo->SaveGeneration = -1;

	pScrn->displayWidth = 24;	/* Nonsense default value */

	/* Open DRM */
	pGlamo->drm_fd = drmOpen(NULL, "platform:glamo-fb");
	if ( pGlamo->drm_fd < 0 ) return FALSE;

	pScrn->racMemFlags = RAC_FB | RAC_COLORMAP;
	pScrn->monitor = pScrn->confScreen->monitor;
	pScrn->progClock = TRUE;
	pScrn->rgbBits = 8;

	/* Prefer 16bpp for everything */
	if ( !xf86SetDepthBpp(pScrn, 16, 16, 16, NoDepth24Support) ) {
		return FALSE;
	}

	/* We can only handle 16bpp */
	if ( pScrn->depth != 16 ) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "Given depth (%d) is not supported by the driver\n",
		           pScrn->depth);
		return FALSE;
	}
	xf86PrintDepthBpp(pScrn);

	if ( !xf86SetWeight(pScrn, defaultWeight, defaultWeight) ) return FALSE;
	if ( !xf86SetDefaultVisual(pScrn, -1) ) return FALSE;

	/* Allocate an xf86CrtcConfig */
	xf86CrtcConfigInit(pScrn, &crtc_config_funcs);
	xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

	max_width = 480;
	max_height = 640;
	xf86CrtcSetSizeRange(pScrn, 320, 200, max_width, max_height);

	crtc_init(pScrn);
	output_init(pScrn);

	if ( !xf86InitialConfiguration(pScrn, TRUE) ) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes.\n");
		return FALSE;
	}

	if ( !xf86SetGamma(pScrn, zeros) ) {
	    return FALSE;
	}

	if ( pScrn->modes == NULL ) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No modes.\n");
		return FALSE;
	}

	pScrn->currentMode = pScrn->modes;

	/* Set display resolution */
	xf86SetDpi(pScrn, 0, 0);

	/* Load the required sub modules */
	if (!xf86LoadSubModule(pScrn, "fb")) return FALSE;
	xf86LoaderReqSymLists(fbSymbols, NULL);
	xf86LoadSubModule(pScrn, "exa");
	xf86LoadSubModule(pScrn, "dri2");

	return TRUE;
}


static Bool GlamoKMSCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	if ( pScrn->vtSema ) {
		GlamoKMSLeaveVT(scrnIndex, 0);
	}
	driCloseScreen(pScreen);

	pScreen->CreateScreenResources = pGlamo->CreateScreenResources;

	if ( pGlamo->exa ) {
		GlamoKMSExaClose(pScrn);
	}

	drmClose(pGlamo->drm_fd);
	pGlamo->drm_fd = -1;

	pScrn->vtSema = FALSE;
	pScreen->CloseScreen = pGlamo->CloseScreen;
	return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}


static Bool GlamoKMSCreateScreenResources(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	PixmapPtr rootPixmap;
	Bool ret;
	unsigned int flags;

	pScreen->CreateScreenResources = pGlamo->CreateScreenResources;
	ret = pScreen->CreateScreenResources(pScreen);
	pScreen->CreateScreenResources = GlamoKMSCreateScreenResources;

	rootPixmap = pScreen->GetScreenPixmap(pScreen);

	if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, NULL))
		FatalError("Couldn't adjust screen pixmap\n");

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Adding framebuffer....!\n");

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%i %i %i %i %i %i\n",
	           pGlamo->drm_fd, pScrn->virtualX, pScrn->virtualY,
	           pScrn->depth, pScrn->bitsPerPixel,
	           pScrn->displayWidth * pScrn->bitsPerPixel / 8);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "rootPixmap = %p\n", (void *)rootPixmap);

	drmModeAddFB(pGlamo->drm_fd,
	             pScrn->virtualX,
	             pScrn->virtualY,
	             pScrn->depth,
	             pScrn->bitsPerPixel,
	             pScrn->displayWidth * pScrn->bitsPerPixel / 8,
	             driGetPixmapHandle(rootPixmap, &flags), &pGlamo->fb_id);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Done\n");

	GlamoKMSAdjustFrame(pScrn->scrnIndex,
	                    pScrn->frameX0, pScrn->frameY0,
	                    0);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Finished\n");

	return ret;
}


Bool GlamoKMSScreenInit(int scrnIndex, ScreenPtr pScreen, int argc,
                        char **argv)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	VisualPtr visual;

	/* Deal with server regeneration */
	if ( pGlamo->drm_fd < 0 ) {
		pGlamo->drm_fd = drmOpen(NULL, "platform:glamo-fb");
		if ( pGlamo->drm_fd < 0 ) return FALSE;
	}

	pScrn->pScreen = pScreen;
	pGlamo->pScreen = pScreen;

	/* HW dependent - FIXME */
	pScrn->displayWidth = pScrn->virtualX;

	miClearVisualTypes();

	if ( !miSetVisualTypes(pScrn->depth,
	                      miGetDefaultVisualMask(pScrn->depth),
	                      pScrn->rgbBits, pScrn->defaultVisual) ) {
		return FALSE;
	}

	if ( !miSetPixmapDepths() ) return FALSE;

	pScrn->memPhysBase = 0;
	pScrn->fbOffset = 0;

	if ( !fbScreenInit(pScreen, NULL,
	                  pScrn->virtualX, pScrn->virtualY,
	                  pScrn->xDpi, pScrn->yDpi,
	                  pScrn->displayWidth, pScrn->bitsPerPixel) ) {
		return FALSE;
	}

	if (pScrn->bitsPerPixel > 8) {
		/* Fixup RGB ordering */
		visual = pScreen->visuals + pScreen->numVisuals;
		while (--visual >= pScreen->visuals) {
			if ((visual->class | DynamicClass) == DirectColor) {
				visual->offsetRed = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue = pScrn->offset.blue;
				visual->redMask = pScrn->mask.red;
				visual->greenMask = pScrn->mask.green;
				visual->blueMask = pScrn->mask.blue;
			}
		}
	}

	fbPictureInit(pScreen, NULL, 0);

	pGlamo->CreateScreenResources = pScreen->CreateScreenResources;
	pScreen->CreateScreenResources = GlamoKMSCreateScreenResources;

	xf86SetBlackWhitePixels(pScreen);

	GlamoKMSExaInit(pScrn);

	miInitializeBackingStore(pScreen);
	xf86SetBackingStore(pScreen);
	xf86SetSilkenMouse(pScreen);
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	/* Must force it before EnterVT, so we are in control of VT and
	 * later memory should be bound when allocating, e.g rotate_mem */
	pScrn->vtSema = TRUE;

	pScreen->SaveScreen = xf86SaveScreen;
	pGlamo->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = GlamoKMSCloseScreen;

	if ( !xf86CrtcScreenInit(pScreen) ) return FALSE;

	if ( !miCreateDefColormap(pScreen) ) return FALSE;

	xf86DPMSInit(pScreen, xf86DPMSSet, 0);

	if ( serverGeneration == 1 ) {
		xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
	}

	driScreenInit(pScreen);

	return GlamoKMSEnterVT(scrnIndex, 1);
}


Bool GlamoKMSSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}


Bool GlamoKMSEnterVT(int scrnIndex, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	GlamoPtr pGlamo = GlamoPTR(pScrn);

	/* Only save state once per server generation since that's what most
	* drivers do.  Could change this to save state at each VT enter. */
	if ( pGlamo->SaveGeneration != serverGeneration ) {
		pGlamo->SaveGeneration = serverGeneration;
		/* ...except there is no hardware state to save */
	}

	if ( !flags ) {
		/* signals startup as we'll do this in CreateScreenResources */
		CreateFrontBuffer(pScrn);
	}

	if ( !xf86SetDesiredModes(pScrn) ) return FALSE;

	return TRUE;
}


void GlamoKMSLeaveVT(int scrnIndex, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
	int o;

	for (o = 0; o < config->num_crtc; o++) {

		xf86CrtcPtr crtc = config->crtc[o];

		if ( crtc->rotatedPixmap || crtc->rotatedData ) {
			crtc->funcs->shadow_destroy(crtc, crtc->rotatedPixmap,
			                            crtc->rotatedData);
			crtc->rotatedPixmap = NULL;
			crtc->rotatedData = NULL;
		}

	}

	drmModeRmFB(pGlamo->drm_fd, pGlamo->fb_id);

	pScrn->vtSema = FALSE;
}


ModeStatus GlamoKMSValidMode(int scrnIndex, DisplayModePtr mode, Bool verbose,
                     int flags)
{
	return MODE_OK;
}
