/* Texas Instruments OMAP framebuffer driver for X.Org
 * Copyright 2008 Kalle Vahlman, <zuh@iki.fi>
 *
 * The driver setup in this file is adapted from the fbdev driver
 * Original authors of the fbdev driver:
 *           Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	     Michel Dänzer, <michel@tungstengraphics.com>
 *
 * The OMAPFB parts are heavily influenced by the KDrive OMAP driver,
 * copyright © 2006 Nokia Corporation
 *
 * Permission to use, copy, modify, distribute and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the authors and/or copyright holders
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors and
 * copyright holders make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without any express
 * or implied warranty.
 *
 * THE AUTHORS AND COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSlib.h"

#include "micmap.h"
#include "fb.h"

#include "exa.h"

#ifdef HAVE_XEXTPROTO_71
#include <X11/extensions/dpmsconst.h>
#else
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif

#include <linux/fb.h>

/* TODO: we'd like this to come from kernel headers, but that's not a good
 * dependancy...
 */
#include "omapfb.h"

#include "omapfb-driver.h"

#define OMAPFB_VERSION 1000
#define OMAPFB_DRIVER_NAME "OMAPFB"
#define OMAPFB_NAME "omapfb"

static Bool OMAPFBProbe(DriverPtr drv, int flags);
static Bool OMAPFBPreInit(ScrnInfoPtr pScrn, int flags);
static Bool OMAPFBScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv);
static Bool OMAPFBEnterVT(int scrnIndex, int flags);
static void OMAPFBLeaveVT(int scrnIndex, int flags);
static Bool OMAPFBSaveScreen(ScreenPtr pScreen, int mode);
static Bool OMAPFBSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
static void OMAPFBDPMSSet(ScrnInfoPtr pScrn, int mode, int flags);

static void setup_default_mode(OMAPFBPtr ofb);
static Bool set_mode(OMAPFBPtr ofb, DisplayModePtr mode);

static Bool
OMAPFBEnsureRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;
	
	pScrn->driverPrivate = xnfcalloc(sizeof(OMAPFBRec), 1);
	return TRUE;
}

static void
OMAPFBFreeRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate == NULL)
		return;
	free(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

/*** General driver section */

static SymTabRec OMAPFBChipsets[] = {
    { 0, "omap1/2/3" },
    { 1, "S1D13745" },
    { 2, "HWA742" },
    { -1, NULL }
};

typedef enum {
	OPTION_ACCELMETHOD,
	OPTION_FB,
} FBDevOpts;

static const OptionInfoRec OMAPFBOptions[] = {
	{ OPTION_ACCELMETHOD,	"AccelMethod",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_FB,		"fb",		OPTV_STRING,	{0},	FALSE },
	{ -1,			NULL,		OPTV_NONE,	{0},	FALSE }
};

static const OptionInfoRec *
OMAPFBAvailableOptions(int chipid, int busid)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
	return OMAPFBOptions;
}

static void
OMAPFBIdentify(int flags)
{
	xf86PrintChipsets(OMAPFB_NAME,
	                  "Driver for OMAP framebuffer (omapfb) "
	                  "and external LCD controllers",
	                  OMAPFBChipsets);
}

static void
OMAPFBProbeController(char *ctrl_name)
{
	int fd;
	Bool found = FALSE;

/* FIXME: fetch this from hal? */
#define SYSFS_LCTRL_FILE "/sys/devices/platform/omapfb/ctrl/name"

	/* Try to read the LCD controller name */
	fd = open(SYSFS_LCTRL_FILE, O_RDONLY, 0);
	if (fd == -1) {
		xf86Msg(X_WARNING, "Error opening %s: %s\n",
		        SYSFS_LCTRL_FILE, strerror(errno));
	} else {
		int s = read(fd, ctrl_name, 31);
		if (s > 0) {
			ctrl_name[s-1] = '\0';
			found = TRUE;
		} else {
			xf86Msg(X_WARNING, "Error reading from %s: %s\n",
				SYSFS_LCTRL_FILE, strerror(errno));
		}
		close(fd);
	}

	/* Fall back to "internal" as controller */
	if (!found) {
		xf86Msg(X_WARNING,
			"Can't autodetect LCD controller, assuming internal\n");
		strcpy(ctrl_name, "internal");
	}

	xf86Msg(X_INFO, "LCD controller: %s\n", ctrl_name);
}

