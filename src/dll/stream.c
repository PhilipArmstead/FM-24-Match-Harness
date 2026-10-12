// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stream.h"

#include "game.h"

#include <windows.h>


enum { STREAM_ERROR = 16 }; // u32 error code inside each stream object

static void clearError(void *stream);

DataBuffer *stream_create(void) {
	DataBuffer *buffer =
		VirtualAlloc(NULL, sizeof(DataBuffer), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (buffer) {
		((DataBufferCtorFn)game_function(RVA_DATA_BUFFER_CTOR))(buffer);
	}
	return buffer;
}

void stream_attachInput(DataBuffer *buffer, uint8_t *bytes, size_t size) {
	SharedBuffer *shared = buffer->shared;
	shared->begin = bytes;
	shared->end = bytes + size;
	shared->capacity = shared->end;
	shared->position = 0;
	clearError(buffer->input);
}

void stream_detachInput(DataBuffer *buffer) {
	SharedBuffer *shared = buffer->shared;
	shared->begin = NULL;
	shared->end = NULL;
	shared->capacity = NULL;
	shared->position = 0;
}

void stream_resetOutput(DataBuffer *buffer) {
	buffer->shared->position = 0;
	clearError(buffer->output);
}

const uint8_t *stream_outputData(const DataBuffer *buffer) {
	return buffer->shared->begin;
}

size_t stream_outputSize(const DataBuffer *buffer) {
	return buffer->shared->position;
}

static void clearError(void *stream) {
	*(uint32_t *)((uint8_t *)stream + STREAM_ERROR) = 0;
}
