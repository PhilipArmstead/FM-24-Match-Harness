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

static void readResult(const wchar_t *path, char *result, size_t size);
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
		DeleteFileW(resultPath);
		ok = files_write(jobPath, job, strlen(job));
		if (!ok) {
			fprintf(stderr, "cannot write the job file\n");
		}
	}
	if (ok) {
		SetEvent(go);
		ok = WaitForSingleObject(done, timeoutMs) == WAIT_OBJECT_0;
		if (!ok) {
			fprintf(stderr, "timed out\n");
		}
	}
	if (ok && result) {
		readResult(resultPath, result, resultSize);
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
	return strncmp(result, "ok", 2) == 0;
}

static void readResult(const wchar_t *path, char *result, size_t size) {
	(void)snprintf(result, size, "no result; see harness.log");
	FILE *file = _wfopen(path, L"rb");
	if (!file) {
		return;
	}
	if (!fgets(result, (int)size, file)) {
		result[0] = 0;
	}
	(void)fclose(file);
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
