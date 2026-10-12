// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Whole-file reads and writes with wide (UTF-16) Windows paths.
#pragma once

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>


// Replaces `out`'s contents with the file's; returns false if it cannot be read.
bool files_read(const wchar_t *path, ByteBuffer *out);

bool files_write(const wchar_t *path, const void *bytes, size_t size);

// True if `path` ends with `suffix`, ignoring case (e.g. L".json").
bool files_hasExtension(const wchar_t *path, const wchar_t *suffix);
