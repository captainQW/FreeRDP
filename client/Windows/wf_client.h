/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Windows Client
 *
 * Copyright 2009-2011 Jay Sorg
 * Copyright 2010-2011 Vic Lee
 * Copyright 2010-2011 Marc-Andre Moreau <marcandre.moreau@gmail.com>
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

#ifndef FREERDP_CLIENT_WIN_INTERFACE_H
#define FREERDP_CLIENT_WIN_INTERFACE_H

#include <winpr/windows.h>

#include <winpr/collections.h>

#ifdef WITH_PROGRESS_BAR
#include <shobjidl.h>
#endif

#include <freerdp/api.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/dc.h>
#include <freerdp/gdi/region.h>
#include <freerdp/codec/color.h>

#include <freerdp/client/rail.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/channels/channels.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/client/file.h>

#include "wf_channels.h"
#include "wf_floatbar.h"
#include "wf_event.h"
#include "wf_cliprdr.h"

#ifdef __cplusplus
extern "C"
{
#endif

// System menu constants
#define SYSCOMMAND_ID_SMARTSIZING 1000
#define SYSCOMMAND_ID_REQUEST_CONTROL 1001

/* Posted from the GFX (DVC) thread to the main UI thread to (re)create, size
 * and show the HiDef RemoteApp window and trigger a repaint. The window must be
 * created and pumped on the main thread or it never receives WM_PAINT.
 * wParam = width, lParam = height. */
#define WM_FREERDP_GFX_UPDATE (WM_USER + 101)

	typedef struct
	{
		rdpBitmap _bitmap;
		HDC hdc;
		HBITMAP bitmap;
		HBITMAP org_bitmap;
		BYTE* pdata;
	} wfBitmap;

	typedef struct
	{
		rdpPointer pointer;
		HCURSOR cursor;
	} wfPointer;

	struct wf_context
	{
		rdpClientContext common;

		int offset_x;
		int offset_y;
		int fullscreen_toggle;
		int fullscreen;
		int percentscreen;
		WCHAR* window_title;
		int client_x;
		int client_y;
		int client_width;
		int client_height;

		HANDLE keyboardThread;

		HICON icon;
		HWND hWndParent;
		HINSTANCE hInstance;
		WNDCLASSEX wndClass;
		LPCTSTR wndClassName;
		HCURSOR hDefaultCursor;

		UINT systemMenuInsertPosition;

		HWND hwnd;
		BOOL is_shown;
		ITaskbarList3* taskBarList;
		POINT diff;

		wfBitmap* primary;
		wfBitmap* drawing;
		HCURSOR cursor;
		HBRUSH brush;
		HBRUSH org_brush;
		RECT update_rect;
		RECT scale_update_rect;

		DWORD mainThreadId;
		DWORD keyboardThreadId;

		rdpFile* connectionRdpFile;

		BOOL disablewindowtracking;

		BOOL updating_scrollbars;
		BOOL xScrollVisible;
		int xMinScroll;
		int xCurrentScroll;
		int xMaxScroll;

		BOOL yScrollVisible;
		int yMinScroll;
		int yCurrentScroll;
		int yMaxScroll;

		void* clipboard;
		CliprdrClientContext* cliprdr;

		wfFloatBar* floatbar;

		RailClientContext* rail;
		wHashTable* railWindows;
		/* RAIL notification-area (system tray) icons, keyed by
		 * windowId<<32|notifyIconId, rendered via Shell_NotifyIcon. */
		wHashTable* railNotifyIcons;
		/* Number of times the RemoteApp launch (Execute PDU) has been retried
		 * after a transient RAIL_EXEC_E_HOOK_NOT_LOADED result. */
		UINT32 railExecRetries;

		/* HiDef RemoteApp (RDPGFX MapSurfaceToWindow): some servers deliver the
		 * application content as a GFX surface mapped to a window instead of via
		 * RAIL window orders. These back a single borderless top-level window
		 * that mirrors that surface so the app is actually visible. The window
		 * and all GDI objects are owned by the main UI thread; the GFX/DVC
		 * thread only stages decoded pixels into gfxData (guarded by gfxLock)
		 * and posts WM_FREERDP_GFX_UPDATE. */
		HWND gfxWnd;
		HDC gfxHdc;
		HBITMAP gfxBitmap;
		BYTE* gfxDibBits;  /* DIB section bits, main thread only */
		BYTE* gfxData;     /* staging buffer written by the GFX thread */
		int gfxWidth;
		int gfxHeight;
		UINT64 gfxWindowId;
		CRITICAL_SECTION gfxLock;
		BOOL gfxLockValid;

		/* RemoteApp launch splash ("正在打开应用 ...") shown on the main thread
		 * between connect and the first application window. */
		HWND splashWnd;

		BOOL isConsole;

		DispClientContext* disp;
		UINT64 lastSentDate;
		BOOL wasMaximized;

		RdpgfxClientContext* gfx;
	};

	/**
	 * Client Interface
	 */

	FREERDP_API int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints);
	FREERDP_API int freerdp_client_set_window_size(wfContext* wfc, int width, int height);
	FREERDP_API void wf_size_scrollbars(wfContext* wfc, UINT32 client_width, UINT32 client_height);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_WIN_INTERFACE_H */