static Bool
OMAPFBProbe(DriverPtr drv, int flags)
{
	int i;
       	GDevPtr *devSections;
	int numDevSections;
	int bus,device,func;
	char *dev;
	ScrnInfoPtr pScrn = NULL;
	Bool foundScreen = FALSE;

	if (flags & PROBE_DETECT) return FALSE;

	/* Search for device sections for us */
	if ((numDevSections = xf86MatchDevice(OMAPFB_NAME, &devSections)) <= 0) 
		return FALSE;

/* FIXME: We don't really want to do it like this... */
#define DEFAULT_DEVICE "/dev/fb"

	for (i = 0; i < numDevSections; i++) {
		int fd;
		
		/* Fetch the device path */
		dev = xf86FindOptionValue(devSections[i]->options, "fb");

		/* Try opening it to see if we can access it */
		fd = open(dev != NULL ? dev : DEFAULT_DEVICE, O_RDWR, 0);
		if (fd > 0) {
			int entity;
			struct fb_fix_screeninfo info;

			if (ioctl (fd, FBIOGET_FSCREENINFO, &info)) {
				xf86Msg(X_WARNING,
				        "%s: Reading hardware info failed: %s\n",
				        __FUNCTION__, strerror(errno));
				close(fd);
				continue;
			}
			close(fd);

			/* We only check that the platform driver is correct
			 * here, detecting LCD controller and other capabilities
			 * are probed for in PreInit
			 */
			if (strcmp(info.id, "omapfb") &&
				strcmp(info.id, "omap24xxfb")) {
				xf86Msg(X_WARNING,
				        "%s: Not an omapfb device: %s\n",
				        __FUNCTION__, info.id);
				continue;
			}

			foundScreen = TRUE;

			/* Tell the rest of the drivers that this one is ours */
			entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
			pScrn = xf86ConfigFbEntity(pScrn, 0, entity,
			                           NULL, NULL, NULL, NULL);

			pScrn->driverVersion = OMAPFB_VERSION;
			pScrn->driverName    = OMAPFB_NAME;
			pScrn->name          = OMAPFB_NAME;
			pScrn->Probe         = OMAPFBProbe;
			pScrn->PreInit       = OMAPFBPreInit;
			pScrn->ScreenInit    = OMAPFBScreenInit;
			pScrn->SwitchMode    = OMAPFBSwitchMode;
			pScrn->EnterVT       = OMAPFBEnterVT;
			pScrn->LeaveVT       = OMAPFBLeaveVT;

		} else {
			xf86Msg(X_WARNING, "Could not open '%s': %s",
			        dev ? dev : DEFAULT_DEVICE, strerror(errno));
		}

	}

	free(devSections);

	return foundScreen;
}

