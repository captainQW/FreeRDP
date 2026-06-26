/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Windows Graphical Objects
 *
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

#include <freerdp/config.h>

#include <winpr/crt.h>

#include <freerdp/codecs.h>
#include <freerdp/log.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/codec/color.h>
#include <freerdp/codec/region.h>

#include "wf_gdi.h"
#include "wf_graphics.h"

#define TAG CLIENT_TAG("windows")

HBITMAP wf_create_dib(wfContext* wfc, UINT32 width, UINT32 height, UINT32 srcFormat,
                      const BYTE* data, BYTE** pdata)
{
	HDC hdc;
	int negHeight;
	HBITMAP bitmap;
	BITMAPINFO bmi;
	BYTE* cdata = nullptr;
	UINT32 dstFormat = srcFormat;
	/**
	 * See: http://msdn.microsoft.com/en-us/library/dd183376
	 * if biHeight is positive, the bitmap is bottom-up
	 * if biHeight is negative, the bitmap is top-down
	 * Since we get top-down bitmaps, let's keep it that way
	 */
	negHeight = (height < 0) ? height : height * (-1);
	hdc = GetDC(nullptr);
	bmi.bmiHeader.biSize = sizeof(BITMAPINFO);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = negHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = FreeRDPGetBitsPerPixel(dstFormat);
	bmi.bmiHeader.biCompression = BI_RGB;
	bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&cdata, nullptr, 0);

	if (data)
		freerdp_image_copy(cdata, dstFormat, 0, 0, 0, width, height, data, srcFormat, 0, 0, 0,
		                   &wfc->common.context.gdi->palette, FREERDP_FLIP_NONE);

	if (pdata)
		*pdata = cdata;

	ReleaseDC(nullptr, hdc);
	GdiFlush();
	return bitmap;
}

wfBitmap* wf_image_new(wfContext* wfc, UINT32 width, UINT32 height, UINT32 format, const BYTE* data)
{
	wfBitmap* image = (wfBitmap*)malloc(sizeof(wfBitmap));
	if (!image)
	{
		WLog_ERR(TAG, "malloc failed for wfBitmap");
		return nullptr;
	}

	HDC hdc = GetDC(nullptr);
	image->hdc = CreateCompatibleDC(hdc);
	image->bitmap = wf_create_dib(wfc, width, height, format, data, &(image->pdata));
	image->org_bitmap = (HBITMAP)SelectObject(image->hdc, image->bitmap);
	ReleaseDC(nullptr, hdc);
	return image;
}

void wf_image_free(wfBitmap* image)
{
	if (image != 0)
	{
		SelectObject(image->hdc, image->org_bitmap);
		DeleteObject(image->bitmap);
		DeleteDC(image->hdc);
		free(image);
	}
}

/* Bitmap Class */

static BOOL wf_Bitmap_New(rdpContext* context, rdpBitmap* bitmap)
{
	HDC hdc;
	wfContext* wfc = (wfContext*)context;
	wfBitmap* wf_bitmap = (wfBitmap*)bitmap;

	if (!context || !bitmap)
		return FALSE;

	wf_bitmap = (wfBitmap*)bitmap;
	hdc = GetDC(nullptr);
	wf_bitmap->hdc = CreateCompatibleDC(hdc);

	if (!bitmap->data)
		wf_bitmap->bitmap = CreateCompatibleBitmap(hdc, bitmap->width, bitmap->height);
	else
		wf_bitmap->bitmap = wf_create_dib(wfc, bitmap->width, bitmap->height, bitmap->format,
		                                  bitmap->data, nullptr);

	wf_bitmap->org_bitmap = (HBITMAP)SelectObject(wf_bitmap->hdc, wf_bitmap->bitmap);
	ReleaseDC(nullptr, hdc);
	return TRUE;
}

