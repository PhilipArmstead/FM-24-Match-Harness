// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rng.h"

#include "game.h"

#include <string.h>
#include <windows.h>


enum { TEB_TLS_POINTER = 0x58 }; // TEB.ThreadLocalStoragePointer

static const uint64_t pcgMultiplier = 0x5851F42D4C957F2DULL;
static const uint64_t pcgIncrement = 0xDA3E39CB94B95BDBULL; // PCG32 default stream

static uint8_t *threadLocalBlock(void);

void rng_seedThread(uint32_t seed) {
	uint8_t *tls = threadLocalBlock();
	if (tls[TLS_INIT_FLAG] == 0) {
		// What the game does on first use: run this thread's thread_local constructors.
		((VoidFn)game_function(RVA_TLS_ON_DEMAND_INIT))();
	}
	uint64_t state = pcgIncrement; // state = 0, then one step
	state += seed;
	state = state * pcgMultiplier + pcgIncrement;
	memcpy(tls + TLS_PCG + 8, &state, sizeof state);
	memcpy(tls + TLS_PCG + 16, &pcgIncrement, sizeof pcgIncrement);
}

static uint8_t *threadLocalBlock(void) {
	uint8_t **slots = NULL;
	memcpy(&slots, (const uint8_t *)NtCurrentTeb() + TEB_TLS_POINTER, sizeof slots);
	uint32_t index = *(const uint32_t *)game_address(RVA_TLS_INDEX);
	return slots[index];
}
