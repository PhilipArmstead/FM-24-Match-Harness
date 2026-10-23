// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Runs work while fm.exe's other threads are paused. Game code outside the match engine shares
// unsynchronised global state with the game's own threads, even when the game is idle (e.g. the
// scope stack at qword_146432408 that news items use), so calling it alongside them corrupts it.
#pragma once

#include <stdbool.h>


typedef enum FreezeResult {
	FREEZE_OK,         // the work ran with every other thread paused
	FREEZE_NOT_PAUSED, // a thread never reached a safe point: the work did not run
	FREEZE_TIMED_OUT,  // the work outlasted the watchdog: the threads were resumed, it then finished
} FreezeResult;

// Pauses every other thread of the process where it is blocked in a system call (so it holds no
// lock half-way), runs `work(context)` on a new thread and resumes them all afterwards.
FreezeResult freeze_run(void (*work)(void *context), void *context);
