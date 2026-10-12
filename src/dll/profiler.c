// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "profiler.h"

#include "game.h"
#include "log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	MAX_FRAMES = 64,
	SCAN_SLOTS = 8192,      // how far up the stack (in 8-byte slots) a scan looks
	INTERVAL_100NS = 10000, // 1 ms
	CALL_REL32_LENGTH = 5,  // E8 rel32
	MAX_CODE_RANGES = 16,
};

typedef struct Range {
	uintptr_t start;
	uintptr_t end;
} Range;

struct Profiler {
	HANDLE target;
	HANDLE thread;
	FILE *file;
	CRITICAL_SECTION lock; // the file is written by both threads
	volatile LONG stop;
	uintptr_t stackLimit; // the target's stack, for the return address scan
	uintptr_t stackBase;
	Range code[MAX_CODE_RANGES]; // fm.exe's executable sections
	int codeCount;
};

static bool inCode(const Profiler *profiler, uintptr_t address);
static bool followsCall(const Profiler *profiler, uintptr_t address);
static int scanStack(const Profiler *profiler, uintptr_t slot, uint64_t *frames, int count);
static int walk(const Profiler *profiler, CONTEXT *context, uint64_t *frames, int *exact);
static void writeSample(Profiler *profiler, const uint64_t *frames, int count, int exact);
static void sample(Profiler *profiler);
static DWORD WINAPI sampler(LPVOID parameter);
static void setCodeRanges(Profiler *profiler);

Profiler *profiler_start(const wchar_t *path) {
	Profiler *profiler = calloc(1, sizeof *profiler);
	if (!profiler) {
		return NULL;
	}
	const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
	profiler->stackLimit = (uintptr_t)tib->StackLimit;
	profiler->stackBase = (uintptr_t)tib->StackBase;
	setCodeRanges(profiler);
	InitializeCriticalSection(&profiler->lock);
	profiler->file = _wfopen(path, L"wb");
	bool duplicated = DuplicateHandle(
		GetCurrentProcess(),
		GetCurrentThread(),
		GetCurrentProcess(),
		&profiler->target,
		THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
		FALSE,
		0
	);
	if (profiler->file && duplicated) {
		fprintf(profiler->file, "# fm.exe %llx\n", (unsigned long long)game_base());
		profiler->thread = CreateThread(NULL, 0, sampler, profiler, 0, NULL);
	}
	if (!profiler->thread) {
		log_message("profiler: cannot start");
		profiler_stop(profiler);
		return NULL;
	}
	return profiler;
}

void profiler_mark(Profiler *profiler, const char *text) {
	if (!profiler) {
		return;
	}
	EnterCriticalSection(&profiler->lock);
	fprintf(profiler->file, "# %s\n", text);
	LeaveCriticalSection(&profiler->lock);
}

void profiler_stop(Profiler *profiler) {
	if (!profiler) {
		return;
	}
	if (profiler->thread) {
		InterlockedExchange(&profiler->stop, 1);
		WaitForSingleObject(profiler->thread, INFINITE);
		CloseHandle(profiler->thread);
	}
	if (profiler->target) {
		CloseHandle(profiler->target);
	}
	if (profiler->file) {
		(void)fclose(profiler->file);
	}
	DeleteCriticalSection(&profiler->lock);
	free(profiler);
}

static bool inCode(const Profiler *profiler, uintptr_t address) {
	for (int i = 0; i < profiler->codeCount; ++i) {
		if (address >= profiler->code[i].start && address < profiler->code[i].end) {
			return true;
		}
	}
	return false;
}

// True if the bytes before `address` end with a call instruction (rel32 or one of the common
// indirect forms), so `address` is plausibly a return address.
static bool followsCall(const Profiler *profiler, uintptr_t address) {
	if (!inCode(profiler, address - 8)) {
		return false;
	}
	uint8_t code[8];
	memcpy(code, (const void *)(address - 8), sizeof code); // NOLINT(performance-no-int-to-ptr)
	bool rel32 = code[8 - CALL_REL32_LENGTH] == 0xE8;
	bool indirect2 = code[6] == 0xFF && (code[7] & 0x38) == 0x10; // call r/m (no displacement)
	bool indirect3 = code[5] == 0xFF && (code[6] & 0x38) == 0x10; // call [r+disp8]
	bool indirect6 = code[2] == 0xFF && (code[3] & 0x38) == 0x10; // call [rip+disp32]
	bool indirect7 = code[1] == 0xFF && (code[2] & 0x38) == 0x10; // call [r+disp32] with SIB
	return rel32 || indirect2 || indirect3 || indirect6 || indirect7;
}

