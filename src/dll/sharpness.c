// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sharpness.h"

#include <stddef.h>


uint16_t sharpness_afterMatch(uint16_t start, unsigned minutes, unsigned x) {
	unsigned step = x < 6 ? 5 : x > 15 ? 15 : x;
	unsigned s = start;
	unsigned rest = minutes;
	if (s < 7500) {
		unsigned rate = 2 * step;
		unsigned need = (7500 - s) / rate;
		s += rate * (need >= minutes ? minutes : need + 1);
		rest = need >= minutes ? 0 : minutes - (need + 1);
	}
	if (s < 9000) {
		unsigned rate = 3 * step / 2;
		unsigned need = (9000 - s) / rate;
		s += rate * (rest > need ? need + 1 : rest);
		// As in the game: the minutes left above 9000 are counted from the whole match.
		rest = rest > need ? minutes - (need + 1) : 0;
	}
	s += rest * (step / 2);
	return (uint16_t)(s < 10000 ? s : 10000);
}

static uint64_t nextRandom(uint64_t *state) {
	uint64_t z = (*state += 0x9E3779B97F4A7C15U);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9U;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBU;
	return z ^ (z >> 31);
}

unsigned sharpness_drawFactor(uint64_t *random) {
	// 90 values of MATCH_PLAYER+0x2D4, clamped, over two days: the game sets it per player and day.
	static const struct {
		uint8_t factor;
		uint8_t weight;
	} seen[] = {{5, 16}, {6, 5}, {7, 7}, {8, 2}, {10, 1}, {12, 5}, {14, 7}, {15, 47}};
	unsigned roll = (unsigned)(nextRandom(random) % 90);
	for (size_t i = 0; i < sizeof seen / sizeof seen[0]; ++i) {
		if (roll < seen[i].weight) {
			return seen[i].factor;
		}
		roll -= seen[i].weight;
	}
	return 15;
}
