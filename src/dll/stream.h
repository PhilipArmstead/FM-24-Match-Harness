// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The game's DATA_BUFFER: a shared byte buffer with an input and an output stream over it.
// GAME_MATCH__serialize / __deserialize take a pointer to one of the stream slots.
#pragma once

#include <stddef.h>
#include <stdint.h>

// {refcount, begin, end, cap, pos}, allocated and owned by the game.
typedef struct SharedBuffer {
	uint32_t refcount;
	uint32_t padding;
	uint8_t *begin;
	uint8_t *end;
	uint8_t *capacity;
	uint32_t position;
} SharedBuffer;

// 32 bytes as the game lays it out (+ slack): [0] vtable, [1] shared buffer, [2] input stream,
// [3] output stream. The streams' error code is a u32 at +16.
typedef struct DataBuffer {
	void *vtable;
	SharedBuffer *shared;
	void *input;
	void *output;
	uint8_t reserved[64];
} DataBuffer;

// A DataBuffer constructed by the game; it lives for the rest of the process (its destructor would
// free memory through the game's allocator, which is wrong for buffers we point it at).
DataBuffer *stream_create(void);

// Points the input stream at `bytes`; call stream_detachInput afterwards so the game is never left
// holding a pointer to memory we own.
void stream_attachInput(DataBuffer *buffer, uint8_t *bytes, size_t size);
void stream_detachInput(DataBuffer *buffer);

// Empties the output stream (its storage stays with the game's allocator).
void stream_resetOutput(DataBuffer *buffer);

// The bytes written to the output stream so far.
const uint8_t *stream_outputData(const DataBuffer *buffer);
size_t stream_outputSize(const DataBuffer *buffer);
