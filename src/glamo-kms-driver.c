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

#include "xf86.h"


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

	} while ( ent )

	closedir(dir);
	return FALSE;
}


Bool GlamoKMSPreInit(ScrnInfoPtr pScrn, int flags)
{
	xf86CrtcConfigPtr xf86_config;
	GlamoPtr pGlamo;
	MessageType from = X_PROBED;
	rgb defaultWeight = { 0, 0, 0 };
	EntityInfoPtr pEnt;
	EntPtr glamoEnt = NULL;
	char *BusID;
	int i;
	char *s;
	int num_pipe;
	int max_width, max_height;

	if ( pScrn->numEntities != 1 ) return FALSE;

	pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

	/* Can't do this yet */
	if ( flags & PROBE_DETECT ) {
		ConfiguredMonitor = NULL;
		return TRUE;
	}

	/* Allocate driverPrivate */
	if ( !GlamoGetRec(pScrn) ) return FALSE;
	pGlamo = GlamoPTR(pScrn);
	pGlamo->SaveGeneration = -1;
	pGlamo->pEnt = pEnt;

	pScrn->displayWidth = 640;	       /* default it */

	/* Allocate an entity private if necessary */
	if ( xf86IsEntityShared(pScrn->entityList[0]) ) {
		msEnt = xf86GetEntityPrivate(pScrn->entityList[0],
		                             modesettingEntityIndex)->ptr;
		pGlamo->entityPrivate = msEnt;
	} else {
		pGlamo->entityPrivate = NULL;
	}

	if ( xf86RegisterResources(ms->pEnt->index, NULL, ResNone) ) {
		return FALSE;
	}

	if ( xf86IsEntityShared(pScrn->entityList[0]) ) {
		if ( xf86IsPrimInitDone(pScrn->entityList[0]) ) {
			/* do something */
		} else {
		    xf86SetPrimInitDone(pScrn->entityList[0]);
		}
	}

	pGlamo->drm_fd = drmOpen(NULL, "platform:glamo-fb");

	if ( ms->fd < 0 ) return FALSE;

	pScrn->racMemFlags = RAC_FB | RAC_COLORMAP;
	pScrn->monitor = pScrn->confScreen->monitor;
	pScrn->progClock = TRUE;
	pScrn->rgbBits = 8;

	if ( !xf86SetDepthBpp (pScrn, 0, 0, 0
	                       PreferConvert24to32
	                       | SupportConvert24to32
	                       | Support32bppFb))
	return FALSE;

	if ( pScrn->depth != 16 ) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "Given depth (%d) is not supported by the driver\n",
		           pScrn->depth);
		return FALSE;
	}
	xf86PrintDepthBpp(pScrn);

	if ( !xf86SetWeight(pScrn, defaultWeight, defaultWeight) ) return FALSE;
	if ( !xf86SetDefaultVisual(pScrn, -1) ) return FALSE;

	/* Process the options */
	xf86CollectOptions(pScrn, NULL);
	if ( !(ms->Options = xalloc(sizeof(Options))) ) return FALSE;
	memcpy(ms->Options, Options, sizeof(Options));
	xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, ms->Options);

	/* Allocate an xf86CrtcConfig */
	xf86CrtcConfigInit(pScrn, &crtc_config_funcs);
	xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

	max_width = 8192;
	max_height = 8192;
	xf86CrtcSetSizeRange(pScrn, 320, 200, max_width, max_height);

	if (xf86ReturnOptValBool(ms->Options, OPTION_SW_CURSOR, FALSE)) {
		ms->SWCursor = TRUE;
	}

	SaveHWState(pScrn);

	crtc_init(pScrn);
	output_init(pScrn);

	if (!xf86InitialConfiguration(pScrn, TRUE)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes.\n");
		RestoreHWState(pScrn);
		return FALSE;
	}

	RestoreHWState(pScrn);

	/*
	 * If the driver can do gamma correction, it should call xf86SetGamma() here.
	 */
	{
	Gamma zeros = { 0.0, 0.0, 0.0 };

	if (!xf86SetGamma(pScrn, zeros)) {
	    return FALSE;
	}
	}

	if (pScrn->modes == NULL) {
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