// Appends every plausible return address into fm.exe found from `slot` up the stack, stale ones
// included. Reads only the target's stack and fm.exe's code.
static int scanStack(const Profiler *profiler, uintptr_t slot, uint64_t *frames, int count) {
	for (int i = 0; i < SCAN_SLOTS && count < MAX_FRAMES && slot >= profiler->stackLimit &&
									slot + sizeof(uintptr_t) <= profiler->stackBase;
		++i, slot += sizeof(uintptr_t)) {
		uintptr_t value = *(const uintptr_t *)slot; // NOLINT(performance-no-int-to-ptr)
		if (inCode(profiler, value) && followsCall(profiler, value)) {
			frames[count++] = value;
		}
	}
	return count;
}

// Walks the (suspended) target's stack: exact unwinding while there is unwind data, then (from code
// without it, like the relocated .arch functions) a stack scan, since unwinding on from a guessed
// context can follow garbage registers. Returns the frame count; `exact` the unwound ones.
// Must not allocate: the target may hold the heap lock.
static int walk(const Profiler *profiler, CONTEXT *context, uint64_t *frames, int *exact) {
	int count = 0;
	while (count < MAX_FRAMES && context->Rip != 0 && context->Rsp >= profiler->stackLimit &&
				 context->Rsp < profiler->stackBase) {
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context->Rip, &imageBase, NULL);
		if (!function) {
			*exact = count + 1;
			frames[count++] = context->Rip;
			return scanStack(profiler, (uintptr_t)context->Rsp, frames, count);
		}
		frames[count++] = context->Rip;
		PVOID handlerData = NULL;
		DWORD64 establisher = 0;
		RtlVirtualUnwind(
			UNW_FLAG_NHANDLER,
			imageBase,
			context->Rip,
			function,
			context,
			&handlerData,
			&establisher,
			NULL
		);
	}
	*exact = count;
	return count;
}

static void writeSample(Profiler *profiler, const uint64_t *frames, int count, int exact) {
	EnterCriticalSection(&profiler->lock);
	fputc('s', profiler->file);
	for (int i = 0; i < count; ++i) {
		fprintf(profiler->file, i == exact ? " | %llx" : " %llx", (unsigned long long)frames[i]);
	}
	fputc('\n', profiler->file);
	LeaveCriticalSection(&profiler->lock);
}

static void sample(Profiler *profiler) {
	uint64_t frames[MAX_FRAMES];
	CONTEXT context = {.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER};
	if (SuspendThread(profiler->target) == (DWORD)-1) {
		return;
	}
	int exact = 0;
	int count =
		GetThreadContext(profiler->target, &context) ? walk(profiler, &context, frames, &exact) : 0;
	ResumeThread(profiler->target);
	if (count > 0) {
		writeSample(profiler, frames, count, exact);
	}
}

static DWORD WINAPI sampler(LPVOID parameter) {
	Profiler *profiler = parameter;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	// Sleep() would only wake every 15.6 ms without raising the system timer resolution.
	HANDLE timer =
		CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!timer) {
		log_message("profiler: no high-resolution timer");
		return 1;
	}
	LARGE_INTEGER due = {.QuadPart = -INTERVAL_100NS};
	while (!profiler->stop) {
		if (!SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE) ||
				WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) {
			break;
		}
		sample(profiler);
	}
	CloseHandle(timer);
	return 0;
}

static void setCodeRanges(Profiler *profiler) {
	const uint8_t *base = (const uint8_t *)game_address(0);
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
	const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
	const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections && profiler->codeCount < MAX_CODE_RANGES;
		++i, ++section) {
		if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
			uintptr_t start = game_base() + section->VirtualAddress;
			profiler->code[profiler->codeCount++] = (Range){start, start + section->Misc.VirtualSize};
		}
	}
}
