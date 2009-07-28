/*
 * Copyright  2007 OpenMoko, Inc.
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

#ifndef _GLAMO_DRM_CMDQ_H_
#define _GLAMO_DRM_CMDQ_H_

#define CCE_DEBUG 0

#if !CCE_DEBUG

#define RING_LOCALS	CARD16 *__head; int __count; int __objects;     \
                        char *__objs; char *__obj_pos;

#define BEGIN_CMDQ(n)							\
do {									\
	if ((pGlamo->cmd_queue->used + 2 * (n)) >			\
	    pGlamo->cmd_queue->size) {					\
		GlamoDRMDispatch(pGlamo);				\
	}								\
	__head = (CARD16 *)((char *)pGlamo->cmd_queue->data +		\
	    pGlamo->cmd_queue->used);					\
	__count = 0;							\
	__objects = 0;                                                  \
	__objs = pGlamo->cmdq_objs + (pGlamo->cmdq_obj_used*2);         \
	__obj_pos = pGlamo->cmdq_obj_pos + (pGlamo->cmdq_obj_used*2);   \
} while (0)

#define END_CMDQ() do {							\
	pGlamo->cmd_queue->used += __count * 2;				\
} while (0)

#define OUT_BURST_REG(reg, val) do {					\
       __head[__count++] = (val);					\
} while (0)

#define OUT_BURST(reg, n)						\
do {									\
       OUT_PAIR((1 << 15) | reg, n);					\
} while (0)

#else /* CCE_DEBUG */

#define RING_LOCALS                                                     \
	CARD16 *__head; int __count, __total, __reg, __packet0count;    \
	int __objects; char *__objs; char *__obj_pos;

#define BEGIN_CMDQ(n)							\
do {									\
	if ((pGlamo->cmd_queue->used + 2 * (n)) >			\
	    pGlamo->cmd_queue->size) {					\
		GlamoDRMDispatch(pGlamo);				\
	}								\
	__head = (CARD16 *)((char *)pGlamo->cmd_queue->data +		\
	    pGlamo->cmd_queue->used);					\
	__count = 0;							\
	__total = n;							\
	__reg = 0;							\
	__packet0count = 0;						\
	__objects = 0;                                                  \
	__objs = pGlamo->cmdq_objs + (pGlamo->cmdq_obj_used*2);         \
	__obj_pos = pGlamo->cmdq_obj_pos + (pGlamo->cmdq_obj_used*2);   \
} while (0)

#define END_CMDQ() do {							\
	if (__count != __total)						\
		FatalError("count != total (%d vs %d) at %s:%d\n",	\
		     __count, __total, __FILE__, __LINE__);		\
	pGlamo->cmd_queue->used += __count * 2;				\
	pGlamo->cmdq_objs_used += __objects;                            \
} while (0)

#define OUT_BURST_REG(reg, val) do {					\
       if (__reg != reg)						\
               FatalError("unexpected reg (0x%x vs 0x%x) at %s:%d\n",	\
                   reg, __reg, __FILE__, __LINE__);			\
       if (__packet0count-- <= 0)					\
               FatalError("overrun of packet0 at %s:%d\n",		\
                   __FILE__, __LINE__);					\
       __head[__count++] = (val);					\
       __reg += 2;							\
} while (0)

#define OUT_BURST(reg, n)						\
do {									\
       OUT_PAIR((1 << 15) | reg, n);					\
       __reg = reg;							\
       __packet0count = n;						\
} while (0)

#endif /* CCE_DEBUG */

#define OUT_PAIR(v1, v2)						\
do {									\
       __head[__count++] = (v1);					\
       __head[__count++] = (v2);					\
} while (0)


#define OUT_REG(reg, val)						\
       OUT_PAIR(reg, val)

#define OUT_REG_BO(reg, bo) __objs[__objects] = bo->handle;             \
                            __obj_pos[__objects++] = __count;           \
                            __head[__count++] = (reg);                  \
                            __head[__count++] = 0x0000;                 \
                            __head[__count++] = (reg+2);                \
                            __head[__count++] = 0x0000;

#endif /* _GLAMO_DRM_CMDQ_H_ */
