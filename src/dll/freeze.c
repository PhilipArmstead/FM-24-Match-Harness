// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "freeze.h"

#include "log.h"
#include "memory.h"

#include <stdlib.h>
#include <string.h>
// clang-format off: tlhelp32.h needs windows.h first, which alphabetical include sorting breaks
#include <windows.h>
#include <tlhelp32.h>
// clang-format on


enum {
	PAUSE_ATTEMPTS = 500, // per thread, a millisecond apart
	WATCHDOG_MS = 60000,
	SYSCALL_LENGTH = 2, // 0F 05
};

typedef struct Paused {
	HANDLE *threads;
	size_t count;
	size_t capacity;
} Paused;

typedef struct Work {
	void (*work)(void *context);
	void *context;
} Work;

static bool pauseAll(Paused *paused, DWORD worker);
static bool pauseAtSafePoint(HANDLE thread);
static void resumeAll(Paused *paused);
static DWORD WINAPI runWork(LPVOID parameter);

FreezeResult freeze_run(void (*work)(void *context), void *context) {
	Work request = {work, context};
	// The worker starts suspended so it can be left running while everything else is paused.
	DWORD workerId = 0;
	HANDLE worker = CreateThread(NULL, 0, runWork, &request, CREATE_SUSPENDED, &workerId);
	if (!worker) {
		log_message("freeze: cannot create the worker (Windows error %lu)", GetLastError());
		return FREEZE_NOT_PAUSED;
	}
	Paused paused = {0};
	if (!pauseAll(&paused, workerId)) {
		resumeAll(&paused);
		TerminateThread(worker, 0); // never started: safe to discard
		CloseHandle(worker);
		return FREEZE_NOT_PAUSED;
	}
	ResumeThread(worker);
	FreezeResult result = FREEZE_OK;
	if (WaitForSingleObject(worker, WATCHDOG_MS) != WAIT_OBJECT_0) {
		log_message("freeze: the work outlasted %d ms; resuming the game's threads", WATCHDOG_MS);
		result = FREEZE_TIMED_OUT;
	}
	resumeAll(&paused);
	WaitForSingleObject(worker, INFINITE);
	CloseHandle(worker);
	return result;
}

// Pauses every thread of the process except the caller and `worker`.
// shortcut: one snapshot; a thread the game starts meanwhile keeps running. Take a second snapshot
// if that ever matters (an idle game starts none).
static bool pauseAll(Paused *paused, DWORD worker) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		log_message("freeze: cannot list threads (Windows error %lu)", GetLastError());
		return false;
	}
	THREADENTRY32 entry = {.dwSize = sizeof entry};
	bool ok = true;
	for (BOOL more = Thread32First(snapshot, &entry); more && ok;
		more = Thread32Next(snapshot, &entry)) {
		if (entry.th32OwnerProcessID != GetCurrentProcessId() ||
				entry.th32ThreadID == GetCurrentThreadId() || entry.th32ThreadID == worker) {
			continue;
		}
		HANDLE thread = OpenThread(
			THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
			FALSE,
			entry.th32ThreadID
		);
		if (!thread) {
			continue; // already exited
		}
		if (paused->count == paused->capacity) {
			size_t grown = paused->capacity ? paused->capacity * 2 : 64;
			HANDLE *threads = realloc(paused->threads, grown * sizeof *threads);
			if (!threads) {
				CloseHandle(thread);
				ok = false;
				break;
			}
			paused->threads = threads;
			paused->capacity = grown;
		}
		if (!pauseAtSafePoint(thread)) {
			log_message("freeze: thread %lu never paused at a safe point", entry.th32ThreadID);
			CloseHandle(thread);
			ok = false;
			break;
		}
		paused->threads[paused->count++] = thread;
	}
	CloseHandle(snapshot);
	return ok;
}

// Suspends `thread` once it sits right after a syscall instruction, i.e. blocked in the kernel
// rather than part-way through game or heap code.
static bool pauseAtSafePoint(HANDLE thread) {
	for (int attempt = 0; attempt < PAUSE_ATTEMPTS; ++attempt) {
		if (SuspendThread(thread) == (DWORD)-1) {
			return false;
		}
		CONTEXT context = {.ContextFlags = CONTEXT_CONTROL};
		if (GetThreadContext(thread, &context)) {
			DWORD64 address = context.Rip - SYSCALL_LENGTH;
			const uint8_t *code = NULL;
			memcpy(&code, &address, sizeof code);
			uint8_t before[SYSCALL_LENGTH] = {0};
			if (memory_read(code, before, sizeof before) && before[0] == 0x0F && before[1] == 0x05) {
				return true;
			}
		}
		ResumeThread(thread);
		Sleep(1);
	}
	return false;
}

static void resumeAll(Paused *paused) {
	for (size_t i = 0; i < paused->count; ++i) {
		ResumeThread(paused->threads[i]);
		CloseHandle(paused->threads[i]);
	}
	free(paused->threads);
	*paused = (Paused){0};
}

static DWORD WINAPI runWork(LPVOID parameter) {
	const Work *request = parameter;
	request->work(request->context);
	return 0;
}
