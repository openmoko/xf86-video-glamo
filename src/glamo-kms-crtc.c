/*
 * KMS Support for the SMedia Glamo3362 X.org Driver
 *
 * Modified: 2009 by Thomas White <taw@bitwiz.org.uk>
 *
 * Based on crtc.c from xf86-video-modesetting, to which the following
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

#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>

#include <xf86.h>
#include <xf86i2c.h>
#include <xf86Crtc.h>
#include <xf86drm.h>
#include <drm.h>
#include <xf86drmMode.h>
#include <xf86Modes.h>
#define DPMS_SERVER
#include <X11/extensions/dpmsconst.h>

#include "glamo.h"
#include "glamo-kms-crtc.h"


struct crtc_private
{
	drmModeCrtcPtr drm_crtc;
};


static void crtc_dpms(xf86CrtcPtr crtc, int mode)
{
	switch (mode) {
	case DPMSModeOn:
	case DPMSModeStandby:
	case DPMSModeSuspend:
		break;
	case DPMSModeOff:
		break;
	}
}


#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0)


static Bool crtc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
                                Rotation rot, int x, int y)
{
	ScrnInfoPtr scrn = crtc->scrn;
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
	DisplayModeRec saved_mode;
	int saved_x, saved_y;
	Rotation saved_rotation;
	GlamoPtr pGlamo = GlamoPTR(crtc->scrn);
	Bool ret = FALSE;
	int i;
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
	xf86OutputPtr output = config->output[config->compat_output];
	drmModeConnectorPtr drm_connector = output->driver_private;
	struct crtc_private *crtcp = crtc->driver_private;
	drmModeCrtcPtr drm_crtc = crtcp->drm_crtc;
	drmModeModeInfo drm_mode;

	crtc->enabled = xf86CrtcInUse (crtc);

	if ( !crtc->enabled ) return TRUE;

	saved_mode = crtc->mode;
	saved_x = crtc->x;
	saved_y = crtc->y;
	saved_rotation = crtc->rotation;

	crtc->mode = *mode;
	crtc->x = x;
	crtc->y = y;
	crtc->rotation = rot;

	crtc->funcs->dpms(crtc, DPMSModeOff);
	for ( i=0; i<xf86_config->num_output; i++ ) {
		xf86OutputPtr output = xf86_config->output[i];
		if (output->crtc != crtc)continue;
		output->funcs->prepare(output);
	}

	/* Set the mode... */
	drm_mode.clock = mode->Clock * 1000.0;
	if ( (rot == RR_Rotate_0) || (rot == RR_Rotate_180) ) {
		drm_mode.hdisplay = mode->HDisplay;
		drm_mode.hsync_start = mode->HSyncStart;
		drm_mode.hsync_end = mode->HSyncEnd;
		drm_mode.htotal = mode->HTotal;
		drm_mode.vdisplay = mode->VDisplay;
		drm_mode.vsync_start = mode->VSyncStart;
		drm_mode.vsync_end = mode->VSyncEnd;
		drm_mode.vtotal = mode->VTotal;
	} else if ( (rot == RR_Rotate_90) || (rot == RR_Rotate_270) ) {
		drm_mode.hdisplay = mode->VDisplay;
		drm_mode.hsync_start = mode->VSyncStart;
		drm_mode.hsync_end = mode->VSyncEnd;
		drm_mode.htotal = mode->VTotal;
		drm_mode.vdisplay = mode->HDisplay;
		drm_mode.vsync_start = mode->HSyncStart;
		drm_mode.vsync_end = mode->HSyncEnd;
		drm_mode.vtotal = mode->HTotal;
	} else {
		drm_mode.hdisplay = mode->HDisplay;
		drm_mode.hsync_start = mode->HSyncStart;
		drm_mode.hsync_end = mode->HSyncEnd;
		drm_mode.htotal = mode->HTotal;
		drm_mode.vdisplay = mode->VDisplay;
		drm_mode.vsync_start = mode->VSyncStart;
		drm_mode.vsync_end = mode->VSyncEnd;
		drm_mode.vtotal = mode->VTotal;
		ErrorF("Couldn't determine rotation\n");
	}
	drm_mode.flags = mode->Flags;
	drm_mode.hskew = mode->HSkew;
	drm_mode.vscan = mode->VScan;
	drm_mode.vrefresh = mode->VRefresh;
	if ( !mode->name ) xf86SetModeDefaultName(mode);
	strncpy(drm_mode.name, mode->name, DRM_DISPLAY_MODE_LEN);
	drmModeSetCrtc(pGlamo->drm_fd, drm_crtc->crtc_id, pGlamo->fb_id,
	                x, y, &drm_connector->connector_id, 1, &drm_mode);

	crtc->funcs->dpms (crtc, DPMSModeOn);
	for (i = 0; i < xf86_config->num_output; i++){
		xf86OutputPtr output = xf86_config->output[i];
		if (output->crtc == crtc) {
			output->funcs->commit(output);
#ifdef RANDR_12_INTERFACE
			if (output->randr_output) {
				RRPostPendingProperties (output->randr_output);
			}
#endif
		}
	}

	ret = TRUE;
	if ( scrn->pScreen ) xf86CrtcSetScreenSubpixelOrder(scrn->pScreen);

	if ( !ret ) {
		crtc->x = saved_x;
		crtc->y = saved_y;
		crtc->rotation = saved_rotation;
		crtc->mode = saved_mode;
	}

	return ret;
}


