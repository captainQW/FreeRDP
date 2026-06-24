/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * macOS RAIL (RemoteApp Integrated Locally)
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

#import <Cocoa/Cocoa.h>

#include <freerdp/config.h>

#include <winpr/crt.h>
#include <winpr/assert.h>
#include <winpr/collections.h>

#include <freerdp/log.h>
#include <freerdp/window.h>
#include <freerdp/codec/color.h>
#include <freerdp/client/rail.h>

#include "mf_rail.h"

#define TAG CLIENT_TAG("mac")

/*
 * macOS RemoteApp (RAIL) integration.
 *
 * This mirrors the structure of the X11 (xf_rail.c) and Windows (wf_rail.c)
 * clients: each remote RAIL window is backed by a native NSWindow. The window
 * order callbacks registered on rdpUpdate->window create/update/destroy those
 * native windows, and local user interactions (close, focus, move/resize) are
 * forwarded back to the server via the RailClientContext client PDUs.
 *
 * NOTE: This is the foundational single-process implementation (one NSWindow
 * per RAIL window). The Dock-per-app proxy architecture described in the design
 * docs is a later, optional enhancement layered on top of this.
 */

@class MacRailWindowController;

typedef struct mac_rail_window
{
	mfContext* mfc;
	UINT32 windowId;

	int x;
	int y;
	int width;
	int height;
	char* title;

	/* Server-provided min/max tracking constraints (RAIL_MINMAXINFO_ORDER). */
	BOOL hasMinMax;
	int minTrackWidth;
	int minTrackHeight;
	int maxTrackWidth;
	int maxTrackHeight;

	/* Native AppKit window + its delegate/controller (retained). */
	void* nsWindow;     /* NSWindow*    */
	void* controller;   /* MacRailWindowController* */
} macRailWindow;

/* Forward declarations of the client->server PDU helpers. */
static BOOL mac_rail_send_activate(mfContext* mfc, macRailWindow* window, BOOL enabled);
static BOOL mac_rail_send_syscommand(mfContext* mfc, macRailWindow* window, UINT16 command);
static BOOL mac_rail_send_window_move(mfContext* mfc, macRailWindow* window);

/* ------------------------------------------------------------------ */
/* NSWindow delegate: translates local window events into RAIL PDUs.   */
/* ------------------------------------------------------------------ */

@interface MacRailWindowController : NSObject <NSWindowDelegate>
@property (nonatomic, assign) macRailWindow* railWindow;
@end

@implementation MacRailWindowController

/* User clicked the red close button: ask the remote app to close instead of
 * destroying the window behind the server's back (RAIL-011 SC_CLOSE). The
 * server then sends a Window Delete order which removes the native window. */
- (BOOL)windowShouldClose:(id)sender
{
	macRailWindow* w = self.railWindow;
	if (w && w->mfc)
	{
		if (mac_rail_send_syscommand(w->mfc, w, SC_CLOSE))
			return NO;
	}
	return YES;
}

/* Local focus changed -> tell the server (RAIL-009 Client Activate). */
- (void)windowDidBecomeKey:(NSNotification*)notification
{
	macRailWindow* w = self.railWindow;
	if (w && w->mfc)
		mac_rail_send_activate(w->mfc, w, TRUE);
}

- (void)windowDidResignKey:(NSNotification*)notification
{
	macRailWindow* w = self.railWindow;
	if (w && w->mfc)
		mac_rail_send_activate(w->mfc, w, FALSE);
}

/* Local move/resize finished -> report new geometry (RAIL-007 Window Move). */
- (void)windowDidMove:(NSNotification*)notification
{
	macRailWindow* w = self.railWindow;
	if (w && w->mfc)
		mac_rail_send_window_move(w->mfc, w);
}

- (void)windowDidResize:(NSNotification*)notification
{
	macRailWindow* w = self.railWindow;
	if (w && w->mfc)
		mac_rail_send_window_move(w->mfc, w);
}

/* Map the macOS miniaturize/zoom buttons onto RAIL system commands so the
 * remote window state follows (RAIL-011). */
- (void)windowDidMiniaturize:(NSNotification*)notification
{
	macRailWindow* w = self.railWindow;
	if (w && w->mfc)
		mac_rail_send_syscommand(w->mfc, w, SC_MINIMIZE);
}

