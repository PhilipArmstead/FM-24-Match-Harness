// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "memory.h"

#include <string.h>
#include <windows.h>


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
