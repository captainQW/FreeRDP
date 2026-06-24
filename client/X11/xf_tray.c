/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * X11 RAIL notification icons (system tray) via the XEmbed System Tray Protocol
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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <winpr/crt.h>
#include <winpr/assert.h>
#include <winpr/collections.h>

#include <freerdp/log.h>
#include <freerdp/codec/color.h>
#include <freerdp/client/rail.h>

#include "xf_rail.h"
#include "xf_tray.h"
#include "xf_utils.h"
#include "xfreerdp.h"

#define TAG CLIENT_TAG("x11.tray")

/* freedesktop.org System Tray Protocol opcodes (_NET_SYSTEM_TRAY_OPCODE). */
#define SYSTEM_TRAY_REQUEST_DOCK 0
#define SYSTEM_TRAY_BEGIN_MESSAGE 1
#define SYSTEM_TRAY_CANCEL_MESSAGE 2

/* XEMBED protocol (subset used for tray icons). */
#define XEMBED_VERSION 0
#define XEMBED_MAPPED (1 << 0)
#define XEMBED_EMBEDDED_NOTIFY 0

/* Default icon side length (in pixels) requested from the tray manager. The
 * tray manager may resize us; we always repaint to the configured size. */
#define XF_TRAY_ICON_SIZE 22

typedef struct
{
	xfContext* xfc;

	UINT32 windowId;     /* owning RAIL window id      */
	UINT32 notifyIconId; /* notification icon id       */

	Window window; /* our XEmbed child window    */
	int width;
	int height;
	BOOL embedded; /* docked into a tray manager */

	/* Cached ARGB icon image (premultiplied BGRA in X server order). */
	UINT32* argb;
	int iconWidth;
	int iconHeight;

	char* tooltip; /* UTF-8 tooltip text         */
} xfTrayIcon;

struct xf_tray
{
	xfContext* xfc;

	Atom MANAGER;
	Atom NET_SYSTEM_TRAY_Sn;
	Atom NET_SYSTEM_TRAY_OPCODE;
	Atom NET_SYSTEM_TRAY_MESSAGE_DATA;
	Atom NET_SYSTEM_TRAY_ORIENTATION;
	Atom XEMBED;
	Atom XEMBED_INFO;
	Atom WM_NAME;

	Window trayManager; /* current _NET_SYSTEM_TRAY_Sn owner */
	wHashTable* icons;  /* key (windowId<<32|iconId) -> xfTrayIcon */
};
typedef struct xf_tray xfTray;

static UINT64 xf_tray_key(UINT32 windowId, UINT32 notifyIconId)
{
	return (((UINT64)windowId) << 32) | (UINT64)notifyIconId;
}

/* ----------------------------------------------------------------- */
/* tray manager discovery + XEmbed docking                            */
/* ----------------------------------------------------------------- */

static Window xf_tray_find_manager(xfTray* tray)
{
	xfContext* xfc = tray->xfc;
	Window owner = None;

	XLockDisplay(xfc->display);
	owner = XGetSelectionOwner(xfc->display, tray->NET_SYSTEM_TRAY_Sn);
	XUnlockDisplay(xfc->display);
	return owner;
}

static BOOL xf_tray_send_dock(xfTray* tray, xfTrayIcon* icon)
{
	xfContext* xfc = tray->xfc;
	XEvent ev = { 0 };

	if (tray->trayManager == None)
		return FALSE;

	ev.xclient.type = ClientMessage;
	ev.xclient.window = tray->trayManager;
	ev.xclient.message_type = tray->NET_SYSTEM_TRAY_OPCODE;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long)CurrentTime;
	ev.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
	ev.xclient.data.l[2] = (long)icon->window;
	ev.xclient.data.l[3] = 0;
	ev.xclient.data.l[4] = 0;

	XSendEvent(xfc->display, tray->trayManager, False, NoEventMask, &ev);
	XFlush(xfc->display);
	return TRUE;
}

/* Publish _XEMBED_INFO so the tray manager maps us. */
static void xf_tray_set_xembed_info(xfTray* tray, xfTrayIcon* icon)
{
	xfContext* xfc = tray->xfc;
	unsigned long info[2] = { XEMBED_VERSION, XEMBED_MAPPED };

	XChangeProperty(xfc->display, icon->window, tray->XEMBED_INFO, tray->XEMBED_INFO, 32,
	                PropModeReplace, (unsigned char*)info, 2);
}

