// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The weekly training schedule (knowledge-base/FM24 Training — Attribute Growth.md, section 17):
// reading the one a player's team trains on, and replacing it for develop's simulation.
#pragma once

#include "game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// The session IDs, day by day, of the schedule the player's team trains on this week. False if
// there is none. Call it with the game's other threads paused: the lookup may create the week.
bool schedule_readCurrent(uint8_t *player, uint16_t out[SCHEDULE_SESSIONS]);

// Hooks the weekly training's lookup: every player trains on weeks[k % count] for the k-th week
// it trains from now on, k counting from schedule_restart. With count 0 the game's own schedules
// are used and each week's date and sessions are logged once. Call it with the game's other threads
// paused. False (logged) if the call site is not the one the harness knows.
bool schedule_override(const uint16_t (*weeks)[SCHEDULE_SESSIONS], size_t count);

// Puts the game's own lookup back. Call it with the game's other threads paused.
bool schedule_release(void);

// The next week trained is weeks[0] again.
void schedule_restart(void);