static Bool
OMAPFBPreInit(ScrnInfoPtr pScrn, int flags)
{
	OMAPFBPtr ofb;
	EntityInfoPtr pEnt;
	int fd;
	char *dev;
	rgb zeros = { 0, 0, 0 };
	struct omapfb_caps caps;
	char ctrl_name[32];

	if (flags & PROBE_DETECT) return FALSE;
	
	/* We only support single entity */
	if (pScrn->numEntities != 1)
		return FALSE;
	
	/* Setup the configured monitor */
	pScrn->monitor = pScrn->confScreen->monitor;
	
	/* Get our private data */
	OMAPFBEnsureRec(pScrn);
	ofb = OMAPFB(pScrn);

	pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
	
	/* Open the device node */
	dev = xf86FindOptionValue(pEnt->device->options, "fb");
	ofb->fd = open(dev != NULL ? dev : DEFAULT_DEVICE, O_RDWR, 0);
	if (ofb->fd == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "%s: Opening '%s' failed: %s\n", __FUNCTION__,
		           dev != NULL ? dev : DEFAULT_DEVICE, strerror(errno));
		OMAPFBFreeRec(pScrn);
		return FALSE;
	}

	if (ioctl (ofb->fd, FBIOGET_FSCREENINFO, &ofb->fixed_info)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "%s: Reading hardware info failed: %s\n",
		           __FUNCTION__, strerror(errno));
		OMAPFBFreeRec(pScrn);
		return FALSE;
	}

	/* Try to detect what LCD controller we're using */
	OMAPFBProbeController(ofb->ctrl_name);

	/* Print out capabilities, if available */
	if (!ioctl (fd, OMAPFB_GET_CAPS, &caps)) {
		OMAPFBPrintCapabilities(pScrn, &caps,
		                        "Base plane");
	}

	/* Check the memory setup. */
	if (ioctl (ofb->fd, OMAPFB_QUERY_MEM, &ofb->mem_info)) {
		/* As a fallback, set up the mem_info struct from info we know */
		ofb->mem_info.type = OMAPFB_MEMTYPE_SDRAM;
		ofb->mem_info.size = ofb->fixed_info.smem_len;
	}

	pScrn->videoRam  = ofb->fixed_info.smem_len;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "VideoRAM: %iKiB (%s)\n",
	           pScrn->videoRam/1024,
	           ofb->mem_info.type == OMAPFB_MEMTYPE_SDRAM ? "SDRAM" : "SRAM");

	if (ioctl (ofb->fd, FBIOGET_VSCREENINFO, &ofb->state_info)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "%s: Reading screen state info failed: %s\n",
		           __FUNCTION__, strerror(errno));
		OMAPFBFreeRec(pScrn);
		return FALSE;
	}
	
	if (!xf86SetDepthBpp(pScrn,
			     0, /* depth: overall server default */
			     ofb->state_info.bits_per_pixel,
			     ofb->state_info.bits_per_pixel,
			     0))
		return FALSE;

	xf86PrintDepthBpp(pScrn);

	/* This apparently sets the color weights. We're feeding it zeros. */
	if (!xf86SetWeight(pScrn, zeros, zeros)) {
		return FALSE;
	}

	/* Initialize default visual */
	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;

/* FIXME: We should allow options for things like overlay framebuffers,
          rotation, etc
	xf86CollectOptions(pScrn, NULL);
*/

	pScrn->progClock = TRUE;
	pScrn->chipset   = "omapfb";
	
	/* Start with configured virtual size */
	pScrn->virtualX = pScrn->display->virtualX;
	pScrn->virtualY = pScrn->display->virtualY;
	pScrn->displayWidth = ofb->state_info.xres;

	/* Clamp to actual resolution */
	if (pScrn->virtualX < ofb->state_info.xres)
		pScrn->virtualX = ofb->state_info.xres;
	if (pScrn->virtualY < ofb->state_info.yres)
		pScrn->virtualY = ofb->state_info.yres;
	
	/* Setup viewport */
	pScrn->frameX0 = 0;
	pScrn->frameY0 = 0;
	pScrn->frameX1 = ofb->state_info.xres;
	pScrn->frameY1 = ofb->state_info.yres;

	pScrn->maxVValue = ofb->state_info.xres;
	pScrn->maxHValue = ofb->state_info.yres;

	/* Setup default mode as the mode the fb is in on startup, *usually*
	 * it'll be the one we want anyway
	 */
	setup_default_mode(ofb);

	/* TODO: Grab all provided modes from config and validate them */
	ofb->default_mode.status = xf86CheckModeForMonitor(&ofb->default_mode, pScrn->monitor);

	/* TODO: Add all validated modes here */
	pScrn->modes = &ofb->default_mode;
	pScrn->currentMode = pScrn->modes;

	pScrn->offset.red   = ofb->state_info.red.offset;
	pScrn->offset.green = ofb->state_info.green.offset;
	pScrn->offset.blue  = ofb->state_info.blue.offset;
	pScrn->mask.red     = ((1 << ofb->state_info.red.length) - 1)
	                          << ofb->state_info.red.offset;
	pScrn->mask.green   = ((1 << ofb->state_info.green.length) - 1)
	                          << ofb->state_info.green.offset;
	pScrn->mask.blue    = ((1 << ofb->state_info.blue.length) - 1)
	                          << ofb->state_info.blue.offset;
	
	xf86PrintModes(pScrn);
	
	/* Set the screen dpi value (we don't give defaults) */
	xf86SetDpi(pScrn, 0, 0);

	return TRUE;
}

