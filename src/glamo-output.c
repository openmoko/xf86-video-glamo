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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86i2c.h"
#include "xf86Crtc.h"
#include "xf86Modes.h"

#include "fbdevhw.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "glamo.h"

#ifdef JBT6K74_SET_STATE
static const char jbt6k74_state_vga[] = "normal";
static const char jbt6k74_state_qvga[] = "qvga-normal";
#endif

typedef struct _GlamoOutput {
   DisplayModePtr modes;
} GlamoOutputRec, *GlamoOutputPtr;

static void
GlamoOutputDPMS(xf86OutputPtr output, int mode) {}

static xf86OutputStatus
GlamoOutputDetect(xf86OutputPtr output);

static Bool
GlamoOutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
                     DisplayModePtr mode_adjusted);

static void
GlamoOutputPrepare(xf86OutputPtr output);

static void
GlamoOutputModeSet(xf86OutputPtr output, DisplayModePtr mode,
                   DisplayModePtr adjusted_mode);

static int
GlamoOutputModeValid(xf86OutputPtr output, DisplayModePtr mode);

static Bool
GlamoOutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
                     DisplayModePtr mode_adjusted);

static void
GlamoOutputPrepare(xf86OutputPtr output);

static void GlamoOutputModeSet(xf86OutputPtr output, DisplayModePtr mode,
                  DisplayModePtr adjusted_mode);

static void
GlamoOutputCommit(xf86OutputPtr output);

static void
GlamoOutputDestroy(xf86OutputPtr output);

static DisplayModePtr
GlamoOutputGetModes(xf86OutputPtr output);

static const xf86OutputFuncsRec glamo_output_funcs = {
    .create_resources = NULL,
    .dpms = GlamoOutputDPMS,
    .save = NULL,
    .restore = NULL,
    .mode_valid = GlamoOutputModeValid,
    .mode_fixup = GlamoOutputModeFixup,
    .prepare = GlamoOutputPrepare,
    .commit = GlamoOutputCommit,
    .mode_set = GlamoOutputModeSet,
    .detect = GlamoOutputDetect,
    .get_modes = GlamoOutputGetModes,
#ifdef RANDR_12_INTERFACE
    .set_property = NULL,
#endif
    .destroy = GlamoOutputDestroy
};

static void
ConvertModeFbToXfree(const struct fb_var_screeninfo *var, DisplayModePtr mode,
                     Rotation *rotation) {
    mode->HDisplay = var->xres;
    mode->VDisplay = var->yres;

    mode->Clock = var->pixclock ? 1000000000 / var->pixclock : 0;
    mode->HSyncStart = mode->HDisplay + var->right_margin;
    mode->HSyncEnd = mode->HSyncStart + var->hsync_len;
    mode->HTotal = mode->HSyncEnd + var->left_margin;
    mode->VSyncStart = mode->VDisplay + var->lower_margin;
    mode->VSyncEnd = mode->VSyncStart + var->vsync_len;
    mode->VTotal = mode->VSyncEnd + var->upper_margin;

    mode->Flags = 0;

    xf86SetModeCrtc(mode, 0);

    if (rotation) {
        switch (var->rotate) {
        case FB_ROTATE_UR:
            *rotation = RR_Rotate_0;
            break;
        case FB_ROTATE_CW:
            *rotation = RR_Rotate_90;
            break;
        case FB_ROTATE_UD:
            *rotation = RR_Rotate_180;
            break;
        case FB_ROTATE_CCW:
            *rotation = RR_Rotate_270;
            break;
        }
    }
}

