// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static bool reserve(ByteBuffer *buffer, size_t extra);

bool buffer_append(ByteBuffer *buffer, const void *bytes, size_t size) {
	if (!reserve(buffer, size)) {
		return false;
	}
	if (size) {
		memcpy(buffer->data + buffer->length, bytes, size);
	}
	buffer->length += size;
	buffer->data[buffer->length] = 0;
	return true;
}

bool buffer_appendFormat(ByteBuffer *buffer, const char *format, ...) {
	va_list args;
	va_start(args, format);
	va_list copy;
	va_copy(copy, args);
	int size = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	bool ok = size >= 0 && reserve(buffer, (size_t)size);
	if (ok) {
		ok = vsnprintf((char *)buffer->data + buffer->length, (size_t)size + 1, format, args) == size;
		if (ok) {
			buffer->length += (size_t)size;
		}
	}
	va_end(args);
	return ok;
}

const char *buffer_string(const ByteBuffer *buffer) {
	return buffer->data ? (const char *)buffer->data : "";
}

void buffer_clear(ByteBuffer *buffer) {
	buffer->length = 0;
	if (buffer->data) {
		buffer->data[0] = 0;
	}
}

void buffer_free(ByteBuffer *buffer) {
	free(buffer->data);
	*buffer = (ByteBuffer){0};
}

static bool reserve(ByteBuffer *buffer, size_t extra) {
	size_t needed = buffer->length + extra + 1; // room for the trailing NUL
	if (needed <= buffer->capacity) {
		return true;
	}
	size_t capacity = buffer->capacity ? buffer->capacity : 64;
	while (capacity < needed) {
		capacity *= 2;
	}
	uint8_t *data = realloc(buffer->data, capacity);
	if (!data) {
		return false;
	}
	buffer->data = data;
	buffer->capacity = capacity;
	return true;
}