static void xf_tray_paint_icon(xfTray* tray, xfTrayIcon* icon)
{
	xfContext* xfc = tray->xfc;
	XImage* image = NULL;
	UINT32* scaled = NULL;

	if (!icon->embedded || !icon->argb || (icon->iconWidth <= 0) || (icon->iconHeight <= 0))
		return;

	/* Nearest-neighbour scale the source icon to the actual window size. */
	scaled = (UINT32*)calloc((size_t)icon->width * icon->height, sizeof(UINT32));
	if (!scaled)
		return;

	for (int y = 0; y < icon->height; y++)
	{
		const int sy = (icon->height > 0) ? (y * icon->iconHeight / icon->height) : 0;
		for (int x = 0; x < icon->width; x++)
		{
			const int sx = (icon->width > 0) ? (x * icon->iconWidth / icon->width) : 0;
			scaled[(size_t)y * icon->width + x] =
			    icon->argb[(size_t)sy * icon->iconWidth + sx];
		}
	}

	image = XCreateImage(xfc->display, xfc->visual, (unsigned)xfc->depth, ZPixmap, 0,
	                     (char*)scaled, icon->width, icon->height, 32, 0);
	if (!image)
	{
		free(scaled);
		return;
	}

	{
		GC gc = XCreateGC(xfc->display, icon->window, 0, NULL);
		if (gc)
		{
			XPutImage(xfc->display, icon->window, gc, image, 0, 0, 0, 0, icon->width,
			          icon->height);
			XFreeGC(xfc->display, gc);
		}
	}

	/* XDestroyImage frees scaled (image->data). */
	XDestroyImage(image);
	XFlush(xfc->display);
}

/* ----------------------------------------------------------------- */
/* icon image conversion (ICON_INFO -> premultiplied BGRA)            */
/* ----------------------------------------------------------------- */

static BOOL xf_tray_convert_icon(const ICON_INFO* iconInfo, xfTrayIcon* icon)
{
	BYTE* argb = NULL;
	const UINT32 w = iconInfo->width;
	const UINT32 h = iconInfo->height;

	if ((w == 0) || (h == 0))
		return FALSE;

	argb = (BYTE*)calloc((size_t)w * h, 4);
	if (!argb)
		return FALSE;

	if (!freerdp_image_copy_from_icon_data(
	        argb, PIXEL_FORMAT_BGRA32, 0, 0, 0, WINPR_ASSERTING_INT_CAST(UINT16, w),
	        WINPR_ASSERTING_INT_CAST(UINT16, h), iconInfo->bitsColor,
	        WINPR_ASSERTING_INT_CAST(UINT16, iconInfo->cbBitsColor), iconInfo->bitsMask,
	        WINPR_ASSERTING_INT_CAST(UINT16, iconInfo->cbBitsMask), iconInfo->colorTable,
	        WINPR_ASSERTING_INT_CAST(UINT16, iconInfo->cbColorTable), iconInfo->bpp))
	{
		free(argb);
		return FALSE;
	}

	free(icon->argb);
	icon->argb = (UINT32*)argb;
	icon->iconWidth = (int)w;
	icon->iconHeight = (int)h;
	return TRUE;
}

/* ----------------------------------------------------------------- */
/* lifecycle                                                          */
/* ----------------------------------------------------------------- */

static void xf_tray_icon_free(void* value)
{
	xfTrayIcon* icon = (xfTrayIcon*)value;
	if (!icon)
		return;

	if (icon->xfc && icon->window)
	{
		XDestroyWindow(icon->xfc->display, icon->window);
		XFlush(icon->xfc->display);
	}
	free(icon->argb);
	free(icon->tooltip);
	free(icon);
}

static xfTray* xf_tray_get(xfContext* xfc)
{
	if (!xfc)
		return NULL;
	return (xfTray*)xfc->tray;
}

