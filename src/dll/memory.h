// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Reads of game memory that may have been freed in the meantime. They go through
// ReadProcessMemory on our own process, so a bad address fails the read instead of crashing fm.exe.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


bool memory_read(const void *address, void *out, size_t size);

bool memory_readPointer(const void *base, ptrdiff_t offset, uint8_t **out);
bool memory_readU16(const void *base, ptrdiff_t offset, uint16_t *out);
bool memory_readU32(const void *base, ptrdiff_t offset, uint32_t *out);

// Unaligned little-endian loads from a byte array.
uint16_t memory_loadU16(const uint8_t *bytes);
uint32_t memory_loadU32(const uint8_t *bytes);
uint64_t memory_loadU64(const uint8_t *bytes);
