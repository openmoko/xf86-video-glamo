/*
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *		 Michel Dänzer, <michel@tungstengraphics.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

/* all driver need this */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "mipointer.h"
#include "mibstore.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "shadow.h"

/* for visuals */
#include "fb.h"

#include "xf86RAC.h"

#include "fbdevhw.h"

#include "xf86xv.h"

#include "xf86i2c.h"
#include "xf86Modes.h"
#include "xf86Crtc.h"
#include "xf86RandR12.h"

#include "glamo.h"
#include "glamo-regs.h"
#include "glamo-kms-driver.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <sys/mman.h>


static Bool debug = 0;

#define TRACE_ENTER(str) \
    do { if (debug) ErrorF("Glamo: " str " %d\n",pScrn->scrnIndex); } while (0)
#define TRACE_EXIT(str) \
    do { if (debug) ErrorF("Glamo: " str " done\n"); } while (0)
#define TRACE(str) \
    do { if (debug) ErrorF("Glamo trace: " str "\n"); } while (0)

/* -------------------------------------------------------------------- */
/* prototypes                                                           */
static const OptionInfoRec *
GlamoAvailableOptions(int chipid, int busid);

static void
GlamoIdentify(int flags);

static Bool
GlamoProbe(DriverPtr drv, int flags);

static Bool
GlamoPreInit(ScrnInfoPtr pScrn, int flags);

static Bool
GlamoScreenInit(int Index, ScreenPtr pScreen, int argc, char **argv);

static Bool
GlamoCloseScreen(int scrnIndex, ScreenPtr pScreen);

static Bool
GlamoCrtcResize(ScrnInfoPtr scrn, int width, int height);

static Bool
GlamoInitFramebufferDevice(ScrnInfoPtr scrn, const char *fb_device);

static void
GlamoSaveHW(ScrnInfoPtr pScrn);

static void
GlamoRestoreHW(ScrnInfoPtr pScren);

static Bool
GlamoEnterVT(int scrnIndex, int flags);

static void
GlamoLeaveVT(int scrnIndex, int flags);

static void
GlamoLoadColormap(ScrnInfoPtr pScrn, int numColors, int *indices,
        LOCO *colors, VisualPtr pVisual);
 /* -------------------------------------------------------------------- */

static const xf86CrtcConfigFuncsRec glamo_crtc_config_funcs = {
    .resize = GlamoCrtcResize
};

#define GLAMO_VERSION		1000
#define GLAMO_NAME		"Glamo"
#define GLAMO_DRIVER_NAME	"Glamo"

_X_EXPORT DriverRec Glamo = {
	GLAMO_VERSION,
	GLAMO_DRIVER_NAME,
#if 0
	"driver for glamo devices",
#endif
	GlamoIdentify,
	GlamoProbe,
	GlamoAvailableOptions,
	NULL,
	0,
	NULL
};

/* Supported "chipsets" */
static SymTabRec GlamoChipsets[] = {
    { 0, "Glamo" },
    {-1, NULL }
};

/* Supported options */
typedef enum {
	OPTION_SHADOW_FB,
    OPTION_DEVICE,
	OPTION_DEBUG,
#ifdef JBT6K74_SET_STATE
    OPTION_JBT6K74_STATE_PATH
#endif
} GlamoOpts;

static const OptionInfoRec GlamoOptions[] = {
	{ OPTION_SHADOW_FB,	"ShadowFB",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_DEBUG,		"debug",	OPTV_BOOLEAN,	{0},	FALSE },
#ifdef JBT6K74_SET_STATE
	{ OPTION_JBT6K74_STATE_PATH, "StatePath", OPTV_STRING, {0}, FALSE },
#endif
	{ -1,			NULL,		OPTV_NONE,	{0},	FALSE }
};

#ifdef XFree86LOADER

MODULESETUPPROTO(GlamoSetup);

static XF86ModuleVersionInfo GlamoVersRec =
{
	"Glamo",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	NULL,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData glamoModuleData = { &GlamoVersRec, GlamoSetup, NULL };

pointer
GlamoSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&Glamo, module, 0);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

#endif /* XFree86LOADER */

Bool
GlamoGetRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;

	pScrn->driverPrivate = xnfcalloc(sizeof(GlamoRec), 1);
	return TRUE;
}

