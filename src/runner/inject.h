// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Loading the harness DLL into the running fm.exe.
#pragma once

#include <stdbool.h>


// Injects HARNESS_DLL_NAME, or HARNESS_DLL_FALLBACK_NAME if that is missing (both next to
// fm24ctl.exe), with CreateRemoteThread + LoadLibraryW.
// Prints what happened; returns false on failure.
bool inject_harness(void);

// The process ID of the running fm.exe, or 0 if it isn't running.
unsigned long inject_findGame(void);
