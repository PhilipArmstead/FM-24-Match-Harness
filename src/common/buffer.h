// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// A growable byte buffer, also used as a string builder (it keeps a NUL after the last byte).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct ByteBuffer {
	uint8_t *data;
	size_t length;
	size_t capacity;
} ByteBuffer;

// Appends `size` bytes; returns false if memory runs out (the buffer is unchanged).
bool buffer_append(ByteBuffer *buffer, const void *bytes, size_t size);

// Appends a NUL-terminated string (without its terminator).
bool buffer_appendString(ByteBuffer *buffer, const char *text);

// Appends printf-style formatted text.
bool buffer_appendFormat(ByteBuffer *buffer, const char *format, ...)
	__attribute__((format(gnu_printf, 2, 3)));

// The contents as a C string ("" when empty).
const char *buffer_string(const ByteBuffer *buffer);

void buffer_clear(ByteBuffer *buffer);
void buffer_free(ByteBuffer *buffer);
