// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "client.h"

#include "inject.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


enum {
	READY_TIMEOUT_MS = 10000, // from injection until the DLL has created its pipe
	READY_POLL_MS = 50,
};

static bool validResult(char *result, size_t length);
static HANDLE connectHarness(unsigned timeoutMs);
static bool
transfer(HANDLE pipe, bool write, void *data, DWORD size, unsigned timeoutMs, DWORD *done);

bool client_submit(const char *job, unsigned timeoutMs, char *result, size_t resultSize) {
	char reply[HARNESS_RESULT_LENGTH];
	if (!result) {
		result = reply;
		resultSize = sizeof reply;
	}
	if (resultSize < 2) {
		fprintf(stderr, "result buffer is too small\n");
		return false;
	}
	result[0] = 0;
	size_t length = strlen(job);
	if (length > HARNESS_JOB_LENGTH) {
		fprintf(stderr, "the job is too long\n");
		return false;
	}
	ULONGLONG started = GetTickCount64();
	HANDLE pipe = connectHarness(timeoutMs);
	if (!pipe) {
		return false;
	}
	ULONGLONG waited = GetTickCount64() - started;
	unsigned remaining = waited < timeoutMs ? timeoutMs - (unsigned)waited : 0;
	DWORD mode = PIPE_READMODE_MESSAGE;
	DWORD count = 0;
	bool ok = SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
	if (!ok) {
		fprintf(stderr, "cannot set up the harness pipe (%lu)\n", GetLastError());
	}
	ok = ok && transfer(pipe, true, (void *)job, (DWORD)length, remaining, &count) &&
			 transfer(pipe, false, result, (DWORD)(resultSize - 1), remaining, &count);
	CloseHandle(pipe);
	return ok && validResult(result, count);
}

bool client_succeeded(const char *result) {
	return strncmp(result, "ok", 2) == 0 && (result[2] == 0 || result[2] == ':');
}

// Terminates a reply of `length` bytes; false (and empty) unless it is one non-empty line.
static bool validResult(char *result, size_t length) {
	result[length] = 0;
	bool ok = length > 0 && !memchr(result, 0, length);
	size_t lineLength = strcspn(result, "\r\n");
	if (lineLength < length) {
		size_t ending = result[lineLength] == '\r' && result[lineLength + 1] == '\n' ? 2 : 1;
		ok = ok && lineLength + ending == length;
	}
	if (!ok) {
		result[0] = 0;
		fprintf(stderr, "cannot read a complete result; see harness.log\n");
	}
	return ok;
}

// Connects to the harness pipe, injecting the DLL first if this protocol's harness is not loaded
// yet, and waiting while another runner's job holds the pipe. Prints why and returns NULL on
// failure.
static HANDLE connectHarness(unsigned timeoutMs) {
	ULONGLONG started = GetTickCount64();
	ULONGLONG injected = 0;
	bool announced = false;
	for (;;) {
		HANDLE pipe = CreateFileW(
			HARNESS_PIPE_NAME,
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL
		);
		if (pipe != INVALID_HANDLE_VALUE) {
			return pipe;
		}
		DWORD error = GetLastError();
		ULONGLONG now = GetTickCount64();
		if (error == ERROR_PIPE_BUSY) {
			if (!announced) {
				fprintf(stderr, "the harness is running another job; waiting for it to finish\n");
				announced = true;
			}
			if (now - started >= timeoutMs) {
				fprintf(stderr, "timed out waiting for the other job\n");
				return NULL;
			}
			ULONGLONG left = timeoutMs - (now - started);
			(void)WaitNamedPipeW(HARNESS_PIPE_NAME, left < MAXDWORD - 1 ? (DWORD)left + 1 : MAXDWORD - 1);
		} else if (error != ERROR_FILE_NOT_FOUND) {
			fprintf(stderr, "cannot connect to the harness (%lu)\n", error);
			return NULL;
		} else if (!injected) {
			if (!inject_harness()) {
				return NULL;
			}
			injected = GetTickCount64();
		} else if (now - injected >= READY_TIMEOUT_MS) {
			// The DLL creates its pipe once its build check has passed.
			fprintf(stderr, "the harness did not start in fm.exe (wrong game build?); see harness.log\n");
			return NULL;
		} else {
			Sleep(READY_POLL_MS);
		}
	}
}

// Writes or reads one message on the overlapped pipe, waiting at most `timeoutMs`. A timeout
// cancels only the transfer: the DLL keeps running the job.
static bool
transfer(HANDLE pipe, bool write, void *data, DWORD size, unsigned timeoutMs, DWORD *done) {
	OVERLAPPED overlapped = {.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)};
	if (!overlapped.hEvent) {
		fprintf(stderr, "cannot create an event (%lu)\n", GetLastError());
		return false;
	}
	*done = 0;
	BOOL started = write ? WriteFile(pipe, data, size, NULL, &overlapped)
											 : ReadFile(pipe, data, size, NULL, &overlapped);
	bool ok = started || GetLastError() == ERROR_IO_PENDING;
	if (ok && !started && WaitForSingleObject(overlapped.hEvent, timeoutMs) != WAIT_OBJECT_0) {
		CancelIo(pipe);
		(void)GetOverlappedResult(pipe, &overlapped, done, TRUE);
		fprintf(stderr, "timed out; the harness may still be running\n");
		CloseHandle(overlapped.hEvent);
		return false;
	}
	ok = ok && GetOverlappedResult(pipe, &overlapped, done, FALSE);
	if (!ok) {
		DWORD error = GetLastError();
		if (error == ERROR_MORE_DATA) {
			fprintf(stderr, "the harness's result is too long\n");
		} else {
			fprintf(
				stderr,
				"cannot %s the harness (%lu); see harness.log\n",
				write ? "write to" : "read from",
				error
			);
		}
	} else if (write && *done != size) {
		fprintf(stderr, "cannot send the whole job to the harness\n");
		ok = false;
	}
	CloseHandle(overlapped.hEvent);
	return ok;
}
