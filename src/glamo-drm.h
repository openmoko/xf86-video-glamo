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


#ifndef _GLAMO_DRM_H
#define _GLAMO_DRM_H

#include <stdint.h>
#include <glamo_bo.h>

#include "glamo.h"

extern void GlamoDRMInit(GlamoPtr pGlamo);
extern void GlamoDRMDispatch(GlamoPtr pGlamo);
extern void GlamoDRMAddCommand(GlamoPtr pGlamo, uint16_t reg, uint16_t val);
extern void GlamoDRMAddCommandBO(GlamoPtr pGlamo, uint16_t reg,
                                 struct glamo_bo *bo);

#endif /* _GLAMO_DRM_H */