- (void)windowDidDeminiaturize:(NSNotification*)notification
{
	macRailWindow* w = self.railWindow;
	if (w && w->mfc)
		mac_rail_send_syscommand(w->mfc, w, SC_RESTORE);
}

@end

/* ------------------------------------------------------------------ */
/* Coordinate helpers (RDP is top-left origin, AppKit is bottom-left). */
/* ------------------------------------------------------------------ */

static NSRect mac_rail_rdp_to_cocoa(int x, int y, int width, int height)
{
	NSScreen* screen = [NSScreen mainScreen];
	const CGFloat screenHeight = screen ? screen.frame.size.height : 0;
	NSRect frame;
	frame.origin.x = x;
	frame.origin.y = screenHeight - y - height;
	frame.size.width = width;
	frame.size.height = height;
	return frame;
}

/* ------------------------------------------------------------------ */
/* Hash table helpers for the windowId -> macRailWindow map.           */
/* ------------------------------------------------------------------ */

static void mac_rail_window_free(void* value)
{
	macRailWindow* window = (macRailWindow*)value;
	if (!window)
		return;

	@autoreleasepool
	{
		NSWindow* nsWindow = (__bridge_transfer NSWindow*)window->nsWindow;
		MacRailWindowController* controller =
		    (__bridge_transfer MacRailWindowController*)window->controller;
		window->nsWindow = NULL;
		window->controller = NULL;

		if (nsWindow)
		{
			nsWindow.delegate = nil;
			[nsWindow orderOut:nil];
			[nsWindow close];
		}
		(void)controller;
	}

	free(window->title);
	free(window);
}

static macRailWindow* mac_rail_get_window(mfContext* mfc, UINT32 windowId)
{
	if (!mfc || !mfc->railWindows)
		return NULL;
	return (macRailWindow*)HashTable_GetItemValue(mfc->railWindows, (void*)(UINT_PTR)windowId);
}

/* ------------------------------------------------------------------ */
/* client -> server RAIL PDU helpers.                                  */
/* ------------------------------------------------------------------ */

static BOOL mac_rail_send_activate(mfContext* mfc, macRailWindow* window, BOOL enabled)
{
	RAIL_ACTIVATE_ORDER activate = { 0 };

	if (!mfc || !mfc->rail || !window)
		return FALSE;

	activate.windowId = window->windowId;
	activate.enabled = enabled;
	return mfc->rail->ClientActivate(mfc->rail, &activate) == CHANNEL_RC_OK;
}

static BOOL mac_rail_send_syscommand(mfContext* mfc, macRailWindow* window, UINT16 command)
{
	RAIL_SYSCOMMAND_ORDER syscommand = { 0 };

	if (!mfc || !mfc->rail || !window)
		return FALSE;

	syscommand.windowId = window->windowId;
	syscommand.command = command;
	return mfc->rail->ClientSystemCommand(mfc->rail, &syscommand) == CHANNEL_RC_OK;
}

static BOOL mac_rail_send_window_move(mfContext* mfc, macRailWindow* window)
{
	RAIL_WINDOW_MOVE_ORDER windowMove = { 0 };
	NSWindow* nsWindow = NULL;
	NSRect frame;
	NSScreen* screen = NULL;
	CGFloat screenHeight = 0;
	int top = 0;

	if (!mfc || !mfc->rail || !window || !window->nsWindow)
		return FALSE;

	nsWindow = (__bridge NSWindow*)window->nsWindow;
	frame = nsWindow.frame;
	screen = nsWindow.screen ? nsWindow.screen : [NSScreen mainScreen];
	screenHeight = screen ? screen.frame.size.height : 0;

	/* Convert AppKit (bottom-left) frame back to RDP (top-left) coordinates. */
	top = (int)(screenHeight - frame.origin.y - frame.size.height);

	window->x = (int)frame.origin.x;
	window->y = top;
	window->width = (int)frame.size.width;
	window->height = (int)frame.size.height;

	windowMove.windowId = window->windowId;
	windowMove.left = (INT16)window->x;
	windowMove.top = (INT16)window->y;
	/* per [MS-RDPERP] the right/bottom edges are one pixel past the window */
	windowMove.right = (INT16)(window->x + window->width);
	windowMove.bottom = (INT16)(window->y + window->height);
	return mfc->rail->ClientWindowMove(mfc->rail, &windowMove) == CHANNEL_RC_OK;
}

