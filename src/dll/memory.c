// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "memory.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum { SCAN_CHUNK = 1 << 20 };

static const uintptr_t userSpaceEnd = 0x7FFFFFFF0000ULL;

static void scanRegion(
	uint8_t *base,
	size_t size,
	uint8_t *chunk,
	uintptr_t value,
	void (*visit)(void *context, uint8_t *address),
	void *context
);

bool memory_read(const void *address, void *out, size_t size) {
	SIZE_T got = 0;
	return ReadProcessMemory(GetCurrentProcess(), address, out, size, &got) && got == size;
}

bool memory_readPointer(const void *base, ptrdiff_t offset, uint8_t **out) {
	*out = NULL;
	return base && memory_read((const uint8_t *)base + offset, out, sizeof *out) && *out;
}

bool memory_readU16(const void *base, ptrdiff_t offset, uint16_t *out) {
	return base && memory_read((const uint8_t *)base + offset, out, sizeof *out);
}

bool memory_readU32(const void *base, ptrdiff_t offset, uint32_t *out) {
	return base && memory_read((const uint8_t *)base + offset, out, sizeof *out);
}

uint16_t memory_loadU16(const uint8_t *bytes) {
	uint16_t value = 0;
	memcpy(&value, bytes, sizeof value);
	return value;
}

uint32_t memory_loadU32(const uint8_t *bytes) {
	uint32_t value = 0;
	memcpy(&value, bytes, sizeof value);
	return value;
}

uint64_t memory_loadU64(const uint8_t *bytes) {
	uint64_t value = 0;
	memcpy(&value, bytes, sizeof value);
	return value;
}

void memory_scanForQword(
	uintptr_t value,
	void (*visit)(void *context, uint8_t *address),
	void *context
) {
	uint8_t *chunk = malloc(SCAN_CHUNK);
	if (!chunk) {
		return;
	}
	MEMORY_BASIC_INFORMATION info;
	for (uint8_t *address = NULL; VirtualQuery(address, &info, sizeof info);
		address = (uint8_t *)info.BaseAddress + info.RegionSize) {
		if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && info.Protect == PAGE_READWRITE) {
			scanRegion(info.BaseAddress, info.RegionSize, chunk, value, visit, context);
		}
		if ((uintptr_t)info.BaseAddress + info.RegionSize >= userSpaceEnd) {
			break;
		}
	}
	free(chunk);
}

// Scans one committed region in chunks for qwords equal to `value`.
static void scanRegion(
	uint8_t *base,
	size_t size,
	uint8_t *chunk,
	uintptr_t value,
	void (*visit)(void *context, uint8_t *address),
	void *context
) {
	for (size_t offset = 0; offset < size; offset += SCAN_CHUNK) {
		size_t length = size - offset < SCAN_CHUNK ? size - offset : SCAN_CHUNK;
		if (!memory_read(base + offset, chunk, length)) {
			continue;
		}
		for (size_t i = 0; i + sizeof(uintptr_t) <= length; i += sizeof(uintptr_t)) {
			uintptr_t found = 0;
			memcpy(&found, chunk + i, sizeof found);
			if (found == value) {
				visit(context, base + offset + i);
			}
		}
	}
}
