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

#ifndef FREERDP_CLIENT_X11_TRAY_H
#define FREERDP_CLIENT_X11_TRAY_H

#include <X11/Xlib.h>

#include <freerdp/window.h>

#include "xf_types.h"

/*
 * RAIL notification-icon (system tray) support for the X11 client.
 *
 * Implements the freedesktop.org "System Tray Protocol" (XEmbed) so that
 * RemoteApp notification-area icons ([MS-RDPERP] 2.2.1.3.2 Notification Icon)
 * are docked into the user's panel system tray (tint2, xfce4-panel, plasma,
 * stalonetray, ...). Icon images are forwarded from the server orders, and
 * local clicks are sent back to the server as RAIL Notify Event PDUs so the
 * remote application reacts as it would natively.
 */

/* Create/update/delete a notification icon for (windowId, notifyIconId). */
BOOL xf_tray_notify_icon_create(xfContext* xfc, const WINDOW_ORDER_INFO* orderInfo,
                                const NOTIFY_ICON_STATE_ORDER* notifyIconState);
BOOL xf_tray_notify_icon_update(xfContext* xfc, const WINDOW_ORDER_INFO* orderInfo,
                                const NOTIFY_ICON_STATE_ORDER* notifyIconState);
BOOL xf_tray_notify_icon_delete(xfContext* xfc, const WINDOW_ORDER_INFO* orderInfo);

/* Dispatch an X11 event to the tray subsystem. Returns TRUE if the event was
 * consumed by a tray icon window (expose/click/configure/tray-manager loss). */
BOOL xf_tray_handle_event(xfContext* xfc, const XEvent* event);

/* Release all tray icons and resources (called on RAIL uninit / disconnect). */
void xf_tray_uninit(xfContext* xfc);

#endif /* FREERDP_CLIENT_X11_TRAY_H */
