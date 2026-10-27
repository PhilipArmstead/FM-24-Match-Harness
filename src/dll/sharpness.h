// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Match sharpness as the match engine moves it (knowledge-base/FM24 Training — Attribute
// Growth.md, section 21), for develop's planned matches.
#pragma once

#include <stdint.h>

// Sharpness after a match: the engine's end-of-match step (sub_141B48C20), from `start` over
// `minutes` with the player's factor `x` for the day (MATCH_PLAYER+0x2D4).
// shortcut: its 3/4 variant, for matches whose byte +1674 is 3 (a type not identified), is left
// out.
uint16_t sharpness_afterMatch(uint16_t start, unsigned minutes, unsigned x);

// A day's factor x after the engine's 5-15 clamp, drawn from the values seen in the game; `random`
// is the caller's generator state (splitmix64), separate from the game's.
unsigned sharpness_drawFactor(uint64_t *random);