static void wf_Bitmap_Free(rdpContext* context, rdpBitmap* bitmap)
{
	wfBitmap* wf_bitmap = (wfBitmap*)bitmap;

	if (wf_bitmap != 0)
	{
		SelectObject(wf_bitmap->hdc, wf_bitmap->org_bitmap);
		DeleteObject(wf_bitmap->bitmap);
		DeleteDC(wf_bitmap->hdc);

		winpr_aligned_free(wf_bitmap->_bitmap.data);
		wf_bitmap->_bitmap.data = nullptr;
	}
}

static BOOL wf_Bitmap_Paint(rdpContext* context, rdpBitmap* bitmap)
{
	BOOL rc;
	UINT32 width, height;
	wfContext* wfc = (wfContext*)context;
	wfBitmap* wf_bitmap = (wfBitmap*)bitmap;

	if (!context || !bitmap)
		return FALSE;

	width = bitmap->right - bitmap->left + 1;
	height = bitmap->bottom - bitmap->top + 1;
	rc = BitBlt(wfc->primary->hdc, bitmap->left, bitmap->top, width, height, wf_bitmap->hdc, 0, 0,
	            SRCCOPY);
	wf_invalidate_region(wfc, bitmap->left, bitmap->top, width, height);
	return rc;
}

static BOOL wf_Bitmap_SetSurface(rdpContext* context, rdpBitmap* bitmap, BOOL primary)
{
	wfContext* wfc = (wfContext*)context;
	wfBitmap* bmp = (wfBitmap*)bitmap;
	rdpGdi* gdi = context->gdi;

	if (!gdi || !wfc)
		return FALSE;

	if (primary)
		wfc->drawing = wfc->primary;
	else if (!bmp)
		return FALSE;
	else
		wfc->drawing = bmp;

	return TRUE;
}

/* Pointer Class */

static BOOL flip_bitmap(const BYTE* src, BYTE* dst, UINT32 scanline, UINT32 nHeight)
{
	BYTE* bottomLine = dst + scanline * (nHeight - 1);

	for (UINT32 x = 0; x < nHeight; x++)
	{
		memcpy(bottomLine, src, scanline);
		src += scanline;
		bottomLine -= scanline;
	}

	return TRUE;
}

static BOOL wf_Pointer_New(rdpContext* context, rdpPointer* pointer)
{
	HCURSOR hCur;
	ICONINFO info;
	rdpGdi* gdi;
	BOOL rc = FALSE;

	if (!context || !pointer)
		return FALSE;

	gdi = context->gdi;

	if (!gdi)
		return FALSE;

	info.fIcon = FALSE;
	info.xHotspot = pointer->xPos;
	info.yHotspot = pointer->yPos;

	if (pointer->xorBpp == 1)
	{
		BYTE* pdata = nullptr;

		if ((pointer->lengthAndMask > 0) || (pointer->lengthXorMask > 0))
		{
			pdata =
			    (BYTE*)winpr_aligned_malloc(pointer->lengthAndMask + pointer->lengthXorMask, 16);

			if (!pdata)
				goto fail;
		}

		CopyMemory(pdata, pointer->andMaskData, pointer->lengthAndMask);
		CopyMemory(pdata + pointer->lengthAndMask, pointer->xorMaskData, pointer->lengthXorMask);
		info.hbmMask = CreateBitmap(pointer->width, pointer->height * 2, 1, 1, pdata);
		winpr_aligned_free(pdata);
		info.hbmColor = nullptr;
	}
	else
	{
		UINT32 srcFormat;
		BYTE* pdata = nullptr;

		if (pointer->lengthAndMask > 0)
		{
			pdata = (BYTE*)winpr_aligned_malloc(pointer->lengthAndMask, 16);

			if (!pdata)
				goto fail;
			flip_bitmap(pointer->andMaskData, pdata, (pointer->width + 7) / 8, pointer->height);
		}

		info.hbmMask = CreateBitmap(pointer->width, pointer->height, 1, 1, pdata);
		winpr_aligned_free(pdata);

		/* currently color xorBpp is only 24 per [T128] section 8.14.3 */
		srcFormat = gdi_get_pixel_format(pointer->xorBpp);

		if (!srcFormat)
			goto fail;

		info.hbmColor = wf_create_dib((wfContext*)context, pointer->width, pointer->height,
		                              gdi->dstFormat, nullptr, &pdata);

		if (!info.hbmColor)
			goto fail;

		if (!freerdp_image_copy_from_pointer_data(
		        pdata, gdi->dstFormat, 0, 0, 0, pointer->width, pointer->height,
		        pointer->xorMaskData, pointer->lengthXorMask, pointer->andMaskData,
		        pointer->lengthAndMask, pointer->xorBpp, &gdi->palette))
		{
			goto fail;
		}
	}

	hCur = CreateIconIndirect(&info);
	((wfPointer*)pointer)->cursor = hCur;
	rc = TRUE;
fail:

	if (info.hbmMask)
		DeleteObject(info.hbmMask);

	if (info.hbmColor)
		DeleteObject(info.hbmColor);

	return rc;
}