static xfTray* xf_tray_init(xfContext* xfc)
{
	xfTray* tray = xf_tray_get(xfc);
	char selection[64] = { 0 };

	if (tray)
		return tray;

	tray = (xfTray*)calloc(1, sizeof(xfTray));
	if (!tray)
		return NULL;

	tray->xfc = xfc;
	tray->MANAGER = Logging_XInternAtom(xfc->log, xfc->display, "MANAGER", False);
	(void)_snprintf(selection, sizeof(selection), "_NET_SYSTEM_TRAY_S%d", xfc->screen_number);
	tray->NET_SYSTEM_TRAY_Sn = Logging_XInternAtom(xfc->log, xfc->display, selection, False);
	tray->NET_SYSTEM_TRAY_OPCODE =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_SYSTEM_TRAY_OPCODE", False);
	tray->NET_SYSTEM_TRAY_MESSAGE_DATA =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_SYSTEM_TRAY_MESSAGE_DATA", False);
	tray->NET_SYSTEM_TRAY_ORIENTATION =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	tray->XEMBED = Logging_XInternAtom(xfc->log, xfc->display, "_XEMBED", False);
	tray->XEMBED_INFO = Logging_XInternAtom(xfc->log, xfc->display, "_XEMBED_INFO", False);
	tray->WM_NAME = Logging_XInternAtom(xfc->log, xfc->display, "WM_NAME", False);

	tray->icons = HashTable_New(TRUE);
	if (!tray->icons)
	{
		free(tray);
		return NULL;
	}
	{
		wObject* obj = HashTable_ValueObject(tray->icons);
		obj->fnObjectFree = xf_tray_icon_free;
	}

	tray->trayManager = xf_tray_find_manager(tray);

	/* Watch for a tray manager (re)appearing: the spec broadcasts a MANAGER
	 * client message to the root window, delivered to clients selecting
	 * StructureNotifyMask on root. Preserve any event mask other code already
	 * selected on the root window (e.g. cliprdr's PropertyChangeMask) by
	 * OR-ing instead of overwriting. */
	{
		Window root = RootWindowOfScreen(xfc->screen);
		XWindowAttributes wa = { 0 };
		long mask = StructureNotifyMask;
		if (XGetWindowAttributes(xfc->display, root, &wa))
			mask |= wa.your_event_mask;
		XSelectInput(xfc->display, root, mask);
	}

	xfc->tray = tray;
	return tray;
}

static xfTrayIcon* xf_tray_lookup(xfTray* tray, UINT32 windowId, UINT32 notifyIconId)
{
	const UINT64 key = xf_tray_key(windowId, notifyIconId);
	return (xfTrayIcon*)HashTable_GetItemValue(tray->icons, (void*)(UINT_PTR)key);
}

static xfTrayIcon* xf_tray_icon_new(xfTray* tray, UINT32 windowId, UINT32 notifyIconId)
{
	xfContext* xfc = tray->xfc;
	xfTrayIcon* icon = (xfTrayIcon*)calloc(1, sizeof(xfTrayIcon));
	XSetWindowAttributes attrs = { 0 };

	if (!icon)
		return NULL;

	icon->xfc = xfc;
	icon->windowId = windowId;
	icon->notifyIconId = notifyIconId;
	icon->width = XF_TRAY_ICON_SIZE;
	icon->height = XF_TRAY_ICON_SIZE;

	attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask;
	attrs.background_pixmap = ParentRelative; /* transparent against panel */

	icon->window = XCreateWindow(
	    xfc->display, RootWindowOfScreen(xfc->screen), 0, 0, (unsigned)icon->width,
	    (unsigned)icon->height, 0, CopyFromParent, InputOutput, CopyFromParent,
	    CWEventMask | CWBackPixmap, &attrs);

	if (!icon->window)
	{
		free(icon);
		return NULL;
	}

	xf_tray_set_xembed_info(tray, icon);

	{
		const UINT64 key = xf_tray_key(windowId, notifyIconId);
		if (!HashTable_Insert(tray->icons, (void*)(UINT_PTR)key, icon))
		{
			XDestroyWindow(xfc->display, icon->window);
			free(icon);
			return NULL;
		}
	}

	/* Request docking into the panel tray. */
	if (tray->trayManager != None)
		xf_tray_send_dock(tray, icon);

	return icon;
}