static void
OMAPFBXvScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	XF86VideoAdaptorPtr *ptr = NULL;
	XF86VideoAdaptorPtr *omap_adaptors = NULL;
	int on = 0;

	int n = xf86XVListGenericAdaptors(pScrn, &ptr);

	/* Get the omap adaptors */
	on = OMAPFBXVInit(pScrn, &omap_adaptors);

	/* Merge the adaptor lists */
	if (n > 0 || on > 0) {
		int i;
		XF86VideoAdaptorPtr *generic_adaptors = ptr;
		ptr = malloc((n + on) * sizeof(XF86VideoAdaptorPtr));
		for (i = 0; i < n; i++) {
			ptr[i] = generic_adaptors[i];
		}
		for (i = n; i < on; i++) {
			ptr[i] = omap_adaptors[i-n];
		}
		n = n + on;
	}

	if (n == 0 || !xf86XVScreenInit(pScreen, ptr, n)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "XVScreenInit failed\n");
		return;
	}
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "XVideo extension initialized\n");
}

static Bool
OMAPFBCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPFBPtr ofb = OMAPFB(pScrn);

	munmap(ofb->fb, ofb->mem_info.size);

	pScreen->CloseScreen = ofb->CloseScreen;
	
	return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

static Bool
OMAPFBScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPFBPtr ofb = OMAPFB(pScrn);

	ofb->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = OMAPFBCloseScreen;

	/* Map our framebuffer memory */
	ofb->fb = mmap (NULL, ofb->mem_info.size,
	                PROT_READ | PROT_WRITE, MAP_SHARED,
	                ofb->fd, 0);
	if (ofb->fb == NULL) {
		xf86DrvMsg(scrnIndex, X_ERROR, "Mapping framebuffer memory failed\n");
		return FALSE;
	}

	/* Reset visuals */
	miClearVisualTypes();

	/* Only support TrueColor for now */
	if (!miSetVisualTypes(pScrn->depth, TrueColorMask,
		pScrn->rgbBits, pScrn->defaultVisual)) {
		xf86DrvMsg(scrnIndex, X_ERROR, "visual type setup failed"
		           " for %d bits per pixel [1]\n",
		           pScrn->bitsPerPixel);
		return FALSE;
	}

	/* Set up pixmap depth information */
	if (!miSetPixmapDepths()) {
		xf86DrvMsg(scrnIndex,X_ERROR,"pixmap depth setup failed\n");
		return FALSE;
	}

	/* Load the fallback module */
	xf86LoadSubModule(pScrn, "fb");

	/* Initialize fallbacks for the screen */
	if (!fbScreenInit(pScreen, ofb->fb, pScrn->virtualX,
	                  pScrn->virtualY, pScrn->xDpi,
	                  pScrn->yDpi, pScrn->displayWidth,
	                  pScrn->bitsPerPixel)) {
		xf86DrvMsg(scrnIndex, X_ERROR, "fbScreenInit failed\n");
		return FALSE;
	}

	/* Setup visual RGB properties */
	if (pScrn->bitsPerPixel > 8) {
		VisualPtr visual = pScreen->visuals + pScreen->numVisuals;
		while (--visual >= pScreen->visuals) {
			if ((visual->class | DynamicClass) == DirectColor) {
				visual->offsetRed = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue = pScrn->offset.blue;
				visual->redMask = pScrn->mask.red;
				visual->greenMask = pScrn->mask.green;
				visual->blueMask = pScrn->mask.blue;
			}
		}
	}

	/* Initialize XRender fallbacks */
	if (!fbPictureInit(pScreen, NULL, 0)) {
		xf86DrvMsg(scrnIndex, X_ERROR, "fbPictureInit failed\n");
		return FALSE;
	}
	
	/* Setup default colors */
	xf86SetBlackWhitePixels(pScreen);
	
	/* Initialize software cursor */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	/* Initialize default colormap */
	if (!miCreateDefColormap(pScreen)) {
		xf86DrvMsg(scrnIndex, X_ERROR,
		           "creating default colormap failed\n");
		return FALSE;
	}

	/* Enforce the default mode (this is silly I guess) */
	set_mode(ofb, &ofb->default_mode);

	/* Make sure the plane is up and running */
	if (ioctl (ofb->fd, OMAPFB_QUERY_PLANE, &ofb->plane_info)) {
		/* This is non-fatal since we might be running against older
		 * kernel driver in which case we only do basic 2D stuff...
		 */
		xf86DrvMsg(scrnIndex, X_ERROR, "Reading plane info failed\n");
	} else {

		ofb->plane_info.enabled = 1;
		ofb->plane_info.out_width = ofb->state_info.xres;
		ofb->plane_info.out_height = ofb->state_info.yres;

		if (ioctl (ofb->fd, OMAPFB_SETUP_PLANE, &ofb->plane_info)) {
			xf86DrvMsg(scrnIndex, X_ERROR,
			            "%s: Plane setup failed: %s\n",
			            __FUNCTION__, strerror(errno));
			return FALSE;
		}
	}

	if (ioctl(ofb->fd, FBIOBLANK, (void *)VESA_NO_BLANKING)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "FBIOBLANK: %s\n", strerror(errno));
	}

	/* NO-OP currently */
	pScreen->SaveScreen = OMAPFBSaveScreen;
	
	/* Setup DPMS support */
	xf86DPMSInit(pScreen, OMAPFBDPMSSet, 0);
	