#else /* XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0) */


static Bool crtc_lock(xf86CrtcPtr crtc)
{
	return FALSE;
}


static void crtc_unlock(xf86CrtcPtr crtc)
{
}


static void crtc_prepare(xf86CrtcPtr crtc)
{
}


static void crtc_commit(xf86CrtcPtr crtc)
{
}


static Bool crtc_mode_fixup(xf86CrtcPtr crtc, DisplayModePtr mode,
                            DisplayModePtr adjusted_mode)
{
	return TRUE;
}


static void crtc_mode_set(xf86CrtcPtr crtc, DisplayModePtr mode,
                          DisplayModePtr adjusted_mode, int x, int y)
{
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
	GlamoPtr pGlamo = GlamoPTR(crtc->scrn);
	xf86OutputPtr output = config->output[config->compat_output];
	drmModeConnectorPtr drm_connector = output->driver_private;
	struct crtc_private *crtcp = crtc->driver_private;
	drmModeCrtcPtr drm_crtc = crtcp->drm_crtc;
	drmModeModeInfo drm_mode;

	drm_mode.clock = mode->Clock * 1000.0;
	drm_mode.hdisplay = mode->HDisplay;
	drm_mode.hsync_start = mode->HSyncStart;
	drm_mode.hsync_end = mode->HSyncEnd;
	drm_mode.htotal = mode->HTotal;
	drm_mode.vdisplay = mode->VDisplay;
	drm_mode.vsync_start = mode->VSyncStart;
	drm_mode.vsync_end = mode->VSyncEnd;
	drm_mode.vtotal = mode->VTotal;
	drm_mode.flags = mode->Flags;
	drm_mode.hskew = mode->HSkew;
	drm_mode.vscan = mode->VScan;
	drm_mode.vrefresh = mode->VRefresh;
	if ( !mode->name )
		xf86SetModeDefaultName(mode);
	strncpy(drm_mode.name, mode->name, DRM_DISPLAY_MODE_LEN);

	drmModeSetCrtc(pGlamo->drm_fd, drm_crtc->crtc_id, pGlamo->fb_id, x, y,
	               &drm_connector->connector_id, 1, &drm_mode);
}

#endif /* XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0) */


void crtc_load_lut(xf86CrtcPtr crtc)
{
}


static void crtc_gamma_set(xf86CrtcPtr crtc,
                           CARD16 *red, CARD16 *green, CARD16 *blue,
                           int size)
{
}


static void *crtc_shadow_allocate(xf86CrtcPtr crtc, int width, int height)
{
	return NULL;
}


static PixmapPtr crtc_shadow_create(xf86CrtcPtr crtc, void *data,
                                    int width, int height)
{
	return NULL;
}


static void crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap,
                                void *data)
{
}


static void crtc_destroy(xf86CrtcPtr crtc)
{
	struct crtc_private *crtcp = crtc->driver_private;

	drmModeFreeCrtc(crtcp->drm_crtc);
	xfree(crtcp);
}


static const xf86CrtcFuncsRec crtc_funcs = {
	.dpms = crtc_dpms,
	.save = NULL,
	.restore = NULL,
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0)
	.lock = NULL,
	.unlock = NULL,
	.mode_fixup = NULL,
	.prepare = NULL,
	.mode_set = NULL,
	.commit = NULL,
	.set_mode_major = crtc_set_mode_major,
#else
	.lock = crtc_lock,
	.unlock = crtc_unlock,
	.mode_fixup = crtc_mode_fixup,
	.prepare = crtc_prepare,
	.mode_set = crtc_mode_set,
	.commit = crtc_commit,
#endif
	.gamma_set = crtc_gamma_set,
	.shadow_create = crtc_shadow_create,
	.shadow_allocate = crtc_shadow_allocate,
	.shadow_destroy = crtc_shadow_destroy,
	.set_cursor_position = NULL,
	.show_cursor = NULL,
	.hide_cursor = NULL,
	.load_cursor_image = NULL,	       /* lets convert to argb only */
	.set_cursor_colors = NULL,	       /* using argb only */
	.load_cursor_argb = NULL,
	.destroy = crtc_destroy,
};


void crtc_init(ScrnInfoPtr pScrn)
{
	xf86CrtcPtr crtc;
	GlamoPtr pGlamo = GlamoPTR(pScrn);
	drmModeResPtr res;
	drmModeCrtcPtr drm_crtc = NULL;
	struct crtc_private *crtcp;
	int c;

	res = drmModeGetResources(pGlamo->drm_fd);
	if (res == 0) {
		ErrorF("Failed drmModeGetResources %d\n", errno);
		return;
	}

	for (c = 0; c < res->count_crtcs; c++) {
		drm_crtc = drmModeGetCrtc(pGlamo->drm_fd, res->crtcs[c]);
		if (!drm_crtc)
		    continue;

		crtc = xf86CrtcCreate(pScrn, &crtc_funcs);
		if (crtc == NULL)
		    goto out;

		crtcp = xcalloc(1, sizeof(struct crtc_private));
		if (!crtcp) {
		    xf86CrtcDestroy(crtc);
		    goto out;
		}

		crtcp->drm_crtc = drm_crtc;

		crtc->driver_private = crtcp;
	}

out:
	drmModeFreeResources(res);
}