static void xf_tray_apply_state(xfTray* tray, xfTrayIcon* icon,
                                const WINDOW_ORDER_INFO* orderInfo,
                                const NOTIFY_ICON_STATE_ORDER* st)
{
	if (orderInfo->fieldFlags & WINDOW_ORDER_FIELD_NOTIFY_TIP)
	{
		const WCHAR* str = (const WCHAR*)st->toolTip.string;
		char* tip = NULL;
		if (st->toolTip.length > 0)
			tip = ConvertWCharNToUtf8Alloc(str, st->toolTip.length / sizeof(WCHAR), NULL);
		free(icon->tooltip);
		icon->tooltip = tip;
		if (tip)
			XStoreName(tray->xfc->display, icon->window, tip);
	}

	if (orderInfo->fieldFlags & WINDOW_ORDER_ICON)
	{
		if (xf_tray_convert_icon(&st->icon, icon))
			xf_tray_paint_icon(tray, icon);
	}

	/* Visibility state: NOTIFY_ICON state field. A non-zero state means the
	 * icon is hidden; map that onto mapping/unmapping our XEmbed window. */
	if (orderInfo->fieldFlags & WINDOW_ORDER_FIELD_NOTIFY_STATE)
	{
		unsigned long info[2] = { XEMBED_VERSION, (st->state == 0) ? XEMBED_MAPPED : 0 };
		XChangeProperty(tray->xfc->display, icon->window, tray->XEMBED_INFO, tray->XEMBED_INFO,
		                32, PropModeReplace, (unsigned char*)info, 2);
	}
}

/* ----------------------------------------------------------------- */
/* public notify-icon callbacks                                       */
/* ----------------------------------------------------------------- */

BOOL xf_tray_notify_icon_create(xfContext* xfc, const WINDOW_ORDER_INFO* orderInfo,
                                const NOTIFY_ICON_STATE_ORDER* notifyIconState)
{
	xfTray* tray = NULL;
	xfTrayIcon* icon = NULL;

	WINPR_ASSERT(xfc);
	WINPR_ASSERT(orderInfo);
	WINPR_ASSERT(notifyIconState);

	tray = xf_tray_init(xfc);
	if (!tray)
		return FALSE;

	icon = xf_tray_lookup(tray, orderInfo->windowId, orderInfo->notifyIconId);
	if (!icon)
		icon = xf_tray_icon_new(tray, orderInfo->windowId, orderInfo->notifyIconId);
	if (!icon)
		return FALSE;

	xf_tray_apply_state(tray, icon, orderInfo, notifyIconState);
	return TRUE;
}

BOOL xf_tray_notify_icon_update(xfContext* xfc, const WINDOW_ORDER_INFO* orderInfo,
                                const NOTIFY_ICON_STATE_ORDER* notifyIconState)
{
	xfTray* tray = NULL;
	xfTrayIcon* icon = NULL;

	WINPR_ASSERT(xfc);
	WINPR_ASSERT(orderInfo);
	WINPR_ASSERT(notifyIconState);

	tray = xf_tray_init(xfc);
	if (!tray)
		return FALSE;

	icon = xf_tray_lookup(tray, orderInfo->windowId, orderInfo->notifyIconId);
	if (!icon)
	{
		/* update before create: treat as create */
		return xf_tray_notify_icon_create(xfc, orderInfo, notifyIconState);
	}

	xf_tray_apply_state(tray, icon, orderInfo, notifyIconState);
	return TRUE;
}

BOOL xf_tray_notify_icon_delete(xfContext* xfc, const WINDOW_ORDER_INFO* orderInfo)
{
	xfTray* tray = xf_tray_get(xfc);
	UINT64 key = 0;

	if (!tray)
		return TRUE;

	key = xf_tray_key(orderInfo->windowId, orderInfo->notifyIconId);
	HashTable_Remove(tray->icons, (void*)(UINT_PTR)key);
	return TRUE;
}

/* ----------------------------------------------------------------- */
/* local click -> RAIL Notify Event PDU                               */
/* ----------------------------------------------------------------- */

