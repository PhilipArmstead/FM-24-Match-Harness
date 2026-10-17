// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Loading the harness DLL into the running fm.exe.
#pragma once

#include <stdbool.h>


// Injects HARNESS_DLL_NAME with CreateRemoteThread + LoadLibraryW: the copy next to fm24ctl.exe if
// there is one (a development build), else the one built into fm24ctl.exe (see embedded.h).
// Prints what happened; returns false on failure.
bool inject_harness(void);

// The process ID of the running fm.exe, or 0 if it isn't running.
unsigned long inject_findGame(void);
