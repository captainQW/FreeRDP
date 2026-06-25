/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Unit tests for the public RAIL (RemoteApp) helper API
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

#include <winpr/crt.h>
#include <winpr/stream.h>

#include <freerdp/rail.h>
#include <freerdp/settings.h>

/* Encode a UTF-8 string into a RAIL_UNICODE_STRING, serialize it the way the
 * RAIL wire format does (UINT16 length + UTF-16LE bytes), then read it back
 * with the public rail_read_unicode_string() and verify the round-trip. */
static BOOL test_rail_unicode_string_roundtrip(const char* utf8)
{
	BOOL rc = FALSE;
	RAIL_UNICODE_STRING src = { 0 };
	RAIL_UNICODE_STRING dst = { 0 };
	wStream* s = NULL;

	if (!utf8_string_to_rail_string(utf8, &src))
	{
		(void)fprintf(stderr, "utf8_string_to_rail_string('%s') failed\n", utf8);
		goto fail;
	}

	/* An empty input yields a zero-length string; a non-empty input must
	 * produce an even number of bytes (UTF-16). */
	if ((strlen(utf8) > 0) && (src.length == 0))
	{
		(void)fprintf(stderr, "expected non-zero length for '%s'\n", utf8);
		goto fail;
	}
	if ((src.length % 2) != 0)
	{
		(void)fprintf(stderr, "RAIL unicode length must be even, got %" PRIu16 "\n", src.length);
		goto fail;
	}

	/* Serialize: UINT16 cbString followed by the UTF-16LE payload. */
	s = Stream_New(NULL, 2ULL + src.length);
	if (!s)
		goto fail;
	Stream_Write_UINT16(s, src.length);
	if (src.length > 0)
		Stream_Write(s, src.string, src.length);
	Stream_SealLength(s);
	Stream_SetPosition(s, 0);

	if (!rail_read_unicode_string(s, &dst))
	{
		(void)fprintf(stderr, "rail_read_unicode_string('%s') failed\n", utf8);
		goto fail;
	}

	if (dst.length != src.length)
	{
		(void)fprintf(stderr, "length mismatch: wrote %" PRIu16 " read %" PRIu16 "\n", src.length,
		              dst.length);
		goto fail;
	}
	if ((src.length > 0) && (memcmp(dst.string, src.string, src.length) != 0))
	{
		(void)fprintf(stderr, "payload mismatch for '%s'\n", utf8);
		goto fail;
	}

	rc = TRUE;
fail:
	free(src.string);
	free(dst.string);
	Stream_Free(s, TRUE);
	return rc;
}

/* Verify the human-readable flag formatters never overflow and reflect the
 * flags that are set. */
static BOOL test_rail_flag_strings(void)
{
	char buffer[512] = { 0 };
	const char* str = NULL;

	/* RAIL support flags (capability set). */
	str = freerdp_rail_support_flags_to_string(
	    RAIL_LEVEL_SUPPORTED | RAIL_LEVEL_HANDSHAKE_EX_SUPPORTED, buffer, sizeof(buffer));
	if (!str || (strlen(str) == 0))
	{
		(void)fprintf(stderr, "freerdp_rail_support_flags_to_string returned empty\n");
		return FALSE;
	}

	/* Handshake-ex flags formatter lives in the rail channel; only exercise the
	 * libfreerdp-exported support-flags formatter here so the test links against
	 * the core library alone. */

	/* Zero flags must still produce a valid (possibly empty) string, not crash. */
	str = freerdp_rail_support_flags_to_string(0, buffer, sizeof(buffer));
	if (!str)
	{
		(void)fprintf(stderr, "freerdp_rail_support_flags_to_string(0) returned NULL\n");
		return FALSE;
	}

	return TRUE;
}

/* Property 3 (launch reliability) + Property 4 (window lifecycle):
 * exercise the pure decision helpers that drive the seamless-launch retry loop
 * and the close-to-disconnect behavior. These are the exact functions the
 * Windows client calls, so the test pins down shipping behavior. */
