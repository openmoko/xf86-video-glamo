/*
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

#include <stdbool.h>

#ifdef HAVE_ENGINE_IOCTLS
#include <linux/glamofb.h>

typedef GLAMOEngine glamo_engine;

#else

enum GLAMOEngine {
	GLAMO_ENGINE_CMDQ,
	GLAMO_ENGINE_ISP,
	GLAMO_ENGINE_2D,
	GLAMO_ENGINE_MPEG,
	GLAMO_ENGINE_ALL,
	NB_GLAMO_ENGINES /*should be the last entry*/
};
#endif /* #ifdef HAVE_ENGINE_IOCTLS */

void
GLAMOEngineEnable(GlamoPtr pGlamo, enum GLAMOEngine engine);

void
GLAMOEngineDisable(GlamoPtr pGlamo, enum GLAMOEngine engine);

void
GLAMOEngineReset(GlamoPtr pGlamo, enum GLAMOEngine engine);

bool
GLAMOEngineBusy(GlamoPtr pGlamo, enum GLAMOEngine engine);

void
GLAMOEngineWait(GlamoPtr pGlamo, enum GLAMOEngine engine);