/* ------------------------------------------------------------------ */
/* Window order callbacks (server -> client).                          */
/* ------------------------------------------------------------------ */

static BOOL mac_rail_window_common(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo,
                                   const WINDOW_STATE_ORDER* windowState)
{
	mfContext* mfc = (mfContext*)context;
	macRailWindow* window = NULL;
	const UINT32 fieldFlags = orderInfo->fieldFlags;

	WINPR_ASSERT(mfc);
	WINPR_ASSERT(orderInfo);
	WINPR_ASSERT(windowState);

	if (fieldFlags & WINDOW_ORDER_STATE_NEW)
	{
		window = (macRailWindow*)calloc(1, sizeof(macRailWindow));
		if (!window)
			return FALSE;

		window->mfc = mfc;
		window->windowId = orderInfo->windowId;
		window->x = windowState->windowOffsetX;
		window->y = windowState->windowOffsetY;
		window->width = (int)windowState->windowWidth;
		window->height = (int)windowState->windowHeight;

		if (fieldFlags & WINDOW_ORDER_FIELD_TITLE)
		{
			const WCHAR* str = (const WCHAR*)windowState->titleInfo.string;
			window->title = ConvertWCharNToUtf8Alloc(
			    str, windowState->titleInfo.length / sizeof(WCHAR), NULL);
		}
		if (!window->title)
			window->title = _strdup("RemoteApp");

		@autoreleasepool
		{
			NSRect frame = mac_rail_rdp_to_cocoa(window->x, window->y, window->width,
			                                     window->height);
			NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
			                              NSWindowStyleMaskMiniaturizable |
			                              NSWindowStyleMaskResizable;
			NSWindow* nsWindow = [[NSWindow alloc] initWithContentRect:frame
			                                                 styleMask:styleMask
			                                                   backing:NSBackingStoreBuffered
			                                                     defer:NO];
			MacRailWindowController* controller = [[MacRailWindowController alloc] init];
			controller.railWindow = window;
			nsWindow.delegate = controller;
			nsWindow.releasedWhenClosed = NO;
			if (window->title)
				nsWindow.title = [NSString stringWithUTF8String:window->title];

			window->nsWindow = (__bridge_retained void*)nsWindow;
			window->controller = (__bridge_retained void*)controller;

			[nsWindow makeKeyAndOrderFront:nil];
		}

		if (!HashTable_Insert(mfc->railWindows, (void*)(UINT_PTR)window->windowId, window))
		{
			mac_rail_window_free(window);
			return FALSE;
		}
		return TRUE;
	}

	window = mac_rail_get_window(mfc, orderInfo->windowId);
	if (!window)
		return TRUE;

	@autoreleasepool
	{
		NSWindow* nsWindow = (__bridge NSWindow*)window->nsWindow;

		if ((fieldFlags & WINDOW_ORDER_FIELD_WND_OFFSET) ||
		    (fieldFlags & WINDOW_ORDER_FIELD_WND_SIZE))
		{
			if (fieldFlags & WINDOW_ORDER_FIELD_WND_OFFSET)
			{
				window->x = windowState->windowOffsetX;
				window->y = windowState->windowOffsetY;
			}
			if (fieldFlags & WINDOW_ORDER_FIELD_WND_SIZE)
			{
				window->width = (int)windowState->windowWidth;
				window->height = (int)windowState->windowHeight;
			}
			NSRect frame = mac_rail_rdp_to_cocoa(window->x, window->y, window->width,
			                                     window->height);
			[nsWindow setFrame:frame display:YES];
		}

		if (fieldFlags & WINDOW_ORDER_FIELD_TITLE)
		{
			const WCHAR* str = (const WCHAR*)windowState->titleInfo.string;
			char* title = ConvertWCharNToUtf8Alloc(
			    str, windowState->titleInfo.length / sizeof(WCHAR), NULL);
			if (title)
			{
				free(window->title);
				window->title = title;
				nsWindow.title = [NSString stringWithUTF8String:title];
			}
		}

		if (fieldFlags & WINDOW_ORDER_FIELD_SHOW)
		{
			switch (windowState->showState)
			{
				case 0: /* SW_HIDE */
					[nsWindow orderOut:nil];
					break;
				case 2: /* SW_SHOWMINIMIZED */
					if (![nsWindow isMiniaturized])
						[nsWindow miniaturize:nil];
					break;
				case 3: /* SW_SHOWMAXIMIZED */
					if (![nsWindow isZoomed])
						[nsWindow zoom:nil];
					break;
				default: /* SW_SHOWNORMAL / others */
					if ([nsWindow isMiniaturized])
						[nsWindow deminiaturize:nil];
					[nsWindow makeKeyAndOrderFront:nil];
					break;
			}
		}
	}

	return TRUE;
}