#ifdef USE_EXA
	/* EXA init */
	xf86LoadSubModule(pScrn, "exa");

	/* TODO: This should depend on the AccelMethod option */
	ofb->exa = exaDriverAlloc();
	if (OMAPFBSetupExa(ofb)) {
		exaDriverInit(pScreen, ofb->exa);
	} else {
		free(ofb->exa);
		ofb->exa = NULL;
	}
#endif

	/* Initialize XVideo support */
	OMAPFBXvScreenInit(pScreen);
	
	/* TODO: RANDR support */
	
	return TRUE;
}

/* Adapted from fbdevhw.c in X.Org server sources */
static Bool
set_mode(OMAPFBPtr ofb, DisplayModePtr mode)
{
#ifdef ENFORCE_MODES
	struct fb_var_screeninfo var;
	
	var = ofb->state_info;

	var.xres = mode->HDisplay;
	var.yres = mode->VDisplay;
	if (var.xres_virtual < var.xres)
		var.xres_virtual = var.xres;
	if (var.yres_virtual < var.yres)
		var.yres_virtual = var.yres;
	var.xoffset = var.yoffset = 0;
	var.pixclock = mode->Clock ? 1000000000/mode->Clock : 0;
	var.right_margin = mode->HSyncStart - mode->HDisplay;
	var.hsync_len = mode->HSyncEnd - mode->HSyncStart;
	var.left_margin = mode->HTotal - mode->HSyncEnd;
	var.lower_margin = mode->VSyncStart - mode->VDisplay;
	var.vsync_len = mode->VSyncEnd - mode->VSyncStart;
	var.upper_margin = mode->VTotal - mode->VSyncEnd;
	var.sync = 0;
	var.bits_per_pixel = 16;
	var.grayscale = 0;
	var.nonstd = 0;
	var.accel_flags = 0;
	var.activate = FB_ACTIVATE_NOW;
	var.rotate = FB_ROTATE_UR;
	var.vmode = FB_VMODE_NONINTERLACED;

	if (ioctl (ofb->fd, FBIOPUT_VSCREENINFO, &var)) {
		return FALSE;
	}

	if (ioctl (ofb->fd, FBIOGET_VSCREENINFO, &ofb->state_info)) {
		return FALSE;
	}
#endif

	return TRUE;
}

