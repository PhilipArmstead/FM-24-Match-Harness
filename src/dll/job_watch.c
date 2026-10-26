// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "watch_write": logs every instruction that writes a field of one player's record, to find
// the game code behind a value (first use: the match-points counter at player +0x1AE, knowledge
// base §20). It sets hardware breakpoint 0 (DR0/DR7) on every thread that exists when it is
// armed, and a vectored exception handler logs each hit to harness.log: the instruction after the
// write, the value now, a few registers and the fm.exe addresses on the stack (likely callers).
// Without a person, the job stops watching. The game is otherwise untouched.

#include "database.h"
#include "game.h"
#include "job.h"
#include "labels.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

// clang-format off: tlhelp32.h needs windows.h first, which alphabetical include sorting breaks
#include <windows.h>
#include <tlhelp32.h>
// clang-format on


enum {
	MAX_HITS = 200,        // logged per arming; later writes are only counted
	STACK_SCAN = 96,       // qwords above rsp searched for fm.exe return addresses
	MAX_CALLERS = 8,       // of those, logged per hit
	DR7_SLOT0 = 0xF0001,   // L0 (bit 0), RW0 (bits 16-17) and LEN0 (bits 18-19)
	DR7_WRITE = 0x10000,   // RW0 = 01: data writes
	DR7_LENGTH2 = 0x40000, // LEN0 = 01: 2 bytes (the address must be 2-aligned)
	DR6_SLOT0 = 0x1,
};

static PVOID handler;          // registered once and kept
static volatile LONG watching; // 1 while hits are logged
static volatile LONG hits;     // writes seen since arming
static uint8_t *watchAddress;  // the watched bytes
static size_t watchSize;       // 1 or 2
static uintptr_t imageStart;   // fm.exe, for telling return addresses from other stack values
static uintptr_t imageEnd;

static LONG CALLBACK onException(EXCEPTION_POINTERS *info);
static unsigned setBreakpoints(bool on);

void jobs_watchWrite(const Job *job) {
	if (job->personId == 0) {
		InterlockedExchange(&watching, 0);
		unsigned threads = setBreakpoints(false);
		log_message("watch_write: stopped after %ld writes (%u threads cleared)", hits, threads);
		job_writeResult("ok: stopped watching after %ld writes", hits);
		return;
	}
	PeopleIndex people;
	if (!database_loadPeople(&people)) {
		job_writeResult("error: cannot read the people list; is a save loaded?");
		return;
	}
	const uint8_t *person = database_findPerson(&people, job->personId);
	uint8_t *player = person ? database_playerRecord(person) : NULL;
	char name[PLAYER_NAME_LENGTH] = "";
	if (person && !database_personName(person, name, sizeof name)) {
		(void)snprintf(name, sizeof name, "(unnamed, UID %u)", job->personId);
	}
	database_freePeople(&people);
	if (!player) {
		job_writeResult("error: no player with unique ID %u", job->personId);
		return;
	}
	if (!handler) {
		handler = AddVectoredExceptionHandler(1, onException);
		if (!handler) {
			job_writeResult("error: cannot install the exception handler");
			return;
		}
		const uint8_t *base = game_address(0);
		const IMAGE_NT_HEADERS *headers =
			(const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
		imageStart = (uintptr_t)base;
		imageEnd = imageStart + headers->OptionalHeader.SizeOfImage;
	}
	InterlockedExchange(&watching, 0);
	watchAddress = player + job->offset;
	watchSize = ((uintptr_t)watchAddress & 1) ? 1 : 2;
	InterlockedExchange(&hits, 0);
	InterlockedExchange(&watching, 1);
	unsigned threads = setBreakpoints(true);
	log_message(
		"watch_write: %s (%u), player +0x%x = %p (%zu bytes), on %u threads",
		name,
		job->personId,
		job->offset,
		(void *)watchAddress,
		watchSize,
		threads
	);
	job_writeResult(
		"ok: watching writes to %s's +0x%x (%zu bytes) on %u threads; hits go to harness.log",
		name,
		job->offset,
		watchSize,
		threads
	);
}

// Sets (or clears) breakpoint 0 on every thread of the process except this one, which cannot
// change its own registers; it is the harness's job thread. Threads created later are not
// covered. Returns the number of threads changed.
static unsigned setBreakpoints(bool on) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		log_message("watch_write: cannot list threads (Windows error %lu)", GetLastError());
		return 0;
	}
	DWORD process = GetCurrentProcessId();
	DWORD self = GetCurrentThreadId();
	unsigned changed = 0;
	THREADENTRY32 entry = {.dwSize = sizeof entry};
	for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
		if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) {
			continue;
		}
		HANDLE thread = OpenThread(
			THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID
		);
		if (!thread) {
			continue;
		}
		if (SuspendThread(thread) != (DWORD)-1) {
			CONTEXT context = {.ContextFlags = CONTEXT_DEBUG_REGISTERS};
			if (GetThreadContext(thread, &context)) {
				context.Dr7 &= ~(DWORD64)DR7_SLOT0;
				context.Dr0 = 0;
				if (on) {
					context.Dr0 = (DWORD64)(uintptr_t)watchAddress;
					context.Dr7 |= 1 | DR7_WRITE | (watchSize == 2 ? DR7_LENGTH2 : 0);
				}
				changed += SetThreadContext(thread, &context) != 0;
			}
			ResumeThread(thread);
		}
		CloseHandle(thread);
	}
	CloseHandle(snapshot);
	return changed;
}

// Data breakpoints trap after the write: the context's Rip is the next instruction.
static LONG CALLBACK onException(EXCEPTION_POINTERS *info) {
	CONTEXT *context = info->ContextRecord;
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
			!(context->Dr6 & DR6_SLOT0)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	context->Dr6 = 0;
	LONG hit = InterlockedIncrement(&hits);
	if (!watching || hit > MAX_HITS) {
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	unsigned value = watchSize == 2 ? *(const uint16_t *)watchAddress : *watchAddress;
	char callers[MAX_CALLERS * 20] = "";
	size_t used = 0;
	unsigned found = 0;
	const uint64_t *stack =
		(const uint64_t *)(uintptr_t)context->Rsp; // NOLINT(performance-no-int-to-ptr)
	for (unsigned i = 0; i < STACK_SCAN && found < MAX_CALLERS; ++i) {
		if (stack[i] >= imageStart && stack[i] < imageEnd) {
			int n = snprintf(
				callers + used, sizeof callers - used, " +0x%llx", stack[i] - (uint64_t)imageStart
			);
			used += n > 0 ? (size_t)n : 0;
			++found;
		}
	}
	// A write from outside fm.exe (e.g. a CRT memcpy) is logged as an absolute address.
	bool inImage = context->Rip >= imageStart && context->Rip < imageEnd;
	log_message(
		"watch_write: hit %ld, thread %lu: after %s0x%llx, value now %u; rax=%llx rcx=%llx "
		"rdx=%llx r8=%llx r9=%llx rbx=%llx rsi=%llx rdi=%llx; stack callers (fm.exe+):%s",
		hit,
		GetCurrentThreadId(),
		inImage ? "fm.exe+" : "",
		context->Rip - (inImage ? (uint64_t)imageStart : 0),
		value,
		context->Rax,
		context->Rcx,
		context->Rdx,
		context->R8,
		context->R9,
		context->Rbx,
		context->Rsi,
		context->Rdi,
		callers
	);
	return EXCEPTION_CONTINUE_EXECUTION;
}
