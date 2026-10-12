// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// A sampling profiler for the calling thread. A helper thread suspends it about once a millisecond,
// walks its stack and appends one line per sample to a text file:
//   s <pc0> <pc1> ... [| <pcN> ...]   addresses in hex, innermost frame first
// Frames before '|' are exactly unwound; after it (once code without unwind data is reached) they
// are a stack scan: plausible return addresses, which can include stale ones.
// Lines starting with '#' are comments (phase markers, timings).
#pragma once

#include <stdbool.h>
#include <wchar.h>


typedef struct Profiler Profiler;

// Starts sampling the calling thread into `path`; NULL (logged) if that is not possible.
Profiler *profiler_start(const wchar_t *path);

// Writes `# <text>` into the sample file, between samples.
void profiler_mark(Profiler *profiler, const char *text);

// Stops sampling, closes the file and frees the profiler.
void profiler_stop(Profiler *profiler);