static BOOL mac_rail_window_delete(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo)
{
	mfContext* mfc = (mfContext*)context;

	WINPR_ASSERT(mfc);
	WINPR_ASSERT(orderInfo);

	if (!mfc->railWindows)
		return TRUE;

	/* HashTable value object free callback destroys the native window. */
	HashTable_Remove(mfc->railWindows, (void*)(UINT_PTR)orderInfo->windowId);
	return TRUE;
}

/* ----------------------------------------------------------------- */
/* RAIL notification icons -> macOS Menu Bar status items.            */
/* macOS has no system tray; the documented equivalent is an          */
/* NSStatusItem in the menu bar (see design doc section 11).          */
/* ----------------------------------------------------------------- */

static UINT64 mac_rail_notify_key(UINT32 windowId, UINT32 notifyIconId)
{
	return (((UINT64)windowId) << 32) | (UINT64)notifyIconId;
}

/* Controller object retained as the status item's target; forwards menu-bar
 * clicks back to the server as RAIL Notify Event PDUs. */
@interface MacRailStatusItem : NSObject
@property (nonatomic, strong) NSStatusItem* item;
@property (nonatomic, assign) mfContext* mfc;
@property (nonatomic, assign) UINT32 windowId;
@property (nonatomic, assign) UINT32 notifyIconId;
@end

@implementation MacRailStatusItem
- (void)clicked:(id)sender
{
	if (self.mfc && self.mfc->rail)
	{
		RAIL_NOTIFY_EVENT_ORDER notify = { 0 };
		notify.windowId = self.windowId;
		notify.notifyIconId = self.notifyIconId;
		notify.message = WM_LBUTTONUP;
		(void)self.mfc->rail->ClientNotifyEvent(self.mfc->rail, &notify);

		notify.message = NIN_SELECT;
		(void)self.mfc->rail->ClientNotifyEvent(self.mfc->rail, &notify);
	}
}
@end

static void mac_rail_status_item_free(void* value)
{
	MacRailStatusItem* ctl = (__bridge_transfer MacRailStatusItem*)value;
	if (ctl)
	{
		@autoreleasepool
		{
			if (ctl.item)
				[[NSStatusBar systemStatusBar] removeStatusItem:ctl.item];
			ctl.item = nil;
		}
	}
}

/* Convert a RAIL ICON_INFO into an NSImage (premultiplied BGRA). Returns a
 * retained NSImage* via __bridge_retained in 'out' or nil on failure. */
static NSImage* mac_rail_icon_to_nsimage(const ICON_INFO* iconInfo)
{
	const UINT32 w = iconInfo->width;
	const UINT32 h = iconInfo->height;
	BYTE* argb = NULL;
	NSBitmapImageRep* rep = NULL;
	NSImage* image = NULL;

	if ((w == 0) || (h == 0))
		return nil;

	argb = (BYTE*)calloc((size_t)w * h, 4);
	if (!argb)
		return nil;

	if (!freerdp_image_copy_from_icon_data(
	        argb, PIXEL_FORMAT_RGBA32, 0, 0, 0, (UINT16)w, (UINT16)h, iconInfo->bitsColor,
	        (UINT16)iconInfo->cbBitsColor, iconInfo->bitsMask, (UINT16)iconInfo->cbBitsMask,
	        iconInfo->colorTable, (UINT16)iconInfo->cbColorTable, iconInfo->bpp))
	{
		free(argb);
		return nil;
	}

	rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
	                                              pixelsWide:(NSInteger)w
	                                              pixelsHigh:(NSInteger)h
	                                           bitsPerSample:8
	                                         samplesPerPixel:4
	                                                hasAlpha:YES
	                                                isPlanar:NO
	                                          colorSpaceName:NSCalibratedRGBColorSpace
	                                             bytesPerRow:(NSInteger)(w * 4)
	                                            bitsPerPixel:32];
	if (!rep)
	{
		free(argb);
		return nil;
	}

	memcpy([rep bitmapData], argb, (size_t)w * h * 4);
	free(argb);

	image = [[NSImage alloc] initWithSize:NSMakeSize((CGFloat)w, (CGFloat)h)];
	[image addRepresentation:rep];
	return image;
}

