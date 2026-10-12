// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The thread-local PCG32 that the game's pre-match setup (attendance and other build-time choices)
// draws from when it is not handed an RNG. Each thread has its own stream, advanced by whatever the
// thread did before, so builds are only reproducible if it is reset first.
#pragma once

#include <stdint.h>


// Seeds the calling thread's copy from `seed` (pcg32_srandom style, fixed increment).
void rng_seedThread(uint32_t seed);