void
GlamoFreeRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate == NULL)
		return;
	xfree(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

/* -------------------------------------------------------------------- */

/* Map the mmio registers of the glamo. We can not use xf86MapVidMem since it
 * will open /dev/mem without O_SYNC. */
static Bool
GlamoMapMMIO(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    off_t base = 0x8000000;
    size_t length = 0x2400;
    int fd;
    off_t page_base = base & ~(getpagesize() - 1);
    off_t base_offset = base - page_base;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to open \"/dev/mem\": %s\n",
                   strerror(errno));
        return FALSE;
    }
    pGlamo->reg_base = (char *)mmap(NULL, length, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, page_base);

    close(fd);

    if (pGlamo->reg_base == MAP_FAILED) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to mmap mmio registers: %s\n",
                   strerror(errno));
        return FALSE;
    }

    pGlamo->reg_base += base_offset;

    return TRUE;
}

static void
GlamoUnmapMMIO(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    size_t length = 0x2400;
    char *page_base = (char *)((off_t)pGlamo->reg_base & ~(getpagesize() - 1));
    size_t base_offset = page_base - pGlamo->reg_base;

   if (pGlamo->reg_base != MAP_FAILED)
        munmap(page_base, length + base_offset);
}

static Bool
GlamoSwitchMode(int scrnIndex, DisplayModePtr mode, int flags) {
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR (pScrn);
    xf86OutputPtr output = config->output[config->compat_output];
    Rotation rotation;

    if (output && output->crtc)
        rotation = output->crtc->rotation;
    else
        rotation = RR_Rotate_0;

    return xf86SetSingleMode(pScrn, mode, rotation);
}

static const OptionInfoRec *
GlamoAvailableOptions(int chipid, int busid)
{
	return GlamoOptions;
}

static void
GlamoIdentify(int flags)
{
	xf86PrintChipsets(GLAMO_NAME, "driver for glamo", GlamoChipsets);
}

static Bool
GlamoFbdevProbe(DriverPtr drv, GDevPtr *devSections, int numDevSections)
{
	char *dev;
	Bool foundScreen = FALSE;
	int i;
	ScrnInfoPtr pScrn;

	if (!xf86LoadDrvSubModule(drv, "fbdevhw")) return FALSE;

	for (i = 0; i < numDevSections; i++) {

		dev = xf86FindOptionValue(devSections[i]->options, "Device");
		if (fbdevHWProbe(NULL, dev, NULL)) {
			int entity;
			pScrn = NULL;

			entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
			pScrn = xf86ConfigFbEntity(pScrn,0,entity, NULL, NULL,
				                   NULL, NULL);

			if (pScrn) {

				foundScreen = TRUE;

				pScrn->driverVersion = GLAMO_VERSION;
				pScrn->driverName    = GLAMO_DRIVER_NAME;
				pScrn->name          = GLAMO_NAME;
				pScrn->Probe         = GlamoProbe;
				pScrn->PreInit       = GlamoPreInit;
				pScrn->ScreenInit    = GlamoScreenInit;
				pScrn->SwitchMode    = GlamoSwitchMode;
				pScrn->AdjustFrame   = fbdevHWAdjustFrameWeak();
				pScrn->EnterVT       = GlamoEnterVT;
				pScrn->LeaveVT       = GlamoLeaveVT;
				pScrn->ValidMode     = fbdevHWValidModeWeak();

				xf86DrvMsg(pScrn->scrnIndex, X_INFO,
					   "using %s\n",
					   dev ? dev : "default device\n");

			}
		}

	}

	return foundScreen;
}

static Bool
GlamoKMSProbe(DriverPtr drv, GDevPtr *devSections, int numDevSections)
{
	ScrnInfoPtr pScrn = NULL;
	int entity;
	Bool foundScreen = FALSE;
	int i;

	for ( i = 0; i < numDevSections; i++ ) {

		/* This is a little dodgy.  We aren't really using fbdevhw
		 * (/dev/fb0 is irrelevant), but we need a device entity to make
		 * the later stages of initialisation work.  xf86ClaimFbSlot()
		 * does the minimum required to make this work, so we use it
		 * despite the above. */
		entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
		pScrn = xf86ConfigFbEntity(pScrn, 0, entity, NULL, NULL, NULL,
		                           NULL);

		if ( pScrn ) {

			foundScreen = TRUE;

			/* Plug in KMS functions */
			pScrn->driverVersion = GLAMO_VERSION;
			pScrn->driverName    = GLAMO_DRIVER_NAME;
			pScrn->name          = GLAMO_NAME;
			pScrn->PreInit       = GlamoKMSPreInit;
			pScrn->ScreenInit    = GlamoKMSScreenInit;
			pScrn->SwitchMode    = GlamoKMSSwitchMode;
			pScrn->AdjustFrame   = GlamoKMSAdjustFrame;
			pScrn->EnterVT       = GlamoKMSEnterVT;
			pScrn->LeaveVT       = GlamoKMSLeaveVT;
			pScrn->ValidMode     = GlamoKMSValidMode;

		}
	}

	return foundScreen;
}

