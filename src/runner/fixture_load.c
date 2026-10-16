// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_load.h"

#include "client.h"
#include "files.h"
#include "match_config.h"
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
			(void)snprintf(error, errorSize, "cannot read the decoded fixture");
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
			(void)snprintf(error, errorSize, "cannot read fixture");
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

static bool writeFixture(
	const wchar_t *input,
	const wchar_t *output,
	const ByteBuffer *bytes,
	char *error,
	size_t errorSize
) {
	wchar_t destination[HARNESS_PATH_LENGTH];
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t temporary[HARNESS_PATH_LENGTH] = L"";
	bool ok = paths_absolute(output, destination, HARNESS_PATH_LENGTH);
	if (ok) {
		wcscpy(directory, destination);
		wchar_t *separator = wcsrchr(directory, L'\\');
		ok = separator != NULL;
		if (ok) {
			separator[1] = 0;
			ok = GetTempFileNameW(directory, L"fms", 0, temporary) != 0;
		}
	}
	if (!ok) {
		temporary[0] = 0;
		(void)snprintf(
			error, errorSize, "Cannot create a save file (Windows error %lu).", GetLastError()
		);
	}
	if (ok) {
		ok = bytes ? files_write(temporary, bytes->data, bytes->length)
							 : CopyFileW(input, temporary, FALSE) != 0;
		if (!ok) {
			(void)snprintf(
				error, errorSize, "Cannot write the fixture. The destination was not changed."
			);
		} else if (!MoveFileExW(
								 temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
							 )) {
			(void)snprintf(
				error, errorSize, "Cannot replace the destination (Windows error %lu).", GetLastError()
			);
			ok = false;
		} else {
			temporary[0] = 0;
		}
	}
	if (temporary[0] && !DeleteFileW(temporary)) {
		fprintf(
			stderr,
			"cannot remove save temporary file %ls (Windows error %lu)\n",
			temporary,
			GetLastError()
		);
	}
	return ok;
}

bool fixtureLoad_save(const wchar_t *input, const wchar_t *output, char *error, size_t errorSize) {
	bool inputBinary = files_hasExtension(input, L".bin");
	bool outputBinary = files_hasExtension(output, L".bin");
	if ((!inputBinary && !files_hasExtension(input, L".json")) ||
			(!outputBinary && !files_hasExtension(output, L".json"))) {
		(void)snprintf(error, errorSize, "Expected a .json or .bin fixture.");
		return false;
	}
	ByteBuffer text = {0};
	ByteBuffer encoded = {0};
	bool ok = true;
	if (inputBinary != outputBinary) {
		if (inputBinary) {
			ok = decodeBinary(input, &text, error, errorSize);
		} else if (!files_read(input, &text)) {
			(void)snprintf(error, errorSize, "Cannot read the current fixture.");
			ok = false;
		} else {
			ok = matchConfig_encode(buffer_string(&text), text.length, &encoded, NULL, error, errorSize);
		}
	}
	if (ok) {
		ok = writeFixture(
			input,
			output,
			inputBinary == outputBinary ? NULL
			: outputBinary              ? &encoded
																	: &text,
			error,
			errorSize
		);
	}
	buffer_free(&encoded);
	buffer_free(&text);
	return ok;
}