/* Adapted from fbdevhw.c in X.Org server sources */
static void
setup_default_mode(OMAPFBPtr ofb)
{
	ofb->default_mode.name = "current";
	ofb->default_mode.next = &ofb->default_mode;
	ofb->default_mode.prev = &ofb->default_mode;
	ofb->default_mode.type |= M_T_BUILTIN;

	ofb->default_mode.HDisplay = ofb->state_info.xres;
	ofb->default_mode.HSyncStart =   ofb->default_mode.HDisplay
	                               + ofb->state_info.right_margin;
	ofb->default_mode.HSyncEnd =   ofb->default_mode.HSyncStart
	                             + ofb->state_info.hsync_len;
	ofb->default_mode.HTotal =   ofb->default_mode.HSyncEnd
	                           + ofb->state_info.left_margin;
	ofb->default_mode.VDisplay = ofb->state_info.yres;
	ofb->default_mode.VSyncStart =   ofb->default_mode.VDisplay
	                               + ofb->state_info.lower_margin;
	ofb->default_mode.VSyncEnd =   ofb->default_mode.VSyncStart
	                             + ofb->state_info.vsync_len;
	ofb->default_mode.VTotal =   ofb->default_mode.VSyncEnd
	                           + ofb->state_info.upper_margin;
	ofb->default_mode.SynthClock = ofb->state_info.pixclock
	                               ? 1000000000 / ofb->state_info.pixclock
	                               : 28000000;
	ofb->default_mode.CrtcHDisplay = ofb->default_mode.HDisplay;
	ofb->default_mode.CrtcHSyncStart = ofb->default_mode.HSyncStart;
	ofb->default_mode.CrtcHSyncEnd = ofb->default_mode.HSyncEnd;
	ofb->default_mode.CrtcHTotal = ofb->default_mode.HTotal;
	ofb->default_mode.CrtcVDisplay = ofb->default_mode.VDisplay;
	ofb->default_mode.CrtcVSyncStart = ofb->default_mode.VSyncStart;
	ofb->default_mode.CrtcVSyncEnd = ofb->default_mode.VSyncEnd;
	ofb->default_mode.CrtcVTotal = ofb->default_mode.VTotal;
	ofb->default_mode.CrtcHAdjusted = FALSE;
	ofb->default_mode.CrtcVAdjusted = FALSE;
}

static Bool OMAPFBSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	OMAPFBPtr ofb = OMAPFB(pScrn);

	if (!set_mode(ofb, mode)) {
		xf86DrvMsg(scrnIndex, X_ERROR,
		           "%s: Setting display mode failed: %s\n",
		           __FUNCTION__, strerror(errno));
		/* Restore the default mode as a fallback
		 * TODO: should we do this or does X do it for us?
		 */
		set_mode(ofb, &ofb->default_mode);
	}

	if (ioctl (ofb->fd, FBIOGET_VSCREENINFO, &ofb->state_info)) {
		xf86DrvMsg(scrnIndex, X_ERROR,
		           "%s: Reading screen state info failed: %s\n",
		           __FUNCTION__, strerror(errno));
		return FALSE;
	}

	/* TODO: If we support scaling, setup the update window to do it
	 * This needs the manual updates mode to be done.
	 */
	
	return TRUE;
}