static Bool
GlamoProbe(DriverPtr drv, int flags)
{
	ScrnInfoPtr pScrn;
	GDevPtr *devSections;
	int numDevSections;
	Bool foundScreen = FALSE;

	TRACE("probe start");

	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;

	numDevSections = xf86MatchDevice(GLAMO_DRIVER_NAME, &devSections);
	if (numDevSections <= 0) return FALSE;

	/* Is today a good day to use KMS? */
	if ( GlamoKernelModesettingAvailable() ) {
		foundScreen = GlamoKMSProbe(drv, devSections, numDevSections);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using KMS!\n");
	} else {
		foundScreen = GlamoFbdevProbe(drv, devSections, numDevSections);
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Not using KMS\n");
	}

	xfree(devSections);
	TRACE("probe done");
	return foundScreen;
}

static Bool
GlamoPreInit(ScrnInfoPtr pScrn, int flags)
{
    GlamoPtr pGlamo;
    int default_depth, fbbpp;
    rgb weight_defaults = {0, 0, 0};
    Gamma gamma_defaults = {0.0, 0.0, 0.0};
    char *fb_device;

    if (flags & PROBE_DETECT)
        return FALSE;

    TRACE_ENTER("PreInit");

    /* Check the number of entities, and fail if it isn't one. */
    if (pScrn->numEntities != 1)
        return FALSE;

    pScrn->monitor = pScrn->confScreen->monitor;

    GlamoGetRec(pScrn);
    pGlamo = GlamoPTR(pScrn);

    pGlamo->accel = FALSE;

    pGlamo->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

    fb_device = xf86FindOptionValue(pGlamo->pEnt->device->options, "Device");

    /* open device */
    if (!fbdevHWInit(pScrn, NULL, fb_device))
            return FALSE;

	/* FIXME: Replace all fbdev functionality with our own code, so we only have
	 * to open the fb devic only once. */
    if (!GlamoInitFramebufferDevice(pScrn, fb_device))
        return FALSE;

    default_depth = fbdevHWGetDepth(pScrn, &fbbpp);

    if (!xf86SetDepthBpp(pScrn, default_depth, default_depth, fbbpp, 0))
        return FALSE;

    xf86PrintDepthBpp(pScrn);

	/* color weight */
    if (!xf86SetWeight(pScrn, weight_defaults, weight_defaults))
        return FALSE;

    /* visual init */
    if (!xf86SetDefaultVisual(pScrn, -1))
        return FALSE;

    /* We don't currently support DirectColor at > 8bpp */
    if (pScrn->defaultVisual != TrueColor) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "requested default visual"
			   " (%s) is not supported at depth %d\n",
			   xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
        return FALSE;
    }

    if (!xf86SetGamma(pScrn, gamma_defaults)) {
        return FALSE;
    }

    xf86CrtcConfigInit(pScrn, &glamo_crtc_config_funcs);
    xf86CrtcSetSizeRange(pScrn, 240, 320, 480, 640);
    GlamoCrtcInit(pScrn);
    GlamoOutputInit(pScrn);

    if (!xf86InitialConfiguration(pScrn, TRUE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes.\n");
        return FALSE;
    }

    pScrn->progClock = TRUE;
    pScrn->chipset   = "Glamo";
    pScrn->videoRam  = fbdevHWGetVidmem(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hardware: %s (video memory:"
		   " %dkB)\n", fbdevHWGetName(pScrn), pScrn->videoRam/1024);

    /* handle options */
    xf86CollectOptions(pScrn, NULL);
    if (!(pGlamo->Options = xalloc(sizeof(GlamoOptions))))
        return FALSE;
    memcpy(pGlamo->Options, GlamoOptions, sizeof(GlamoOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pGlamo->pEnt->device->options, pGlamo->Options);

    /* use shadow framebuffer by default */
    pGlamo->shadowFB = xf86ReturnOptValBool(pGlamo->Options, OPTION_SHADOW_FB, TRUE);

    debug = xf86ReturnOptValBool(pGlamo->Options, OPTION_DEBUG, FALSE);

#ifdef JBT6K74_SET_STATE
    pGlamo->jbt6k74_state_path = xf86GetOptValString(pGlamo->Options,
                                                     OPTION_JBT6K74_STATE_PATH);
    if (pGlamo->jbt6k74_state_path == NULL)
        pGlamo->jbt6k74_state_path = JBT6K74_STATE_PATH;
#endif

    /* First approximation, may be refined in ScreenInit */
    pScrn->displayWidth = pScrn->virtualX;

    xf86PrintModes(pScrn);

    /* Set display resolution */
    xf86SetDpi(pScrn, 0, 0);

    if (xf86LoadSubModule(pScrn, "fb") == NULL) {
        GlamoFreeRec(pScrn);
        return FALSE;
    }

    TRACE_EXIT("PreInit");
    return TRUE;
}


static Bool
GlamoScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    VisualPtr visual;
    int ret, flags;
    size_t mem_start = 640 * 480 * 2;
    size_t mem_size = 1024 * 1024 * 4 - mem_start;

    TRACE_ENTER("GlamoScreenInit");

#if DEBUG
	ErrorF("\tbitsPerPixel=%d, depth=%d, defaultVisual=%s\n"
		   "\tmask: %x,%x,%x, offset: %d,%d,%d\n",
		   pScrn->bitsPerPixel,
		   pScrn->depth,
		   xf86GetVisualName(pScrn->defaultVisual),
		   pScrn->mask.red,pScrn->mask.green,pScrn->mask.blue,
		   pScrn->offset.red,pScrn->offset.green,pScrn->offset.blue);
#endif

    if (NULL == (pGlamo->fbmem = fbdevHWMapVidmem(pScrn))) {
        xf86DrvMsg(scrnIndex, X_ERROR, "mapping of video memory failed\n");
        return FALSE;
    }

    pGlamo->fboff = fbdevHWLinearOffset(pScrn);

    fbdevHWSaveScreen(pScreen, SCREEN_SAVER_ON);

    /* mi layer */
    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
        xf86DrvMsg(scrnIndex, X_ERROR,
                   "visual type setup failed for %d bits per pixel [1]\n",
                   pScrn->bitsPerPixel);
        return FALSE;
    }
    if (!miSetPixmapDepths()) {
      xf86DrvMsg(scrnIndex, X_ERROR, "pixmap depth setup failed\n");
      return FALSE;
    }

    pScrn->displayWidth = fbdevHWGetLineLength(pScrn) /
					  (pScrn->bitsPerPixel / 8);

    pGlamo->fbstart = pGlamo->fbmem + pGlamo->fboff;

    ret = fbScreenInit(pScreen, pGlamo->fbstart, pScrn->virtualX,
                       pScrn->virtualY, pScrn->xDpi, pScrn->yDpi,
                       pScrn->displayWidth,  pScrn->bitsPerPixel);
    if (!ret)
        return FALSE;

    /* Fixup RGB ordering */
    visual = pScreen->visuals + pScreen->numVisuals;
    while (--visual >= pScreen->visuals) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed   = pScrn->offset.red;
            visual->offsetGreen = pScrn->offset.green;
            visual->offsetBlue  = pScrn->offset.blue;
            visual->redMask     = pScrn->mask.red;
            visual->greenMask   = pScrn->mask.green;
            visual->blueMask    = pScrn->mask.blue;
        }
    }

    /* must be after RGB ordering fixed */
    if (!fbPictureInit(pScreen, NULL, 0))
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Render extension initialisation failed\n");

    pGlamo->pScreen = pScreen;

    /* map in the registers */
    if (GlamoMapMMIO(pScrn)) {

        xf86LoadSubModule(pScrn, "exa");

    	if (!GLAMODrawInit(pScrn, mem_start, mem_size)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "EXA hardware acceleration initialization failed\n");
        } else {
            pGlamo->accel = TRUE;
        }
    }

    xf86SetBlackWhitePixels(pScreen);
    miInitializeBackingStore(pScreen);
    xf86SetBackingStore(pScreen);

    /* software cursor */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    GlamoEnterVT(scrnIndex, 0);

    xf86CrtcScreenInit(pScreen);
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0)
    xf86RandR12SetRotations(pScreen, RR_Rotate_0 | RR_Rotate_90 |
                                     RR_Rotate_180 | RR_Rotate_270);
