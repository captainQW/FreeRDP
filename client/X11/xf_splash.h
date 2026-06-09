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

#ifndef FREERDP_CLIENT_X11_SPLASH_H
#define FREERDP_CLIENT_X11_SPLASH_H

#include <X11/X.h>
#include <X11/Xlib.h>

#include "xf_types.h"

/**
 * A small, borderless, centered window that is shown while a RemoteApp is being
 * launched on the server. It displays a "Opening application <name>" message so
 * the user gets immediate feedback instead of a blank screen between the RDP
 * connection completing and the remote application window actually appearing.
 *
 * The splash is created when RemoteApp mode is entered and is destroyed as soon
 * as the first real RAIL window shows up (or the launch fails / the connection
 * aborts).
 */

/**
 * Show the launch splash for the given application name.
 * Safe to call multiple times; subsequent calls update the message.
 *
 * @param xfc the X11 client context
 * @param appName the remote application program/name (may be NULL)
 * @return TRUE on success, FALSE on failure
 */
BOOL xf_splash_show(xfContext* xfc, const char* appName);

/**
 * Hide and destroy the launch splash if it is currently shown.
 * No-op when no splash is active.
 */
void xf_splash_hide(xfContext* xfc);

/**
 * Returns TRUE if the given X11 window belongs to the active splash.
 */
BOOL xf_splash_is_window(xfContext* xfc, Window window);

/**
 * Returns TRUE if a launch splash is currently shown.
 */
BOOL xf_splash_active(xfContext* xfc);

/**
 * Keep the splash mapped and on top. Call this while RemoteApp output is being
 * received but no real application window exists yet, so the remote session
 * sign-in / desktop is never revealed behind the splash.
 */
void xf_splash_raise(xfContext* xfc);

/**
 * Repaint the splash content. Call this from the Expose event handler when
 * xf_splash_is_window() matched the exposed window.
 */
void xf_splash_handle_expose(xfContext* xfc);

/**
 * Safety net: if the splash has been shown for longer than the internal
 * timeout without an application window appearing, hide it so the user is not
 * stuck on the loading screen forever. Returns TRUE if the splash was
 * dismissed due to timeout.
 */
BOOL xf_splash_check_timeout(xfContext* xfc);

#endif /* FREERDP_CLIENT_X11_SPLASH_H */