void
GlamoOutputInit(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    xf86OutputPtr output;
    GlamoOutputPtr pGlamoOutput;
    DisplayModePtr mode;

    output = xf86OutputCreate(pScrn, &glamo_output_funcs, "LCD");
    if (!output)
        return;

    output->possible_crtcs = 1;
    output->possible_clones = 0;

    pGlamoOutput = (GlamoOutputPtr)xnfalloc(sizeof(GlamoOutputRec));
    /* The code will still work if pGlamoOutput is not present, there will just
     * be no builtin modes */
    if (!pGlamoOutput) {
        output->driver_private = NULL;
        return;
    }
    output->driver_private = pGlamoOutput;
    pGlamoOutput->modes = NULL;

    mode = xnfcalloc(1, sizeof(DisplayModeRec));
    if (!mode)
        return;

    mode->next = NULL;
    mode->prev = NULL;

    ConvertModeFbToXfree(&pGlamo->fb_var, mode, NULL);
    xf86SetModeDefaultName(mode);

    mode->type = M_T_PREFERRED | M_T_DRIVER;
    pGlamoOutput->modes = xf86ModesAdd(pGlamoOutput->modes, mode);


    /* This is a really really dirty hack. It assumes a configuration like on
     * the freerunner. It would be much better if there was a way to query the
     * framebuffer driver for all valid modes. */
    mode = xf86DuplicateMode(mode);
    if (!mode)
        return;


    if (mode->VDisplay <= 320) {
        mode->HSyncStart = mode->HDisplay * 2 + (mode->HDisplay - mode->HSyncStart);
        mode->HSyncEnd   = mode->HDisplay * 2 + (mode->HDisplay - mode->HSyncEnd);
        mode->HTotal     = mode->HDisplay * 2 + (mode->HDisplay - mode->HTotal);
        mode->HDisplay   *= 2;
        mode->HSyncStart = mode->VDisplay * 2 + (mode->VSyncStart - mode->HDisplay);
        mode->HSyncEnd   = mode->VDisplay * 2 + (mode->VSyncEnd - mode->HDisplay);
        mode->HTotal     = mode->VDisplay * 2 + (mode->VTotal - mode->HDisplay);
        mode->VDisplay   *= 2;
    } else {
        mode->HSyncStart = mode->HDisplay / 2 + (mode->HDisplay - mode->HSyncStart);
        mode->HSyncEnd   = mode->HDisplay / 2 + (mode->HDisplay - mode->HSyncEnd);
        mode->HTotal     = mode->HDisplay / 2 + (mode->HDisplay - mode->HTotal);
        mode->HDisplay   /= 2;
        mode->HSyncStart = mode->VDisplay / 2 + (mode->VSyncStart - mode->HDisplay);
        mode->HSyncEnd   = mode->VDisplay / 2 + (mode->VSyncEnd - mode->HDisplay);
        mode->HTotal     = mode->VDisplay / 2 + (mode->VTotal - mode->HDisplay);
        mode->VDisplay   /= 2;
    }

    xf86SetModeCrtc(mode, 0);
    xf86SetModeDefaultName(mode);
    mode->type = M_T_DRIVER;

    pGlamoOutput->modes = xf86ModesAdd(pGlamoOutput->modes, mode);
}

static xf86OutputStatus
GlamoOutputDetect(xf86OutputPtr output) {
    return XF86OutputStatusConnected;
}

static int
GlamoOutputModeValid(xf86OutputPtr output, DisplayModePtr mode) {
    return MODE_OK;
    /*return fbdevHWValidMode(output->scrn->scrnIndex, mode, FALSE, 0);*/
}

static Bool
GlamoOutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
                  DisplayModePtr mode_adjusted) {
    return TRUE;
}

static void
GlamoOutputPrepare(xf86OutputPtr output) {
}

static void
GlamoOutputModeSet(xf86OutputPtr output, DisplayModePtr mode,
                  DisplayModePtr adjusted_mode) {
}

static void
GlamoOutputCommit(xf86OutputPtr output) {
#ifdef JBT6K74_SET_STATE
    GlamoPtr pGlamo = GlamoPTR(output->scrn);
    int fd = open(pGlamo->jbt6k74_state_path, O_WRONLY);
    if (fd != -1) {
        if(output->crtc->mode.HDisplay == 240 && output->crtc->mode.VDisplay == 320)
            write(fd, jbt6k74_state_qvga, sizeof(jbt6k74_state_qvga));
        else
            write(fd, jbt6k74_state_vga, sizeof(jbt6k74_state_vga));
        close(fd);
    } else {
        xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                   "Couldn't open \"%s\" to change display resolution: %s\n",
                   pGlamo->jbt6k74_state_path, strerror(errno));
    }
#endif
}

static void GlamoOutputDestroy(xf86OutputPtr output) {
    GlamoOutputPtr pGlamoOutput = output->driver_private;
    while (pGlamoOutput->modes)
        xf86DeleteMode(&pGlamoOutput->modes, pGlamoOutput->modes);
    free(pGlamoOutput);
}

static DisplayModePtr GlamoOutputGetModes(xf86OutputPtr output) {
    GlamoPtr pGlamo = GlamoPTR(output->scrn);
    GlamoOutputPtr pGlamoOutput = output->driver_private;

    output->mm_width = pGlamo->fb_var.width;
    output->mm_height = pGlamo->fb_var.height;
    if (pGlamoOutput)
        return xf86DuplicateModes(NULL, pGlamoOutput->modes);
    return NULL;
}