#endif
    /* colormap */
    pGlamo->colormap = NULL;
    if (!miCreateDefColormap(pScreen)) {
        xf86DrvMsg(scrnIndex, X_ERROR,
                   "internal error: miCreateDefColormap failed "
                   "in GlamoScreenInit()\n");
        return FALSE;
    }

    flags = CMAP_PALETTED_TRUECOLOR;
    if (!xf86HandleColormaps(pScreen, 256, 8, GlamoLoadColormap,
                             NULL, flags))
        return FALSE;

    xf86DPMSInit(pScreen, xf86DPMSSet, 0);

    pScreen->SaveScreen = xf86SaveScreen;

    /* Wrap the current CloseScreen function */
    pGlamo->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = GlamoCloseScreen;

    TRACE_EXIT("GlamoScreenInit");

    return TRUE;
}

static Bool
GlamoCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    if (pGlamo->accel)
        GLAMODrawFini(pScrn);

    if (pScrn->vtSema)
        GlamoRestoreHW(pScrn);

    fbdevHWUnmapVidmem(pScrn);
    GlamoUnmapMMIO(pScrn);

    if (pGlamo->colormap) {
        xfree(pGlamo->colormap);
        pGlamo->colormap = NULL;
    }

    pScrn->vtSema = FALSE;

    pScreen->CreateScreenResources = pGlamo->CreateScreenResources;
    pScreen->CloseScreen = pGlamo->CloseScreen;
    return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

