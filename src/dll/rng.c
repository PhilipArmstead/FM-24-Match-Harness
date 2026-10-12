// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rng.h"

#include "game.h"

#include <string.h>


static const uint64_t pcgMultiplier = 0x5851F42D4C957F2DULL;
static const uint64_t pcgIncrement = 0xDA3E39CB94B95BDBULL; // PCG32 default stream

void rng_seedThread(uint32_t seed) {
	uint8_t *tls = game_initThread();
	uint64_t state = pcgIncrement; // state = 0, then one step
	state += seed;
	state = state * pcgMultiplier + pcgIncrement;
	memcpy(tls + TLS_PCG + 8, &state, sizeof state);
	memcpy(tls + TLS_PCG + 16, &pcgIncrement, sizeof pcgIncrement);
}
