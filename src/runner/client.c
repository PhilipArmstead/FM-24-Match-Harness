// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "client.h"

#include "files.h"
#include "inject.h"
#include "paths.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


enum {
	READY_TIMEOUT_MS = 10000, // from injection until the DLL has created its events
	READY_POLL_MS = 50,
};

static bool readResult(const wchar_t *path, char *result, size_t size);
static bool openEvents(HANDLE *go, HANDLE *done);
static bool connectHarness(HANDLE *go, HANDLE *done);

bool client_submit(const char *job, unsigned timeoutMs, char *result, size_t resultSize) {
	HANDLE go = NULL;
	HANDLE done = NULL;
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t jobPath[HARNESS_PATH_LENGTH];
	wchar_t resultPath[HARNESS_PATH_LENGTH];
	bool ok = connectHarness(&go, &done);
	ok = ok && paths_executableDirectory(directory, HARNESS_PATH_LENGTH) &&
			 paths_join(jobPath, HARNESS_PATH_LENGTH, directory, HARNESS_JOB_FILE) &&
			 paths_join(resultPath, HARNESS_PATH_LENGTH, directory, HARNESS_RESULT_FILE);
	if (ok) {
		ok = DeleteFileW(resultPath) || GetLastError() == ERROR_FILE_NOT_FOUND;
		ok = ok && files_write(jobPath, job, strlen(job));
		if (!ok) {
			fprintf(stderr, "cannot prepare the job and result files\n");
		}
	}
	if (ok) {
		ok = SetEvent(go) != 0;
		if (!ok) {
			fprintf(stderr, "cannot signal the harness (%lu)\n", GetLastError());
		}
	}
	if (ok) {
		DWORD waited = WaitForSingleObject(done, timeoutMs);
		ok = waited == WAIT_OBJECT_0;
		if (waited == WAIT_TIMEOUT) {
			fprintf(stderr, "timed out; the harness may still be running\n");
		} else if (!ok) {
			fprintf(stderr, "cannot wait for the harness (%lu)\n", GetLastError());
		}
	}
	if (ok && result) {
		ok = readResult(resultPath, result, resultSize);
	}
	if (go) {
		CloseHandle(go);
	}
	if (done) {
		CloseHandle(done);
	}
	return ok;
}

bool client_succeeded(const char *result) {
	return strncmp(result, "ok", 2) == 0 && (result[2] == 0 || result[2] == ':');
}

static bool readResult(const wchar_t *path, char *result, size_t size) {
	if (size < 2) {
		fprintf(stderr, "result buffer is too small\n");
		return false;
	}
	result[0] = 0;
	FILE *file = _wfopen(path, L"rb");
	if (!file) {
		fprintf(stderr, "cannot open the result file; see harness.log\n");
		return false;
	}
	size_t length = fread(result, 1, size - 1, file);
	bool ok = length > 0 && !memchr(result, 0, length) && fgetc(file) == EOF && !ferror(file);
	result[length] = 0;
	size_t lineLength = strcspn(result, "\r\n");
	if (lineLength < length) {
		size_t ending = result[lineLength] == '\r' && result[lineLength + 1] == '\n' ? 2 : 1;
		ok = ok && lineLength + ending == length;
	}
	ok = fclose(file) == 0 && ok;
	if (!ok) {
		result[0] = 0;
		fprintf(stderr, "cannot read a complete result; see harness.log\n");
	}
	return ok;
}

// Opens the DLL's events; false if this protocol's harness is not running in fm.exe.
static bool openEvents(HANDLE *go, HANDLE *done) {
	*go = OpenEventW(EVENT_MODIFY_STATE, FALSE, HARNESS_EVENT_GO);
	*done = OpenEventW(SYNCHRONIZE, FALSE, HARNESS_EVENT_DONE);
	if (*go && *done) {
		return true;
	}
	if (*go) {
		CloseHandle(*go);
	}
	if (*done) {
		CloseHandle(*done);
	}
	*go = NULL;
	*done = NULL;
	return false;
}

// Connects to the harness, injecting it first if this protocol's DLL is not loaded yet (a no-op
// otherwise). Prints why and returns false if it cannot be started.
static bool connectHarness(HANDLE *go, HANDLE *done) {
	if (openEvents(go, done)) {
		return true;
	}
	if (!inject_harness()) {
		return false;
	}
	// The DLL creates its events once its build check has passed.
	for (unsigned waited = 0; waited < READY_TIMEOUT_MS; waited += READY_POLL_MS) {
		if (openEvents(go, done)) {
			return true;
		}
		Sleep(READY_POLL_MS);
	}
	fprintf(stderr, "the harness did not start in fm.exe (wrong game build?); see harness.log\n");
	return false;
}