static Bool
GlamoCrtcResize(ScrnInfoPtr pScrn, int width, int height) {
    pScrn->virtualX = width;
    pScrn->virtualY = height;
    pScrn->displayWidth = width * (pScrn->bitsPerPixel / 8);
    pScrn->pScreen->GetScreenPixmap(pScrn->pScreen)->devKind = pScrn->displayWidth;

    return TRUE;
}


static Bool
GlamoInitFramebufferDevice(ScrnInfoPtr pScrn, const char *fb_device) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    if (fb_device) {
        pGlamo->fb_fd = open(fb_device, O_RDWR, 0);
        if (pGlamo->fb_fd == -1) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to open framebuffer device \"%s\": %s\n",
                       fb_device, strerror(errno));
            goto fail2;
        }
    } else {
        fb_device = getenv("FRAMEBUFFER");
        if (fb_device != NULL) {
            pGlamo->fb_fd = open(fb_device, O_RDWR, 0);
        if (pGlamo->fb_fd != -1)
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "Failed to open framebuffer device \"%s\": %s\n",
                       fb_device, strerror(errno));
             fb_device = NULL;
        }
        if (fb_device == NULL) {
            fb_device = "/dev/fb0";
            pGlamo->fb_fd = open(fb_device, O_RDWR, 0);
            if (pGlamo->fb_fd == -1) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "Failed to open framebuffer device \"%s\": %s",
                           fb_device, strerror(errno));
                goto fail2;
            }
        }
    }

    /* retrive current setting */
    if (ioctl(pGlamo->fb_fd, FBIOGET_FSCREENINFO, (void*)(&pGlamo->fb_fix)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOGET_FSCREENINFO failed: %s",
                   strerror(errno));
        goto fail1;
    }

    if (ioctl(pGlamo->fb_fd, FBIOGET_VSCREENINFO, (void*)(&pGlamo->fb_var)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOGET_FSCREENINFO failed: %s",
                   strerror(errno));
        goto fail1;
    }
    return TRUE;

fail1:
    close(pGlamo->fb_fd);
    pGlamo->fb_fd = -1;
fail2:
    return FALSE;
}

/* Save framebuffer setup and all the glamo registers we are going to touch */
static void
GlamoSaveHW(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);
#ifndef HAVE_ENGINE_IOCTLS
    volatile char *mmio = pGlamo->reg_base;
