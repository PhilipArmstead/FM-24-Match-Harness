// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Finding the human manager's next fixture.
#pragma once

#include <stdbool.h>
#include <stdint.h>


typedef struct NextFixture {
	uint8_t *fixture;      // a sicomps::FIXTURE
	char description[160]; // "Home vs Away"
	char when[48];         // "2023 day 186 19:30"
} NextFixture;

// The earliest fixture of the human manager's team at or after the current game date and time (on
// match day the clock sits exactly at kick-off until the match is played). On failure, `error` says
// why.
bool fixture_findNext(NextFixture *next, const char **error);

// Whether a copy of `fixture` (the same home and away teams) is among the human manager's attended
// matches. Only then does the match builder give the human control of their side, with their
// line-up and tactic; the game adds the copy on match day.
bool fixture_isAttended(const uint8_t *fixture);
