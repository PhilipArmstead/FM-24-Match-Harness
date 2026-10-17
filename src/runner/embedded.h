// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The harness DLL built into fm24ctl.exe (resource IDR_HARNESS_DLL), and the copy of it that is
// injected: %LOCALAPPDATA%\fm24-harness\HARNESS_DLL_NAME, with harness.log next to it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>


// The extracted DLL's path; creates its directory. False if it cannot be resolved or created.
bool embedded_dllPath(wchar_t *out, size_t size);

// Writes the built-in DLL to `path` unless an identical copy is already there.
// Prints why and returns false on failure.
bool embedded_extract(const wchar_t *path);

// `path` holds exactly `bytes` afterwards: left alone if it already does, else replaced through a
// temporary file. A copy that fm.exe has loaded cannot be replaced. Prints why and returns false on
// failure.
bool embedded_write(const wchar_t *path, const void *bytes, size_t size);
