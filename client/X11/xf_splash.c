/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * X11 RemoteApp launch splash
 *
 * Copyright 2026 FreeRDP contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <winpr/string.h>
#include <winpr/assert.h>

#include <freerdp/settings.h>

#include "xfreerdp.h"
#include "xf_splash.h"
#include "xf_utils.h"

#include <freerdp/log.h>
#define TAG CLIENT_TAG("x11.splash")

#define XF_SPLASH_WIDTH 420
#define XF_SPLASH_HEIGHT 120
#define XF_SPLASH_PAD 24

struct xf_splash
{
	Window handle;
	GC gc;
	XFontSet fontSet;
	int width;
	int height;
	unsigned long bg;
	unsigned long fg;
	unsigned long accent;
	char* message;
};

static unsigned long xf_splash_color(xfContext* xfc, const char* rgb)
{
	XColor color = { 0 };

	WINPR_ASSERT(xfc);
	Colormap cmap = DefaultColormap(xfc->display, xfc->screen_number);
	if (!XParseColor(xfc->display, cmap, rgb, &color))
		return BlackPixelOfScreen(xfc->screen);
	if (!XAllocColor(xfc->display, cmap, &color))
		return BlackPixelOfScreen(xfc->screen);
	return color.pixel;
}

static char* xf_splash_format_message(const char* appName)
{
	/* Keep the wording short; the application name may already include a path. */
	const char* prefix = "正在打开应用";
	const char* fallback = "正在打开远程应用...";

	if (!appName || (strnlen(appName, 2) == 0))
		return _strdup(fallback);

	/* Use only the trailing component of a path-like program string. */
	const char* base = appName;
	for (const char* p = appName; *p; p++)
	{
		if ((*p == '\\') || (*p == '/'))
			base = p + 1;
	}
	if (strnlen(base, 1) == 0)
		base = appName;

	size_t len = strlen(prefix) + strlen(base) + 8;
	char* msg = (char*)calloc(1, len);
	if (!msg)
		return nullptr;
	(void)_snprintf(msg, len, "%s %s", prefix, base);
	return msg;
}

static void xf_splash_draw(xfContext* xfc)
{
	xfSplash* splash = xfc ? xfc->splash : nullptr;
	if (!splash || !splash->handle)
		return;

	Display* display = xfc->display;

	/* Background */
	LogDynAndXSetForeground(xfc->log, display, splash->gc, splash->bg);
	LogDynAndXFillRectangle(xfc->log, display, splash->handle, splash->gc, 0, 0,
	                        WINPR_ASSERTING_INT_CAST(uint32_t, splash->width),
	                        WINPR_ASSERTING_INT_CAST(uint32_t, splash->height));

	/* Accent bar at the top for a bit of visual structure */
	LogDynAndXSetForeground(xfc->log, display, splash->gc, splash->accent);
	LogDynAndXFillRectangle(xfc->log, display, splash->handle, splash->gc, 0, 0,
	                        WINPR_ASSERTING_INT_CAST(uint32_t, splash->width), 4);

	/* Message text, vertically centered */
	LogDynAndXSetForeground(xfc->log, display, splash->gc, splash->fg);
	if (splash->fontSet && splash->message)
	{
		XRectangle ink = { 0 };
		XRectangle logical = { 0 };
		Xutf8TextExtents(splash->fontSet, splash->message, (int)strlen(splash->message), &ink,
		                 &logical);

		int tx = (splash->width - logical.width) / 2;
		if (tx < XF_SPLASH_PAD)
			tx = XF_SPLASH_PAD;
		int ty = (splash->height + logical.height) / 2 - 4;

		Xutf8DrawString(display, splash->handle, splash->fontSet, splash->gc, tx, ty,
		                splash->message, (int)strlen(splash->message));
	}
	LogDynAndXFlush(xfc->log, display);
}