#endif
#if JBT6K74_SET_STATE
    int fd;

    fd = open(pGlamo->jbt6k74_state_path, O_RDONLY);
    if (fd != -1) {
       read(fd, pGlamo->saved_jbt6k74_state, 14);
        close(fd);
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Couldn't open \"%s\" to save display resolution: %s\n",
                   pGlamo->jbt6k74_state_path, strerror(errno));
    }
#endif

#ifndef HAVE_ENGINE_IOCTLS
    pGlamo->saved_clock_2d = MMIO_IN16(mmio, GLAMO_REG_CLOCK_2D);
    pGlamo->saved_clock_isp = MMIO_IN16(mmio, GLAMO_REG_CLOCK_ISP);
    pGlamo->saved_clock_gen5_1 = MMIO_IN16(mmio, GLAMO_REG_CLOCK_GEN5_1);
    pGlamo->saved_clock_gen5_2 = MMIO_IN16(mmio, GLAMO_REG_CLOCK_GEN5_2);
    pGlamo->saved_hostbus_2 = MMIO_IN16(mmio, GLAMO_REG_HOSTBUS(2));
#endif

    if (ioctl(pGlamo->fb_fd, FBIOGET_VSCREENINFO, (void*)(&pGlamo->fb_saved_var)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOGET_FSCREENINFO failed: %s",
                   strerror(errno));
    }
}

static void
GlamoRestoreHW(ScrnInfoPtr pScrn) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);
#ifndef HAVE_ENGINE_IOCTLS
    volatile char *mmio = pGlamo->reg_base;
#endif
#ifdef JBT6K74_SET_STATE
    int fd;
#endif

    if (ioctl(pGlamo->fb_fd, FBIOPUT_VSCREENINFO, (void*)(&pGlamo->fb_saved_var)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOSET_FSCREENINFO failed: %s",
                   strerror(errno));
    }

#ifndef HAVE_ENGINE_IOCTLS
    MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
        GLAMO_CLOCK_2D_EN_M6CLK | GLAMO_CLOCK_2D_EN_M7CLK |
        GLAMO_CLOCK_2D_EN_GCLK | GLAMO_CLOCK_2D_DG_M7CLK |
        GLAMO_CLOCK_2D_DG_GCLK,
        pGlamo->saved_clock_2d);
    MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
        GLAMO_CLOCK_GEN51_EN_DIV_MCLK |  GLAMO_CLOCK_GEN51_EN_DIV_GCLK,
        pGlamo->saved_clock_gen5_1);
    MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
        GLAMO_HOSTBUS2_MMIO_EN_CMDQ | GLAMO_HOSTBUS2_MMIO_EN_2D,
        pGlamo->saved_hostbus_2);
#endif

#ifdef JBT6K74_SET_STATE
    fd = open(pGlamo->jbt6k74_state_path, O_WRONLY);
    if (fd != -1) {
        write(fd, pGlamo->saved_jbt6k74_state, 14);
        close(fd);
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Couldn't open \"%s\" to restore display resolution: %s\n",
                   pGlamo->jbt6k74_state_path, strerror(errno));
    }
#endif
}

static Bool
GlamoEnterVT(int scrnIndex, int flags) {
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    GlamoSaveHW(pScrn);

    if (pGlamo->accel)
        pGlamo->accel = GLAMODrawEnable(pScrn);

    if (!xf86SetDesiredModes(pScrn))
        return FALSE;

    return TRUE;
}

static void
GlamoLeaveVT(int scrnIndex, int flags) {
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    GlamoPtr pGlamo = GlamoPTR(pScrn);

    if (pGlamo->accel)
        GLAMODrawDisable(pScrn);

    GlamoRestoreHW(pScrn);
}

static void
GlamoLoadColormap(ScrnInfoPtr pScrn, int numColors, int *indices,
        LOCO *colors, VisualPtr pVisual) {
    GlamoPtr pGlamo = GlamoPTR(pScrn);
    int i;
    ErrorF("%s:%s[%d]\n", __FILE__, __func__, __LINE__);

    if (pGlamo->colormap) {
        xfree (pGlamo->colormap);
    }

    pGlamo->colormap = xalloc (sizeof(uint16_t) * numColors);

    for (i = 0; i < numColors; ++i) {
        pGlamo->colormap[i] =
            ((colors[indices[i]].red << 8) & 0xf700) |
            ((colors[indices[i]].green << 3) & 0x7e0) |
            (colors[indices[i]].blue >> 3);
    }
}
