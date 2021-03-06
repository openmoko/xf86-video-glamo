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
 */

#include "xf86.h"

struct glamo_exa_pixmap_priv {
	struct glamo_bo *bo;
};

extern void GlamoKMSExaInit(ScrnInfoPtr pScrn);
extern void GlamoKMSExaClose(ScrnInfoPtr pScrn);
extern unsigned int driGetPixmapHandle(PixmapPtr pPixmap, unsigned int *flags);
extern Bool GlamoKMSExaMakeFullyFledged(PixmapPtr pPix, int width, int height,
                                        int depth, int bitsPerPixel,
                                        int devKind);
