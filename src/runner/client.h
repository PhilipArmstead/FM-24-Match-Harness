// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Submitting jobs to the injected DLL (see protocol.h).
#pragma once

#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>


enum {
	CLIENT_SHORT_TIMEOUT_MS = 10 * 60 * 1000, // builds, exports, rebuilds
	CLIENT_LONG_TIMEOUT_MS = 6 * 3600 * 1000, // batches of matches
	CLIENT_RESULT_LENGTH = HARNESS_RESULT_LENGTH,
};

// Sends the job to the DLL and waits for its result, at most `timeoutMs` in all (including any
// wait for another runner's job to finish). Injects the DLL first if this protocol's harness is not
// loaded in fm.exe yet. If `result` is non-NULL it receives the DLL's one-line result (or a note to
// check harness.log). Prints and returns false on failure.
bool client_submit(const char *job, unsigned timeoutMs, char *result, size_t resultSize);

// True if a result line reports success.
bool client_succeeded(const char *result);