static BOOL mac_rail_window_icon(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo,
                                 const WINDOW_ICON_ORDER* windowIcon)
{
	mfContext* mfc = (mfContext*)context;
	macRailWindow* window = NULL;

	WINPR_ASSERT(mfc);
	WINPR_ASSERT(orderInfo);

	window = mac_rail_get_window(mfc, orderInfo->windowId);
	if (!window || !window->nsWindow || !windowIcon || !windowIcon->iconInfo)
		return TRUE;

	@autoreleasepool
	{
		NSImage* icon = mac_rail_icon_to_nsimage(windowIcon->iconInfo);
		if (icon)
		{
			NSWindow* nsWindow = (__bridge NSWindow*)window->nsWindow;
			/* Show the remote app's icon in the window's title-bar document
			 * icon and as the app icon, matching native behavior (APP-001). */
			[nsWindow setRepresentedURL:[NSURL fileURLWithPath:@"/"]];
			[[nsWindow standardWindowButton:NSWindowDocumentIconButton] setImage:icon];
		}
	}
	return TRUE;
}

static BOOL mac_rail_window_cached_icon(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo,
                                        const WINDOW_CACHED_ICON_ORDER* windowCachedIcon)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(orderInfo);
	WINPR_UNUSED(windowCachedIcon);
	return TRUE;
}

/* Create or update the menu-bar status item that represents a RAIL
 * notification-area icon. */
static BOOL mac_rail_notify_icon_set(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo,
                                     const NOTIFY_ICON_STATE_ORDER* notifyIconState)
{
	mfContext* mfc = (mfContext*)context;
	MacRailStatusItem* ctl = NULL;
	UINT64 key = 0;

	WINPR_ASSERT(mfc);
	WINPR_ASSERT(orderInfo);
	WINPR_ASSERT(notifyIconState);

	if (!mfc->railStatusItems)
		return TRUE;

	key = mac_rail_notify_key(orderInfo->windowId, orderInfo->notifyIconId);

	@autoreleasepool
	{
		void* existing = HashTable_GetItemValue(mfc->railStatusItems, (void*)(UINT_PTR)key);
		if (existing)
			ctl = (__bridge MacRailStatusItem*)existing;

		if (!ctl)
		{
			ctl = [[MacRailStatusItem alloc] init];
			ctl.mfc = mfc;
			ctl.windowId = orderInfo->windowId;
			ctl.notifyIconId = orderInfo->notifyIconId;
			ctl.item = [[NSStatusBar systemStatusBar]
			    statusItemWithLength:NSSquareStatusItemLength];
			ctl.item.button.target = ctl;
			ctl.item.button.action = @selector(clicked:);

			if (!HashTable_Insert(mfc->railStatusItems, (void*)(UINT_PTR)key,
			                      (__bridge_retained void*)ctl))
				return FALSE;
		}

		if (orderInfo->fieldFlags & WINDOW_ORDER_ICON)
		{
			NSImage* icon = mac_rail_icon_to_nsimage(&notifyIconState->icon);
			if (icon)
			{
				icon.size = NSMakeSize(18, 18); /* standard menu-bar size */
				ctl.item.button.image = icon;
			}
		}

		if (orderInfo->fieldFlags & WINDOW_ORDER_FIELD_NOTIFY_TIP)
		{
			const WCHAR* str = (const WCHAR*)notifyIconState->toolTip.string;
			if (notifyIconState->toolTip.length > 0)
			{
				char* tip = ConvertWCharNToUtf8Alloc(
				    str, notifyIconState->toolTip.length / sizeof(WCHAR), NULL);
				if (tip)
				{
					ctl.item.button.toolTip = [NSString stringWithUTF8String:tip];
					free(tip);
				}
			}
		}
	}
	return TRUE;
}

static BOOL mac_rail_notify_icon_create(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo,
                                        const NOTIFY_ICON_STATE_ORDER* notifyIconState)
{
	return mac_rail_notify_icon_set(context, orderInfo, notifyIconState);
}

static BOOL mac_rail_notify_icon_update(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo,
                                        const NOTIFY_ICON_STATE_ORDER* notifyIconState)
{
	return mac_rail_notify_icon_set(context, orderInfo, notifyIconState);
}

