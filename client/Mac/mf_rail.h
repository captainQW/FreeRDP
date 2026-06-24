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

#ifndef FREERDP_CLIENT_MAC_RAIL_H
#define FREERDP_CLIENT_MAC_RAIL_H

#include <freerdp/client/rail.h>

#include "mfreerdp.h"

#ifdef __cplusplus
extern "C"
{
#endif

	/**
	 * Initialize the macOS RAIL (RemoteApp) integration. Registers the window
	 * order update callbacks and the client-side RAIL PDU handlers. Called from
	 * the channel connected handler when the "rail" channel comes up.
	 */
	BOOL mac_rail_init(mfContext* mfc, RailClientContext* rail);

	/**
	 * Tear down the macOS RAIL integration and destroy all native RemoteApp
	 * windows.
	 */
	void mac_rail_uninit(mfContext* mfc, RailClientContext* rail);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_MAC_RAIL_H */
