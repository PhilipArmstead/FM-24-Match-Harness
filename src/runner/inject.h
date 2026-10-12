// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Loading the harness DLL into the running fm.exe.
#pragma once

#include <stdbool.h>


// Injects HARNESS_DLL_NAME (next to fm24ctl.exe) with CreateRemoteThread + LoadLibraryW.
// Prints what happened; returns false on failure.
bool inject_harness(void);
