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

#include <xorg-server.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "xf86.h"
#include "xf86Crtc.h"
#include "xf86str.h"
#include "xf86RAC.h"
#include "xf86drm.h"

#include "glamo.h"


static const char *fbSymbols[] = {
    "fbPictureInit",
    "fbScreenInit",
    NULL
};


static int modesettingEntityIndex = -1;


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


static Bool crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
#if 0
    modesettingPtr ms = modesettingPTR(pScrn);
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr rootPixmap = pScreen->GetScreenPixmap(pScreen);
    Bool fbAccessDisabled;
    CARD8 *fbstart;

    if (width == pScrn->virtualX && height == pScrn->virtualY)
	return TRUE;

    ErrorF("RESIZING TO %dx%d\n", width, height);

    pScrn->virtualX = width;
    pScrn->virtualY = height;

    /* HW dependent - FIXME */
    pScrn->displayWidth = pScrn->virtualX;

    drmModeRmFB(ms->fd, ms->fb_id);

    /* now create new frontbuffer */
    return CreateFrontBuffer(pScrn);
#endif
	return FALSE;
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

	if ( !xf86SetDepthBpp(pScrn, 0, 0, 0, PreferConvert24to32
	                       | SupportConvert24to32 | Support32bppFb) ) {
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


Bool GlamoKMSScreenInit(int scrnIndex, ScreenPtr pScreen, int argc,
                        char **argv)
{
}


Bool GlamoSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
}


void GlamoAdjustFrame(int scrnIndex, int x, int y, int flags)
{
}


Bool GlamoEnterVT(int scrnIndex, int flags)
{
}


void GlamoLeaveVT(int scrnIndex, int flags)
{
}


ModeStatus GlamoValidMode(int scrnIndex, DisplayModePtr mode, Bool verbose,
                     int flags)
{
}