static BOOL mac_rail_notify_icon_delete(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo)
{
	mfContext* mfc = (mfContext*)context;
	UINT64 key = 0;

	WINPR_ASSERT(mfc);
	WINPR_ASSERT(orderInfo);

	if (!mfc->railStatusItems)
		return TRUE;

	key = mac_rail_notify_key(orderInfo->windowId, orderInfo->notifyIconId);
	HashTable_Remove(mfc->railStatusItems, (void*)(UINT_PTR)key);
	return TRUE;
}

static BOOL mac_rail_monitored_desktop(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo,
                                       const MONITORED_DESKTOP_ORDER* monitoredDesktop)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(orderInfo);
	WINPR_UNUSED(monitoredDesktop);
	return TRUE;
}

static BOOL mac_rail_non_monitored_desktop(rdpContext* context, const WINDOW_ORDER_INFO* orderInfo)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(orderInfo);
	return TRUE;
}

static void mac_rail_register_update_callbacks(rdpUpdate* update)
{
	rdpWindowUpdate* window = update->window;
	window->WindowCreate = mac_rail_window_common;
	window->WindowUpdate = mac_rail_window_common;
	window->WindowDelete = mac_rail_window_delete;
	window->WindowIcon = mac_rail_window_icon;
	window->WindowCachedIcon = mac_rail_window_cached_icon;
	window->NotifyIconCreate = mac_rail_notify_icon_create;
	window->NotifyIconUpdate = mac_rail_notify_icon_update;
	window->NotifyIconDelete = mac_rail_notify_icon_delete;
	window->MonitoredDesktop = mac_rail_monitored_desktop;
	window->NonMonitoredDesktop = mac_rail_non_monitored_desktop;
}

/* ------------------------------------------------------------------ */
/* RAIL client context callbacks (server -> client virtual channel).   */
/* ------------------------------------------------------------------ */

static const char* mac_rail_exec_error_code2str(UINT32 code)
{
#define EVCASE(x) \
	case x:       \
		return #x
	switch (code)
	{
		EVCASE(RAIL_EXEC_S_OK);
		EVCASE(RAIL_EXEC_E_HOOK_NOT_LOADED);
		EVCASE(RAIL_EXEC_E_DECODE_FAILED);
		EVCASE(RAIL_EXEC_E_NOT_IN_ALLOWLIST);
		EVCASE(RAIL_EXEC_E_FILE_NOT_FOUND);
		EVCASE(RAIL_EXEC_E_FAIL);
		EVCASE(RAIL_EXEC_E_SESSION_LOCKED);
		default:
			return "RAIL_EXEC_E_UNKNOWN";
	}
#undef EVCASE
}

static UINT mac_rail_server_execute_result(RailClientContext* context,
                                           const RAIL_EXEC_RESULT_ORDER* execResult)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(execResult);

	mfContext* mfc = (mfContext*)context->custom;
	WINPR_ASSERT(mfc);

	if (execResult->execResult != RAIL_EXEC_S_OK)
	{
		WLog_ERR(TAG, "RAIL exec error: execResult=%s [0x%08" PRIx32 "] NtError=0x%X",
		         mac_rail_exec_error_code2str(execResult->execResult), execResult->execResult,
		         execResult->rawResult);
		freerdp_abort_connect_context(&mfc->common.context);
	}

	return CHANNEL_RC_OK;
}

static UINT mac_rail_server_system_param(RailClientContext* context,
                                         const RAIL_SYSPARAM_ORDER* sysparam)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(sysparam);
	return CHANNEL_RC_OK;
}

static UINT mac_rail_server_handshake(RailClientContext* context,
                                      const RAIL_HANDSHAKE_ORDER* handshake)
{
	WINPR_UNUSED(handshake);
	return client_rail_server_start_cmd(context);
}

static UINT mac_rail_server_handshake_ex(RailClientContext* context,
                                         const RAIL_HANDSHAKE_EX_ORDER* handshakeEx)
{
	WINPR_UNUSED(handshakeEx);
	return client_rail_server_start_cmd(context);
}