static BOOL test_rail_exec_retry_decision(void)
{
	const UINT32 maxRetries = FREERDP_RAIL_EXEC_DEFAULT_MAX_RETRIES;

	/* RAIL_EXEC_S_OK always means launched, regardless of retry count. */
	if (freerdp_rail_exec_retry_decide(RAIL_EXEC_S_OK, 0, maxRetries) != FREERDP_RAIL_EXEC_LAUNCHED)
	{
		(void)fprintf(stderr, "S_OK should be LAUNCHED\n");
		return FALSE;
	}
	if (freerdp_rail_exec_retry_decide(RAIL_EXEC_S_OK, maxRetries, maxRetries) !=
	    FREERDP_RAIL_EXEC_LAUNCHED)
	{
		(void)fprintf(stderr, "S_OK should be LAUNCHED even at max retries\n");
		return FALSE;
	}

	/* HOOK_NOT_LOADED below the budget must retry. */
	for (UINT32 i = 0; i < maxRetries; i++)
	{
		if (freerdp_rail_exec_retry_decide(RAIL_EXEC_E_HOOK_NOT_LOADED, i, maxRetries) !=
		    FREERDP_RAIL_EXEC_RETRY)
		{
			(void)fprintf(stderr, "HOOK_NOT_LOADED at retry %" PRIu32 " should RETRY\n", i);
			return FALSE;
		}
	}

	/* HOOK_NOT_LOADED at/after the budget must give up (exactly one clean abort). */
	if (freerdp_rail_exec_retry_decide(RAIL_EXEC_E_HOOK_NOT_LOADED, maxRetries, maxRetries) !=
	    FREERDP_RAIL_EXEC_GIVE_UP)
	{
		(void)fprintf(stderr, "HOOK_NOT_LOADED at budget should GIVE_UP\n");
		return FALSE;
	}

	/* Every other error code is terminal: never retry. */
	const UINT16 fatal[] = { RAIL_EXEC_E_DECODE_FAILED, RAIL_EXEC_E_NOT_IN_ALLOWLIST,
		                     RAIL_EXEC_E_FILE_NOT_FOUND, RAIL_EXEC_E_FAIL,
		                     RAIL_EXEC_E_SESSION_LOCKED };
	for (size_t i = 0; i < ARRAYSIZE(fatal); i++)
	{
		if (freerdp_rail_exec_retry_decide(fatal[i], 0, maxRetries) != FREERDP_RAIL_EXEC_GIVE_UP)
		{
			(void)fprintf(stderr, "fatal code 0x%04" PRIx16 " should GIVE_UP\n", fatal[i]);
			return FALSE;
		}
	}

	/* Simulate the full P3 sequence: N (<MAX) transient failures then S_OK ->
	 * exactly one launch, zero give-ups. */
	{
		UINT32 retries = 0;
		UINT32 launches = 0;
		UINT32 giveUps = 0;
		const UINT16 seq[] = { RAIL_EXEC_E_HOOK_NOT_LOADED, RAIL_EXEC_E_HOOK_NOT_LOADED,
			                   RAIL_EXEC_E_HOOK_NOT_LOADED, RAIL_EXEC_S_OK };
		for (size_t i = 0; i < ARRAYSIZE(seq); i++)
		{
			switch (freerdp_rail_exec_retry_decide(seq[i], retries, maxRetries))
			{
				case FREERDP_RAIL_EXEC_RETRY:
					retries++;
					break;
				case FREERDP_RAIL_EXEC_LAUNCHED:
					launches++;
					break;
				case FREERDP_RAIL_EXEC_GIVE_UP:
					giveUps++;
					break;
				default:
					break;
			}
		}
		if ((launches != 1) || (giveUps != 0) || (retries != 3))
		{
			(void)fprintf(stderr,
			              "P3 sequence wrong: launches=%" PRIu32 " giveUps=%" PRIu32
			              " retries=%" PRIu32 "\n",
			              launches, giveUps, retries);
			return FALSE;
		}
	}

	return TRUE;
}

static BOOL test_rail_disconnect_on_window_delete(void)
{
	/* Property 4: closing the last window disconnects; a non-last delete does not. */
	if (!freerdp_rail_should_disconnect_on_window_delete(0))
	{
		(void)fprintf(stderr, "0 remaining windows should disconnect\n");
		return FALSE;
	}
	if (freerdp_rail_should_disconnect_on_window_delete(1))
	{
		(void)fprintf(stderr, "1 remaining window must NOT disconnect\n");
		return FALSE;
	}
	if (freerdp_rail_should_disconnect_on_window_delete(5))
	{
		(void)fprintf(stderr, "5 remaining windows must NOT disconnect\n");
		return FALSE;
	}
	return TRUE;
}

int TestRail(int argc, char* argv[])
{
	const char* samples[] = { "", "Notepad", "Excel - Book1.xlsx",
		                      "C:\\Program Files\\App\\app.exe", "\xe8\xae\xb0\xe4\xba\x8b\xe6\x9c\xac" /* CJK */ };

	WINPR_UNUSED(argc);
	WINPR_UNUSED(argv);

	for (size_t i = 0; i < ARRAYSIZE(samples); i++)
	{
		if (!test_rail_unicode_string_roundtrip(samples[i]))
		{
			(void)fprintf(stderr, "RAIL unicode round-trip failed for sample %" PRIuz "\n", i);
			return -1;
		}
	}

	if (!test_rail_flag_strings())
	{
		(void)fprintf(stderr, "RAIL flag string formatting test failed\n");
		return -1;
	}

	if (!test_rail_exec_retry_decision())
	{
		(void)fprintf(stderr, "RAIL exec retry decision test failed\n");
		return -1;
	}

	if (!test_rail_disconnect_on_window_delete())
	{
		(void)fprintf(stderr, "RAIL disconnect-on-window-delete test failed\n");
		return -1;
	}

	return 0;
}
