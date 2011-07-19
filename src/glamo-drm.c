/*
 * DRI for the SMedia Glamo3362 X.org Driver
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
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include <xf86drm.h>
#include <glamo_drm.h>
#include <glamo_bo.h>

#include "glamo.h"

/* How many commands can be stored before forced dispatch */
#define GLAMO_CMDQ_MAX_COUNT 1024

/* Submit the prepared command sequence to the kernel */
void GlamoDRMDispatch(GlamoPtr pGlamo)
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


void GlamoDRMAddCommand(GlamoPtr pGlamo, uint16_t reg, uint16_t val)
{
	if ( pGlamo->cmdq_drm_used >= GLAMO_CMDQ_MAX_COUNT - 2 ) {
		xf86DrvMsg(pGlamo->pScreen->myNum, X_INFO,
		           "Forced command cache flush.\n");
		GlamoDRMDispatch(pGlamo);
	}

	/* Record command */
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = reg;
	pGlamo->cmdq_drm[pGlamo->cmdq_drm_used++] = val;
}


void GlamoDRMAddCommandBO(GlamoPtr pGlamo, uint16_t reg, struct glamo_bo *bo)
{
	if ( pGlamo->cmdq_drm_used >= GLAMO_CMDQ_MAX_COUNT - 4 ||
	       pGlamo->cmdq_obj_used >= GLAMO_CMDQ_MAX_COUNT) {
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

	pGlamo->last_buffer_object = bo;
}


void GlamoDRMInit(GlamoPtr pGlamo)
{
	pGlamo->cmdq_objs = malloc(GLAMO_CMDQ_MAX_COUNT*sizeof(uint32_t));
	pGlamo->cmdq_obj_pos = malloc(GLAMO_CMDQ_MAX_COUNT*sizeof(unsigned int));
	pGlamo->cmdq_obj_used = 0;
	pGlamo->cmdq_drm_used = 0;
	/* we're using 2bytes per entry (uint16_t) that's why we need to allocate
	 * GLAMO_CMDQ_MAX_COUNT * 2 bytes
	 */
	pGlamo->cmdq_drm_size = 2 * GLAMO_CMDQ_MAX_COUNT;
	pGlamo->cmdq_drm = malloc(pGlamo->cmdq_drm_size);
}