static void
OMAPFBDPMSSet(ScrnInfoPtr pScrn, int mode, int flags)
{
	OMAPFBPtr ofb = OMAPFB(pScrn);

	switch (mode) {
		case DPMSModeOn:
			if (ioctl(ofb->fd, FBIOBLANK, (void *)VESA_NO_BLANKING)) {
				xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				           "FBIOBLANK: %s\n", strerror(errno));
			}
			break;
		case DPMSModeStandby:
		case DPMSModeSuspend:
			/* TODO: Maybe we would want to use the above modes for
			 * dimming the LCD? That'd match the functionality
			 * (save power)
			 */
		case DPMSModeOff:
			/* OMAPFB only supports on and off */
			if (ioctl(ofb->fd, FBIOBLANK, (void *)VESA_POWERDOWN)) {
				xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				           "FBIOBLANK: %s\n", strerror(errno));
			}
			break;
			break;
		default:
			return;
	}

}

void
OMAPFBPrintCapabilities(ScrnInfoPtr pScrn,
                        struct omapfb_caps *caps,
                        const char *plane_name)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "%s capabilities:\n%s%s%s%s%s%s%s%s%s",
	           plane_name,
	           (caps->ctrl & OMAPFB_CAPS_MANUAL_UPDATE) ?
	             "\tManual updates\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_TEARSYNC) ?
	             "\tTearsync\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_PLANE_RELOCATE_MEM) ?
	             "\tPlane memory relocation\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_PLANE_SCALE) ?
	             "\tPlane scaling\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_PIXEL_DOUBLE) ?
	             "\tUpdate window pixel doubling\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_SCALE) ?
	             "\tUpdate window scaling\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_OVERLAY) ?
	             "\tOverlays\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_ROTATE) ?
	             "\tRotation\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_SET_BACKLIGHT) ?
	             "\tBacklight control\n" : ""
	           );

#define MAKE_STR(f) #f
#define PRINT_FORMAT(f) (caps->plane_color & OMAPFB_COLOR_##f) ? MAKE_STR(\t##f\n) : ""

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "%s supports the following image formats:\n%s%s%s%s%s%s%s%s%s",
	           plane_name,
	           PRINT_FORMAT(RGB565),
	           PRINT_FORMAT(YUV422),
	           PRINT_FORMAT(YUV420),
	           PRINT_FORMAT(CLUT_8BPP),
	           PRINT_FORMAT(CLUT_4BPP),
	           PRINT_FORMAT(CLUT_2BPP),
	           PRINT_FORMAT(CLUT_1BPP),
	           PRINT_FORMAT(RGB444),
	           PRINT_FORMAT(YUY422)
	           );
}

/*** Unimplemented: */

static Bool
OMAPFBEnterVT(int scrnIndex, int flags)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
	return TRUE;
}

static void
OMAPFBLeaveVT(int scrnIndex, int flags)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
}

static Bool
OMAPFBSaveScreen(ScreenPtr pScreen, int mode)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s: Dim backlight?\n", __FUNCTION__);
	return TRUE;
}

static Bool
OMAPFBDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
	xorgHWFlags *flag;

	switch (op) {
		case GET_REQUIRED_HW_INTERFACES:
			flag = (CARD32*)ptr;
			(*flag) = 0;
			return TRUE;
		default:
			return FALSE;
	}
}

/*** Module and driver setup */

_X_EXPORT DriverRec OMAPFB = {
	OMAPFB_VERSION,
	OMAPFB_DRIVER_NAME,
	OMAPFBIdentify,
	OMAPFBProbe,
	OMAPFBAvailableOptions,
	NULL,
	0,
	OMAPFBDriverFunc
};

/** Module loader support */

MODULESETUPPROTO(OMAPFBSetup);

static XF86ModuleVersionInfo OMAPFBVersRec =
{
	OMAPFB_NAME,
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

_X_EXPORT XF86ModuleData omapfbModuleData = { &OMAPFBVersRec, OMAPFBSetup, NULL };

pointer
OMAPFBSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&OMAPFB, module, HaveDriverFuncs);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}