static void wf_Pointer_Free(rdpContext* context, rdpPointer* pointer)
{
	HCURSOR hCur;

	if (!context || !pointer)
		return;

	hCur = ((wfPointer*)pointer)->cursor;

	if (hCur != 0)
		DestroyIcon(hCur);
}

static BOOL wf_Pointer_Set(rdpContext* context, rdpPointer* pointer)
{
	HCURSOR hCur;
	wfContext* wfc = (wfContext*)context;

	if (!context || !pointer)
		return FALSE;

	hCur = ((wfPointer*)pointer)->cursor;

	if (hCur != nullptr)
	{
		SetCursor(hCur);
		wfc->cursor = hCur;
	}

	return TRUE;
}

static BOOL wf_Pointer_SetNull(rdpContext* context)
{
	if (!context)
		return FALSE;

	return TRUE;
}

static BOOL wf_Pointer_SetDefault(rdpContext* context)
{
	if (!context)
		return FALSE;

	return TRUE;
}

static BOOL wf_Pointer_SetPosition(rdpContext* context, UINT32 x, UINT32 y)
{
	if (!context)
		return FALSE;

	return TRUE;
}

BOOL wf_register_pointer(rdpGraphics* graphics)
{
	wfContext* wfc;
	rdpPointer pointer = WINPR_C_ARRAY_INIT;

	if (!graphics)
		return FALSE;

	wfc = (wfContext*)graphics->context;
	pointer.size = sizeof(wfPointer);
	pointer.New = wf_Pointer_New;
	pointer.Free = wf_Pointer_Free;
	pointer.Set = wf_Pointer_Set;
	pointer.SetNull = wf_Pointer_SetNull;
	pointer.SetDefault = wf_Pointer_SetDefault;
	pointer.SetPosition = wf_Pointer_SetPosition;
	graphics_register_pointer(graphics, &pointer);
	return TRUE;
}

/* Graphics Module */

BOOL wf_register_graphics(rdpGraphics* graphics)
{
	wfContext* wfc;
	rdpGlyph glyph;
	rdpBitmap bitmap;

	if (!graphics)
		return FALSE;

	wfc = (wfContext*)graphics->context;
	bitmap = *graphics->Bitmap_Prototype;
	bitmap.size = sizeof(wfBitmap);
	bitmap.New = wf_Bitmap_New;
	bitmap.Free = wf_Bitmap_Free;
	bitmap.Paint = wf_Bitmap_Paint;
	bitmap.SetSurface = wf_Bitmap_SetSurface;
	graphics_register_bitmap(graphics, &bitmap);
	glyph = *graphics->Glyph_Prototype;
	graphics_register_glyph(graphics, &glyph);
	return TRUE;
}

