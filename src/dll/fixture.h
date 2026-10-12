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
