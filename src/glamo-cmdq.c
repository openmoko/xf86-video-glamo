/*
 * Copyright  2007 OpenMoko, Inc.
 * Copyright © 2009 Lars-Peter Clausen <lars@metafoo.de>
 *
 * This driver is based on Xati,
 * Copyright  2004 Eric Anholt
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

#include "glamo-log.h"
#include "glamo.h"
#include "glamo-regs.h"
#include "glamo-cmdq.h"
#include "glamo-engine.h"

static void
GLAMOCMDQResetCP(GlamoPtr pGlamo);

#define CQ_LEN 255
#define CQ_MASK ((CQ_LEN + 1) * 1024 - 1)
#define CQ_MASKL (CQ_MASK & 0xffff)
#define CQ_MASKH (CQ_MASK >> 16)

#if 0
static void
GLAMODumpRegs(GlamoPtr pGlamo, CARD16 from, CARD16 to);

static void
GLAMODebugFifo(GlamoPtr pGlamo)
{
	GLAMOCardInfo *glamoc = pGlamo->glamoc;
	char *mmio = glamoc->reg_base;
	CARD32 offset;

	ErrorF("GLAMO_REG_CMDQ_STATUS: 0x%04x\n",
	    MMIO_IN16(mmio, GLAMO_REG_CMDQ_STATUS));

	offset = MMIO_IN16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRL);
	offset |= (MMIO_IN16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRH) << 16) & 0x7;
	ErrorF("GLAMO_REG_CMDQ_WRITE_ADDR: 0x%08x\n", (unsigned int) offset);

	offset = MMIO_IN16(mmio, GLAMO_REG_CMDQ_READ_ADDRL);
	offset |= (MMIO_IN16(mmio, GLAMO_REG_CMDQ_READ_ADDRH) << 16) & 0x7;
	ErrorF("GLAMO_REG_CMDQ_READ_ADDR: 0x%08x\n", (unsigned int) offset);
}
#endif

void
GLAMODispatchCMDQ(GlamoPtr pGlamo)
{
    MemBuf *buf = pGlamo->cmd_queue;
	volatile char *mmio = pGlamo->reg_base;
	char *addr;
	size_t count, ring_count;
    size_t rest_size;
    size_t ring_read;
    size_t new_ring_write;
    size_t ring_write;

    if (!buf->used)
        return;

    addr = buf->data;
	count = buf->used;
	ring_count = pGlamo->ring_len;

    ring_write = MMIO_IN16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRL);
    ring_write |= MMIO_IN16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRH) << 16;
    new_ring_write = (((ring_write + count) & CQ_MASK) + 1) & ~1;

    /* Wait until there is enough space to queue the cmd buffer */
    if (new_ring_write > ring_write) {
        do {
	        ring_read = MMIO_IN16(mmio, GLAMO_REG_CMDQ_READ_ADDRL) & CQ_MASKL;
        	ring_read |= ((MMIO_IN16(mmio, GLAMO_REG_CMDQ_READ_ADDRH) & CQ_MASKH) << 16);
        } while(ring_read > ring_write && ring_read < new_ring_write);
    } else {
        do {
	        ring_read = MMIO_IN16(mmio, GLAMO_REG_CMDQ_READ_ADDRL) & CQ_MASKL;
        	ring_read |= ((MMIO_IN16(mmio, GLAMO_REG_CMDQ_READ_ADDRH) & CQ_MASKH) << 16);
        } while(ring_read > ring_write || ring_read < new_ring_write);
    }

    /* Wrap around */
    if (ring_write >= new_ring_write) {
        rest_size = (ring_count - ring_write);
        memcpy(pGlamo->ring_addr + ring_write, addr, rest_size);
        memcpy(pGlamo->ring_addr, addr+rest_size, count - rest_size);

        /* ring_write being 0 will result in a deadlock because the cmdq read
         * will never stop. To avoid such an behaviour insert an empty
         * instruction. */
        if (new_ring_write == 0) {
            memset(pGlamo->ring_addr, 0, 4);
            new_ring_write = 4;
        }

        /* The write position has to change to trigger a read */
        if (ring_write == new_ring_write) {
            memset(pGlamo->ring_addr + new_ring_write, 0, 4);
            new_ring_write += 4;
        }
    } else {
        memcpy(pGlamo->ring_addr + ring_write, addr, count);
    }
    /* In Theory waiting for the CMDQ to be ready should be enough, but
     * unfortunally this causes visual artifacts sometimes */
    GLAMOEngineWait(pGlamo, GLAMO_ENGINE_ALL);
    MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
					GLAMO_CLOCK_2D_EN_M6CLK,
					0);

	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRH,
			   (new_ring_write >> 16) & CQ_MASKH);
	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRL,
			   new_ring_write & CQ_MASKL);

    MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
                GLAMO_CLOCK_2D_EN_M6CLK,
					0xffff);
    buf->used = 0;
}

static void
GLAMOCMDQResetCP(GlamoPtr pGlamo)
{
	volatile char *mmio = pGlamo->reg_base;

	/* make the decoder happy? */
	memset(pGlamo->ring_addr, 0, pGlamo->ring_len);

	GLAMOEngineReset(pGlamo, GLAMO_ENGINE_CMDQ);

	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_BASE_ADDRL,
		   pGlamo->ring_start & 0xffff);
	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_BASE_ADDRH,
		   (pGlamo->ring_start >> 16) & 0x7f);
	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_LEN, CQ_LEN);

	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRH, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_WRITE_ADDRL, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_READ_ADDRH, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_READ_ADDRL, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CMDQ_CONTROL,
			 1 << 12 |
			 5 << 8 |
			 8 << 4);
	GLAMOEngineWait(pGlamo, GLAMO_ENGINE_ALL);
}

size_t
GLAMOCMDQInit(ScrnInfoPtr pScrn, size_t mem_start, size_t mem_size)
{
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    MemBuf *buf;

    pGlamo->ring_start = mem_start;
    pGlamo->ring_addr = pGlamo->fbstart + pGlamo->ring_start;

    pGlamo->ring_len = (CQ_LEN + 1) * 1024;

    buf = (MemBuf *)xcalloc(1, sizeof(MemBuf) + pGlamo->ring_len);

    if (!buf) {
        return FALSE;
    }

	buf->size = pGlamo->ring_len;
	buf->used = 0;

	pGlamo->cmd_queue = buf;

    return pGlamo->ring_len;
}

Bool
GLAMOCMDQEnable(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    GLAMOEngineEnable(pGlamo, GLAMO_ENGINE_CMDQ);
    GLAMOCMDQResetCP(pGlamo);

    return TRUE;
}

void
GLAMOCMDQDisable(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);

	GLAMOEngineWait(pGlamo, GLAMO_ENGINE_ALL);
    GLAMOEngineDisable(pGlamo, GLAMO_ENGINE_CMDQ);
}

void
GLAMOCMDQFini(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    GLAMOCMDQDisable(pScrn);

    if (pGlamo->cmd_queue) {
	    xfree(pGlamo->cmd_queue);
	    pGlamo->cmd_queue = NULL;
    }
}

#if 0
static void
GLAMODumpRegs(GlamoPtr pGlamo,
              CARD16 from,
              CARD16 to)
{
	int i=0;
	for (i=from; i <= to; i += 2) {
	    ErrorF("reg:%p, val:%#x\n",
		pGlamo->reg_base+i,
		*(VOL16*)(pGlamo->reg_base+i));
	}
}
#endif