/* ---- HiDef RemoteApp: render a GFX surface mapped to a window ---------------
 *
 * Some RDP servers deliver RemoteApp content not through RAIL window orders but
 * through the graphics pipeline: a GFX surface is created and mapped to a window
 * via RDPGFX_CMDID_MAPSURFACETOWINDOW. The shared gdi layer then dispatches
 * surface updates to context->UpdateWindowFromSurface. The Windows client
 * previously did not implement that callback, so the decoded application surface
 * was silently dropped and nothing was shown.
 *
 * Threading: the GFX surface callbacks run on the dynamic-virtual-channel
 * thread, but a Win32 top-level window only receives WM_PAINT if it is created
 * and pumped on a thread that runs a message loop (the main UI thread). So the
 * GFX thread only stages the decoded pixels into wfc->gfxData (under gfxLock)
 * and posts WM_FREERDP_GFX_UPDATE; the main thread (wf_event.c message loop)
 * creates/sizes/shows the window and blits. */

#define WF_GFX_WND_CLASS _T("RdpGfxAppWindow")

static LRESULT CALLBACK wf_gfx_wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	wfContext* wfc = (wfContext*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (msg)
	{
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			if (wfc && wfc->gfxHdc)
			{
				const int w = ps.rcPaint.right - ps.rcPaint.left;
				const int h = ps.rcPaint.bottom - ps.rcPaint.top;
				BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, w, h, wfc->gfxHdc, ps.rcPaint.left,
				       ps.rcPaint.top, SRCCOPY);
			}
			EndPaint(hWnd, &ps);
			return 0;
		}
		case WM_CLOSE:
			/* Closing the RemoteApp window ends the session (seamless lifecycle). */
			if (wfc)
				freerdp_abort_connect_context(&wfc->common.context);
			return 0;
		default:
			break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void wf_gfx_window_free(wfContext* wfc)
{
	if (!wfc)
		return;
	if (wfc->gfxBitmap)
	{
		DeleteObject(wfc->gfxBitmap);
		wfc->gfxBitmap = nullptr;
	}
	if (wfc->gfxHdc)
	{
		DeleteDC(wfc->gfxHdc);
		wfc->gfxHdc = nullptr;
	}
	if (wfc->gfxWnd)
	{
		DestroyWindow(wfc->gfxWnd);
		wfc->gfxWnd = nullptr;
	}
	if (wfc->gfxData)
	{
		free(wfc->gfxData);
		wfc->gfxData = nullptr;
	}
	wfc->gfxDibBits = nullptr;
	wfc->gfxWidth = 0;
	wfc->gfxHeight = 0;
	if (wfc->gfxLockValid)
	{
		DeleteCriticalSection(&wfc->gfxLock);
		wfc->gfxLockValid = FALSE;
	}
}

/* Main-thread handler for WM_FREERDP_GFX_UPDATE: (re)create the window + backing
 * DIB to match the surface size, copy the staged pixels in, and repaint. */
