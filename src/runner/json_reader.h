// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// A minimal streaming JSON reader: enough to walk a match config, but it accepts any valid JSON.
#pragma once

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>


typedef struct JsonReader {
	const char *begin;
	const char *position;
	const char *end;
	char error[192]; // the first error, with its byte offset
} JsonReader;

void jsonReader_init(JsonReader *reader, const char *text, size_t length);

// Records an error (the first one wins) and returns false.
bool jsonReader_fail(JsonReader *reader, const char *message);

// Skips whitespace, then consumes `c` if it is next.
bool jsonReader_consume(JsonReader *reader, char c);

// Reads a string into `out` (decoding escapes to UTF-8), or validates it if `out` is NULL.
bool jsonReader_readString(JsonReader *reader, ByteBuffer *out);

// Reads a number; `isUnsigned` says whether it was a plain non-negative integer (then `value` holds
// it, unless `value` is NULL).
bool jsonReader_readNumber(JsonReader *reader, unsigned long long *value, bool *isUnsigned);

int jsonReader_hexDigit(uint8_t c);

// Skips any value.
bool jsonReader_skipValue(JsonReader *reader);