static xfTrayIcon* xf_tray_find_by_window(xfTray* tray, Window window)
{
	xfTrayIcon* found = NULL;
	ULONG_PTR* keys = NULL;
	const size_t count = HashTable_GetKeys(tray->icons, &keys);

	for (size_t i = 0; i < count; i++)
	{
		xfTrayIcon* icon = (xfTrayIcon*)HashTable_GetItemValue(tray->icons, (void*)keys[i]);
		if (icon && (icon->window == window))
		{
			found = icon;
			break;
		}
	}
	free(keys);
	return found;
}

static BOOL xf_tray_send_notify_event(xfContext* xfc, xfTrayIcon* icon, UINT32 message)
{
	RAIL_NOTIFY_EVENT_ORDER notify = { 0 };

	if (!xfc->rail || !icon)
		return FALSE;

	notify.windowId = icon->windowId;
	notify.notifyIconId = icon->notifyIconId;
	notify.message = message;
	return xfc->rail->ClientNotifyEvent(xfc->rail, &notify) == CHANNEL_RC_OK;
}

BOOL xf_tray_handle_event(xfContext* xfc, const XEvent* event)
{
	xfTray* tray = xf_tray_get(xfc);
	xfTrayIcon* icon = NULL;

	if (!tray)
		return FALSE;

	switch (event->type)
	{
		case ClientMessage:
			/* A tray manager appeared: (re)dock all icons. */
			if ((event->xclient.message_type == tray->MANAGER) &&
			    ((Atom)event->xclient.data.l[1] == tray->NET_SYSTEM_TRAY_Sn))
			{
				tray->trayManager = (Window)event->xclient.data.l[2];

				ULONG_PTR* keys = NULL;
				const size_t count = HashTable_GetKeys(tray->icons, &keys);
				for (size_t i = 0; i < count; i++)
				{
					xfTrayIcon* it =
					    (xfTrayIcon*)HashTable_GetItemValue(tray->icons, (void*)keys[i]);
					if (it)
					{
						it->embedded = FALSE;
						xf_tray_set_xembed_info(tray, it);
						xf_tray_send_dock(tray, it);
					}
				}
				free(keys);
				return TRUE;
			}
			return FALSE;

		case ReparentNotify:
			icon = xf_tray_find_by_window(tray, event->xreparent.window);
			if (icon)
			{
				/* docked into the tray manager */
				icon->embedded = (event->xreparent.parent != RootWindowOfScreen(xfc->screen));
				if (icon->embedded)
					xf_tray_paint_icon(tray, icon);
				return TRUE;
			}
			return FALSE;

		case ConfigureNotify:
			icon = xf_tray_find_by_window(tray, event->xconfigure.window);
			if (icon)
			{
				if ((event->xconfigure.width > 0) && (event->xconfigure.height > 0))
				{
					icon->width = event->xconfigure.width;
					icon->height = event->xconfigure.height;
				}
				xf_tray_paint_icon(tray, icon);
				return TRUE;
			}
			return FALSE;

		case Expose:
			icon = xf_tray_find_by_window(tray, event->xexpose.window);
			if (icon)
			{
				xf_tray_paint_icon(tray, icon);
				return TRUE;
			}
			return FALSE;

		case ButtonPress:
			icon = xf_tray_find_by_window(tray, event->xbutton.window);
			if (icon)
			{
				/* Left button -> select; right button -> context menu request.
				 * Mirrors the messages a native tray icon would receive. */
				if (event->xbutton.button == Button1)
				{
					xf_tray_send_notify_event(xfc, icon, WM_LBUTTONUP);
					xf_tray_send_notify_event(xfc, icon, NIN_SELECT);
				}
				else if (event->xbutton.button == Button3)
				{
					xf_tray_send_notify_event(xfc, icon, WM_RBUTTONUP);
				}
				return TRUE;
			}
			return FALSE;

		case DestroyNotify:
			/* The tray manager window went away. */
			if (event->xdestroywindow.window == tray->trayManager)
			{
				tray->trayManager = None;
				return FALSE;
			}
			return FALSE;

		default:
			return FALSE;
	}
}

void xf_tray_uninit(xfContext* xfc)
{
	xfTray* tray = xf_tray_get(xfc);
	if (!tray)
		return;

	if (tray->icons)
		HashTable_Free(tray->icons);
	free(tray);
	xfc->tray = NULL;
}