void wf_gfx_window_handle_update(wfContext* wfc, int width, int height)
{
	WINPR_ASSERT(wfc);

	if ((width <= 0) || (height <= 0))
		return;

	if (!wfc->gfxWnd)
	{
		WNDCLASSEX wndClassEx = { 0 };
		HINSTANCE hInstance = GetModuleHandle(nullptr);
		wndClassEx.cbSize = sizeof(WNDCLASSEX);
		wndClassEx.lpfnWndProc = wf_gfx_wnd_proc;
		wndClassEx.hInstance = hInstance;
		wndClassEx.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wndClassEx.lpszClassName = WF_GFX_WND_CLASS;
		RegisterClassEx(&wndClassEx);

		wfc->gfxWnd = CreateWindowEx(WS_EX_APPWINDOW, WF_GFX_WND_CLASS,
		                             wfc->window_title ? wfc->window_title : _T("RemoteApp"),
		                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
		                                 WS_MAXIMIZEBOX | WS_SIZEBOX,
		                             CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr,
		                             hInstance, nullptr);
		if (!wfc->gfxWnd)
		{
			WLog_ERR(TAG, "GFX app window CreateWindowEx failed: %lu", GetLastError());
			return;
		}
		SetWindowLongPtr(wfc->gfxWnd, GWLP_USERDATA, (LONG_PTR)wfc);
	}

	if ((wfc->gfxWidth != width) || (wfc->gfxHeight != height) || !wfc->gfxBitmap)
	{
		HDC screenDc = GetDC(nullptr);
		BITMAPINFO bmi = { 0 };

		if (wfc->gfxBitmap)
		{
			DeleteObject(wfc->gfxBitmap);
			wfc->gfxBitmap = nullptr;
		}
		if (!wfc->gfxHdc)
			wfc->gfxHdc = CreateCompatibleDC(screenDc);

		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height; /* top-down */
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		wfc->gfxBitmap = CreateDIBSection(wfc->gfxHdc, &bmi, DIB_RGB_COLORS,
		                                  (void**)&wfc->gfxDibBits, nullptr, 0);
		ReleaseDC(nullptr, screenDc);
		if (!wfc->gfxBitmap)
		{
			WLog_ERR(TAG, "GFX app window CreateDIBSection failed");
			return;
		}
		SelectObject(wfc->gfxHdc, wfc->gfxBitmap);
		wfc->gfxWidth = width;
		wfc->gfxHeight = height;

		/* size the client area to the surface */
		RECT rc = { 0, 0, width, height };
		AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtr(wfc->gfxWnd, GWL_STYLE), FALSE,
		                   (DWORD)GetWindowLongPtr(wfc->gfxWnd, GWL_EXSTYLE));
		SetWindowPos(wfc->gfxWnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	/* Copy the staged pixels into the DIB under the lock. */
	if (wfc->gfxLockValid && wfc->gfxDibBits && wfc->gfxData)
	{
		EnterCriticalSection(&wfc->gfxLock);
		CopyMemory(wfc->gfxDibBits, wfc->gfxData, (size_t)width * (size_t)height * 4);
		LeaveCriticalSection(&wfc->gfxLock);
	}

	if (!IsWindowVisible(wfc->gfxWnd))
		ShowWindow(wfc->gfxWnd, SW_SHOW);
	InvalidateRect(wfc->gfxWnd, nullptr, FALSE);
	UpdateWindow(wfc->gfxWnd);
}

static UINT wf_MapWindowForSurface(RdpgfxClientContext* context, UINT16 surfaceId, UINT64 windowId)
{
	WINPR_ASSERT(context);
	rdpGdi* gdi = (rdpGdi*)context->custom;
	WINPR_ASSERT(gdi);
	wfContext* wfc = (wfContext*)gdi->context;
	WINPR_ASSERT(wfc);

	WINPR_UNUSED(surfaceId);
	wfc->gfxWindowId = windowId;
	/* The window is created lazily on the first surface update (on the main
	 * thread), when we know the surface dimensions. */
	return CHANNEL_RC_OK;
}

static UINT wf_UnmapWindowForSurface(RdpgfxClientContext* context, UINT64 windowId)
{
	WINPR_ASSERT(context);
	rdpGdi* gdi = (rdpGdi*)context->custom;
	WINPR_ASSERT(gdi);
	wfContext* wfc = (wfContext*)gdi->context;
	WINPR_ASSERT(wfc);

	WINPR_UNUSED(windowId);
	/* Defer teardown to the main thread by closing the window; the session
	 * lifecycle (last window) is handled there. Here just drop the mapping. */
	wfc->gfxWindowId = 0;
	return CHANNEL_RC_OK;
}

static UINT wf_UpdateWindowFromSurface(RdpgfxClientContext* context, gdiGfxSurface* surface)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(surface);
	rdpGdi* gdi = (rdpGdi*)context->custom;
	WINPR_ASSERT(gdi);
	wfContext* wfc = (wfContext*)gdi->context;
	WINPR_ASSERT(wfc);

	const int width = (int)surface->mappedWidth;
	const int height = (int)surface->mappedHeight;
	if ((width <= 0) || (height <= 0))
		return CHANNEL_RC_OK;

	if (!wfc->gfxLockValid)
	{
		InitializeCriticalSection(&wfc->gfxLock);
		wfc->gfxLockValid = TRUE;
	}

	/* (Re)allocate the staging buffer and copy/convert the decoded surface into
	 * it (BGRX32). All window/GDI work happens on the main thread. */
	EnterCriticalSection(&wfc->gfxLock);
	const size_t needed = (size_t)width * (size_t)height * 4;
	BYTE* staging = (BYTE*)realloc(wfc->gfxData, needed);
	if (staging)
	{
		wfc->gfxData = staging;
		if (!freerdp_image_copy(wfc->gfxData, PIXEL_FORMAT_BGRX32, 0, 0, 0, (UINT32)width,
		                        (UINT32)height, surface->data, surface->format, surface->scanline, 0,
		                        0, &gdi->palette, FREERDP_FLIP_NONE))
		{
			LeaveCriticalSection(&wfc->gfxLock);
			return ERROR_INTERNAL_ERROR;
		}
	}
	LeaveCriticalSection(&wfc->gfxLock);
	if (!staging)
		return ERROR_NOT_ENOUGH_MEMORY;

	/* Ask the main thread to create/size/show the window and repaint. */
	if (wfc->hwnd)
		PostMessage(wfc->hwnd, WM_FREERDP_GFX_UPDATE, (WPARAM)width, (LPARAM)height);

	region16_clear(&surface->invalidRegion);
	return CHANNEL_RC_OK;
}

