// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Path helpers for the runner (UTF-16 paths, as Windows hands them to wmain).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>


// The directory fm24ctl.exe lives in (also where the DLL, job and result files are).
bool paths_executableDirectory(wchar_t *out, size_t size);

// `directory` + "\" + `name`.
bool paths_join(wchar_t *out, size_t size, const wchar_t *directory, const wchar_t *name);

// An absolute version of a path given on the command line (the DLL runs with fm.exe's working
// directory).
bool paths_absolute(const wchar_t *path, wchar_t *out, size_t size);

// UTF-8 for job files and messages.
bool paths_toUtf8(const wchar_t *text, char *out, size_t size);
