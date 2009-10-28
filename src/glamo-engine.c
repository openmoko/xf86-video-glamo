/*
 * Copyright © 2009 Lars-Peter Clausen <lars@metafoo.de>
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

#include <unistd.h>

#include "glamo.h"
#include "glamo-engine.h"
#include "glamo-regs.h"

#ifdef HAVE_ENGINE_IOCTLS
#   include <linux/types.h>
#   include <sys/ioctl.h>
#   include <errno.h>
#endif

void
GLAMOEngineReset(GlamoPtr pGlamo, enum GLAMOEngine engine)
{
#ifdef HAVE_ENGINE_IOCTLS
    if (ioctl(pGlamo->fb_fd, GLAMOFB_ENGINE_RESET, (void*)((__u32)engine)) == -1)
        xf86DrvMsg(xf86Screens[pGlamo->pScreen->myNum]->scrnIndex, X_ERROR,
                  "Framebuffer ioctl GLAMOFB_ENGINE_RESET failed: %s\n",
                 strerror(errno));
#else
	CARD32 reg;
	CARD16 mask;
	volatile char *mmio = pGlamo->reg_base;

	if (!mmio)
		return;

	switch (engine) {
		case GLAMO_ENGINE_CMDQ:
			reg = GLAMO_REG_CLOCK_2D;
			mask = GLAMO_CLOCK_2D_CMDQ_RESET;
			break;
		case GLAMO_ENGINE_ISP:
			reg = GLAMO_REG_CLOCK_ISP;
			mask = GLAMO_CLOCK_ISP2_RESET;
			break;
		case GLAMO_ENGINE_2D:
			reg = GLAMO_REG_CLOCK_2D;
			mask = GLAMO_CLOCK_2D_RESET;
			break;
		default:
			return;
			break;
	}
	MMIOSetBitMask(mmio, reg, mask, 0xffff);
    usleep(15000);
    MMIOSetBitMask(mmio, reg, mask, 0);
    usleep(15000);
#endif
}

void
GLAMOEngineDisable(GlamoPtr pGlamo, enum GLAMOEngine engine)
{
#ifdef HAVE_ENGINE_IOCTLS
    if (ioctl(pGlamo->fb_fd, GLAMOFB_ENGINE_DISABLE, (void*)((__u32)engine)) == -1)
        xf86DrvMsg(xf86Screens[pGlamo->pScreen->myNum]->scrnIndex, X_ERROR,
                  "Framebuffer ioctl GLAMOFB_ENGINE_DISABLE failed: %s\n",
                 strerror(errno));
#else
	volatile char *mmio = pGlamo->reg_base;

	if (!mmio)
		return;

    switch (engine) {
		case GLAMO_ENGINE_CMDQ:
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
					GLAMO_CLOCK_2D_EN_M6CLK,
					0);
			MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
					GLAMO_HOSTBUS2_MMIO_EN_CMDQ,
					0);
			break;
		case GLAMO_ENGINE_ISP:
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_ISP,
					GLAMO_CLOCK_ISP_EN_M2CLK |
					GLAMO_CLOCK_ISP_EN_I1CLK,
					0);
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_2,
					GLAMO_CLOCK_GEN52_EN_DIV_ICLK,
					0);
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
					GLAMO_CLOCK_GEN51_EN_DIV_JCLK,
					0);
			MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
					GLAMO_HOSTBUS2_MMIO_EN_ISP,
					0);
			break;
		case GLAMO_ENGINE_2D:
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
					GLAMO_CLOCK_2D_EN_M7CLK |
					GLAMO_CLOCK_2D_EN_GCLK |
					GLAMO_CLOCK_2D_DG_M7CLK |
					GLAMO_CLOCK_2D_DG_GCLK,
					0);
			MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
					GLAMO_HOSTBUS2_MMIO_EN_2D,
					0);
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
					GLAMO_CLOCK_GEN51_EN_DIV_GCLK,
					0);
			break;
		default:
			break;
	}
#endif
}

void
GLAMOEngineEnable(GlamoPtr pGlamo, enum GLAMOEngine engine)
{
#ifdef HAVE_ENGINE_IOCTLS
    if (ioctl(pGlamo->fb_fd, GLAMOFB_ENGINE_ENABLE, (void*)((__u32)engine)) == -1)
        xf86DrvMsg(xf86Screens[pGlamo->pScreen->myNum]->scrnIndex, X_ERROR,
                  "Framebuffer ioctl GLAMOFB_ENGINE_ENABLE failed: %s\n",
                 strerror(errno));
#else
	volatile char *mmio = pGlamo->reg_base;

	if (!mmio)
		return;

	switch (engine) {
		case GLAMO_ENGINE_CMDQ:
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
					GLAMO_CLOCK_2D_EN_M6CLK,
					0xffff);
			MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
					GLAMO_HOSTBUS2_MMIO_EN_CMDQ,
					0xffff);
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
					GLAMO_CLOCK_GEN51_EN_DIV_MCLK,
					0xffff);
			break;
		case GLAMO_ENGINE_ISP:
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_ISP,
					GLAMO_CLOCK_ISP_EN_M2CLK |
					GLAMO_CLOCK_ISP_EN_I1CLK,
					0xffff);
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_2,
					GLAMO_CLOCK_GEN52_EN_DIV_ICLK,
					0xffff);
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
					GLAMO_CLOCK_GEN51_EN_DIV_JCLK,
					0xffff);
			MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
					GLAMO_HOSTBUS2_MMIO_EN_ISP,
					0xffff);
			break;
		case GLAMO_ENGINE_2D:
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
					GLAMO_CLOCK_2D_EN_M7CLK |
					GLAMO_CLOCK_2D_EN_GCLK |
					GLAMO_CLOCK_2D_DG_M7CLK |
					GLAMO_CLOCK_2D_DG_GCLK,
					0xffff);
			MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
					GLAMO_HOSTBUS2_MMIO_EN_2D,
					0xffff);
			MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
					GLAMO_CLOCK_GEN51_EN_DIV_GCLK,
					0xffff);
			break;
		default:
			break;
	}
#endif
}

bool
GLAMOEngineBusy(GlamoPtr pGlamo, enum GLAMOEngine engine)
{
	volatile char *mmio = pGlamo->reg_base;
	CARD16 status, mask, val;

	if (!mmio)
		return FALSE;

	switch (engine)
	{
		case GLAMO_ENGINE_CMDQ:
			mask = 0x3;
			val  = mask;
			break;
		case GLAMO_ENGINE_ISP:
			mask = 0x3 | (1 << 8);
			val  = 0x3;
			break;
		case GLAMO_ENGINE_2D:
			mask = 0x3 | (1 << 4);
			val  = 0x3;
			break;
		case GLAMO_ENGINE_ALL:
		default:
			mask = 1 << 2;
			val  = mask;
			break;
	}

	status = MMIO_IN16(mmio, GLAMO_REG_CMDQ_STATUS);

	return !((status & mask) == val);
}

void
GLAMOEngineWait(GlamoPtr pGlamo,
		   enum GLAMOEngine engine)
{
	volatile char *mmio = pGlamo->reg_base;
	CARD16 status, mask, val;

	if (!mmio)
		return;

	switch (engine)
	{
		case GLAMO_ENGINE_CMDQ:
			mask = 0x3;
			val  = mask;
			break;
		case GLAMO_ENGINE_ISP:
			mask = 0x3 | (1 << 8);
			val  = 0x3;
			break;
		case GLAMO_ENGINE_2D:
			mask = 0x3 | (1 << 4);
			val  = 0x3;
			break;
		case GLAMO_ENGINE_ALL:
		default:
			mask = 1 << 2;
			val  = mask;
			break;
	}

	do {
		status = MMIO_IN16(mmio, GLAMO_REG_CMDQ_STATUS);
    } while ((status & mask) != val);
}