void wf_graphics_pipeline_init(wfContext* wfc, RdpgfxClientContext* gfx)
{
	rdpGdi* gdi = nullptr;
	rdpSettings* settings = nullptr;

	WINPR_ASSERT(wfc);
	WINPR_ASSERT(gfx);

	settings = wfc->common.context.settings;
	WINPR_ASSERT(settings);

	gdi = wfc->common.context.gdi;

	wfc->gfx = gfx;

	/* The Windows client renders the graphics pipeline through the shared GDI
	 * backend (the decoded GFX surfaces are blitted into gdi->primary, which is
	 * the same DIB the WM_PAINT handler copies to the window). Wiring this up is
	 * what actually enables the RDPGFX channel (AVC420/AVC444/progressive/...)
	 * for the Windows client. Without it the GFX DVC stays unhandled. */
	gdi_graphics_pipeline_init(gdi, gfx);

	/* HiDef RemoteApp: also render surfaces that the server maps to a window
	 * (RDPGFX_CMDID_MAPSURFACETOWINDOW). Without these callbacks the shared gdi
	 * layer drops window-mapped surfaces and the RemoteApp window never appears.
	 * Only relevant in RemoteApp mode; in desktop mode the surface is output-
	 * mapped and rendered through gdi->primary as before. */
	if (freerdp_settings_get_bool(settings, FreeRDP_RemoteApplicationMode))
	{
		gfx->MapWindowForSurface = wf_MapWindowForSurface;
		gfx->UnmapWindowForSurface = wf_UnmapWindowForSurface;
		gfx->UpdateWindowFromSurface = wf_UpdateWindowFromSurface;
	}
}

void wf_graphics_pipeline_uninit(wfContext* wfc, RdpgfxClientContext* gfx)
{
	rdpGdi* gdi = nullptr;

	WINPR_ASSERT(wfc);
	WINPR_ASSERT(gfx);

	gdi = wfc->common.context.gdi;

	gdi_graphics_pipeline_uninit(gdi, gfx);

	wf_gfx_window_free(wfc);

	wfc->gfx = nullptr;
}