BOOL xf_splash_show(xfContext* xfc, const char* appName)
{
	WINPR_ASSERT(xfc);

	if (!xfc->display || !xfc->screen)
		return FALSE;

	/* Already showing: just refresh the message. */
	if (xfc->splash)
	{
		char* msg = xf_splash_format_message(appName);
		if (msg)
		{
			free(xfc->splash->message);
			xfc->splash->message = msg;
		}
		xf_splash_draw(xfc);
		return TRUE;
	}

	xfSplash* splash = (xfSplash*)calloc(1, sizeof(xfSplash));
	if (!splash)
		return FALSE;

	splash->width = XF_SPLASH_WIDTH;
	splash->height = XF_SPLASH_HEIGHT;
	splash->message = xf_splash_format_message(appName);
	splash->bg = xf_splash_color(xfc, "rgb:24/28/30");
	splash->fg = xf_splash_color(xfc, "rgb:f0/f0/f0");
	splash->accent = xf_splash_color(xfc, "rgb:3d/8b/fd");

	Window root = RootWindowOfScreen(xfc->screen);
	const int sw = WidthOfScreen(xfc->screen);
	const int sh = HeightOfScreen(xfc->screen);
	const int px = (sw - splash->width) / 2;
	const int py = (sh - splash->height) / 2;

	XSetWindowAttributes attrs = { 0 };
	attrs.background_pixel = splash->bg;
	attrs.border_pixel = splash->accent;
	attrs.override_redirect = True; /* no WM decorations, stays centered */
	attrs.event_mask = ExposureMask;

	splash->handle = LogDynAndXCreateWindow(
	    xfc->log, xfc->display, root, px, py, WINPR_ASSERTING_INT_CAST(uint32_t, splash->width),
	    WINPR_ASSERTING_INT_CAST(uint32_t, splash->height), 1, CopyFromParent, InputOutput,
	    CopyFromParent, CWBackPixel | CWBorderPixel | CWOverrideRedirect | CWEventMask, &attrs);

	if (!splash->handle)
	{
		free(splash->message);
		free(splash);
		return FALSE;
	}

	{
		char** missingList = nullptr;
		int missingCount = 0;
		char* defString = nullptr;
		splash->fontSet = XCreateFontSet(xfc->display, "-*-*-medium-r-normal-*-18-*-*-*-*-*-*-*",
		                                 &missingList, &missingCount, &defString);
		if (!splash->fontSet)
		{
			/* Fall back to any available font set so CJK still renders. */
			splash->fontSet = XCreateFontSet(xfc->display, "-*-*-*-*-*-*-*-*-*-*-*-*-*-*",
			                                 &missingList, &missingCount, &defString);
		}
		if (missingList)
			XFreeStringList(missingList);
	}

	XGCValues gcv = { 0 };
	splash->gc = LogDynAndXCreateGC(xfc->log, xfc->display, splash->handle, 0, &gcv);

	/* Give the window a sensible name (some compositors still show it). */
	XStoreName(xfc->display, splash->handle, "RemoteApp");

	xfc->splash = splash;

	LogDynAndXMapWindow(xfc->log, xfc->display, splash->handle);
	LogDynAndXRaiseWindow(xfc->log, xfc->display, splash->handle);
	xf_splash_draw(xfc);
	return TRUE;
}

void xf_splash_hide(xfContext* xfc)
{
	if (!xfc || !xfc->splash)
		return;

	xfSplash* splash = xfc->splash;
	xfc->splash = nullptr;

	if (splash->gc)
		LogDynAndXFreeGC(xfc->log, xfc->display, splash->gc);
	if (splash->fontSet)
		XFreeFontSet(xfc->display, splash->fontSet);
	if (splash->handle)
		LogDynAndXDestroyWindow(xfc->log, xfc->display, splash->handle);
	LogDynAndXFlush(xfc->log, xfc->display);

	free(splash->message);
	free(splash);
}

BOOL xf_splash_is_window(xfContext* xfc, Window window)
{
	if (!xfc || !xfc->splash || !window)
		return FALSE;
	return xfc->splash->handle == window;
}

BOOL xf_splash_active(xfContext* xfc)
{
	return (xfc != nullptr) && (xfc->splash != nullptr);
}

void xf_splash_raise(xfContext* xfc)
{
	if (!xfc || !xfc->splash || !xfc->splash->handle)
		return;

	/* Keep the splash mapped and on top so the remote session sign-in / desktop
	 * never becomes visible before the real application window appears. */
	LogDynAndXMapWindow(xfc->log, xfc->display, xfc->splash->handle);
	LogDynAndXRaiseWindow(xfc->log, xfc->display, xfc->splash->handle);
	xf_splash_draw(xfc);
}

void xf_splash_handle_expose(xfContext* xfc)
{
	xf_splash_draw(xfc);
}