static UINT mac_rail_server_local_move_size(RailClientContext* context,
                                            const RAIL_LOCALMOVESIZE_ORDER* localMoveSize)
{
	/* On macOS the window server owns interactive move/resize loops and does not
	 * expose a programmatic "begin move/size" entry point equivalent to X11's
	 * _NET_WM_MOVERESIZE or Win32 WM_SYSCOMMAND. The native title-bar drag is
	 * already handled locally and reported back via windowDidMove/Resize, so we
	 * just acknowledge here. */
	WINPR_UNUSED(context);
	WINPR_UNUSED(localMoveSize);
	return CHANNEL_RC_OK;
}

static UINT mac_rail_server_min_max_info(RailClientContext* context,
                                         const RAIL_MINMAXINFO_ORDER* minMaxInfo)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(minMaxInfo);

	mfContext* mfc = (mfContext*)context->custom;
	macRailWindow* window = mac_rail_get_window(mfc, minMaxInfo->windowId);
	if (!window)
		return CHANNEL_RC_OK;

	window->hasMinMax = TRUE;
	window->minTrackWidth = minMaxInfo->minTrackWidth;
	window->minTrackHeight = minMaxInfo->minTrackHeight;
	window->maxTrackWidth = minMaxInfo->maxTrackWidth;
	window->maxTrackHeight = minMaxInfo->maxTrackHeight;

	@autoreleasepool
	{
		NSWindow* nsWindow = (__bridge NSWindow*)window->nsWindow;
		if (nsWindow)
		{
			if ((window->minTrackWidth > 0) && (window->minTrackHeight > 0))
				nsWindow.contentMinSize =
				    NSMakeSize(window->minTrackWidth, window->minTrackHeight);
			if ((window->maxTrackWidth > 0) && (window->maxTrackHeight > 0))
				nsWindow.contentMaxSize =
				    NSMakeSize(window->maxTrackWidth, window->maxTrackHeight);
		}
	}
	return CHANNEL_RC_OK;
}

static UINT mac_rail_server_language_bar_info(RailClientContext* context,
                                              const RAIL_LANGBAR_INFO_ORDER* langBarInfo)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(langBarInfo);
	return CHANNEL_RC_OK;
}

static UINT mac_rail_server_get_appid_response(RailClientContext* context,
                                               const RAIL_GET_APPID_RESP_ORDER* getAppIdResp)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(getAppIdResp);
	return CHANNEL_RC_OK;
}

/* ------------------------------------------------------------------ */
/* Public init / uninit.                                               */
/* ------------------------------------------------------------------ */

BOOL mac_rail_init(mfContext* mfc, RailClientContext* rail)
{
	rdpContext* context = (rdpContext*)mfc;

	if (!mfc || !rail)
		return FALSE;

	mfc->rail = rail;
	rail->custom = (void*)mfc;
	rail->ServerExecuteResult = mac_rail_server_execute_result;
	rail->ServerSystemParam = mac_rail_server_system_param;
	rail->ServerHandshake = mac_rail_server_handshake;
	rail->ServerHandshakeEx = mac_rail_server_handshake_ex;
	rail->ServerLocalMoveSize = mac_rail_server_local_move_size;
	rail->ServerMinMaxInfo = mac_rail_server_min_max_info;
	rail->ServerLanguageBarInfo = mac_rail_server_language_bar_info;
	rail->ServerGetAppIdResponse = mac_rail_server_get_appid_response;

	mac_rail_register_update_callbacks(context->update);

	mfc->railWindows = HashTable_New(TRUE);
	if (!mfc->railWindows)
		return FALSE;

	{
		wObject* obj = HashTable_ValueObject(mfc->railWindows);
		obj->fnObjectFree = mac_rail_window_free;
	}

	/* Menu-bar status items for RAIL notification-area icons. */
	mfc->railStatusItems = HashTable_New(TRUE);
	if (mfc->railStatusItems)
	{
		wObject* obj = HashTable_ValueObject(mfc->railStatusItems);
		obj->fnObjectFree = mac_rail_status_item_free;
	}
	return TRUE;
}

void mac_rail_uninit(mfContext* mfc, RailClientContext* rail)
{
	if (!mfc)
		return;

	if (rail)
		rail->custom = NULL;
	mfc->rail = NULL;

	if (mfc->railWindows)
	{
		HashTable_Free(mfc->railWindows);
		mfc->railWindows = NULL;
	}

	if (mfc->railStatusItems)
	{
		HashTable_Free(mfc->railStatusItems);
		mfc->railStatusItems = NULL;
	}
}
