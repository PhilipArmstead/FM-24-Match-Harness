// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_load.h"

#include "client.h"
#include "files.h"
#include "paths.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static bool decodeBinary(const wchar_t *path, ByteBuffer *text, char *error, size_t errorSize) {
	wchar_t input[HARNESS_PATH_LENGTH];
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t temporary[HARNESS_PATH_LENGTH];
	char inputUtf8[HARNESS_PATH_LENGTH * 3];
	char outputUtf8[HARNESS_PATH_LENGTH * 3];
	DWORD length = GetTempPathW(HARNESS_PATH_LENGTH, directory);
	if (!length || length >= HARNESS_PATH_LENGTH ||
			!paths_absolute(path, input, HARNESS_PATH_LENGTH) ||
			!paths_toUtf8(input, inputUtf8, sizeof inputUtf8) ||
			!GetTempFileNameW(directory, L"fmv", 0, temporary)) {
		(void)snprintf(
			error,
			errorSize,
			"cannot resolve input or create a temporary file (Windows error %lu)",
			GetLastError()
		);
		return false;
	}
	bool ok = paths_toUtf8(temporary, outputUtf8, sizeof outputUtf8);
	if (!ok || strpbrk(inputUtf8, "\r\n") || strpbrk(outputUtf8, "\r\n")) {
		(void)snprintf(error, errorSize, "fixture path cannot be represented in a harness job");
		ok = false;
	} else {
		char job[sizeof inputUtf8 + sizeof outputUtf8 + 64];
		char result[CLIENT_RESULT_LENGTH] = "";
		(void)snprintf(
			job, sizeof job, "command=inspect\ninput=%s\noutput=%s\n", inputUtf8, outputUtf8
		);
		ok = client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, sizeof result);
		if (!ok) {
			(void)snprintf(
				error,
				errorSize,
				"cannot decode binary fixture; FM24 must be running and idle. See the console and "
				"harness.log."
			);
		} else if (!client_succeeded(result)) {
			(void)snprintf(error, errorSize, "%s", result);
			ok = false;
		} else if (!(ok = files_read(temporary, text))) {
			(void)snprintf(
				error, errorSize, "cannot read the decoded fixture (Windows error %lu)", GetLastError()
			);
		}
	}
	if (!DeleteFileW(temporary)) {
		fprintf(
			stderr,
			"cannot remove viewer temporary file %ls (Windows error %lu)\n",
			temporary,
			GetLastError()
		);
	}
	return ok;
}

bool fixtureLoad_open(const wchar_t *path, FixtureData *out, char *error, size_t errorSize) {
	ByteBuffer text = {0};
	bool ok;
	if (files_hasExtension(path, L".bin")) {
		ok = decodeBinary(path, &text, error, errorSize);
	} else if (files_hasExtension(path, L".json")) {
		ok = files_read(path, &text);
		if (!ok) {
			(void)snprintf(error, errorSize, "cannot read fixture (Windows error %lu)", GetLastError());
		}
	} else {
		(void)snprintf(error, errorSize, "expected a .json or .bin fixture");
		ok = false;
	}
	if (ok) {
		ok = fixtureData_read(buffer_string(&text), text.length, out, error, errorSize);
	}
	buffer_free(&text);
	return ok;
}
