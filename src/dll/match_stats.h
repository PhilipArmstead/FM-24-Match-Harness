// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Player and team statistics of played matches, summed over a run's seeds (run --detailed). They
// come from the engine's per-player GAME_MATCH_PLAYER_STATS objects (MATCH_PLAYER+5856), the data
// the game's own match stats screens show; offsets and names are in docs/PLAN.md Phase 18.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>


enum {
	MATCH_STATS_MAX_PLAYERS = 40,
	MATCH_STATS_PLAYER_VALUES = 46,
	MATCH_STATS_TEAM_VALUES = 38,
	MATCH_STATS_NAME_LENGTH = 128,
};

typedef struct PlayerTotals {
	uint32_t uniqueId;
	unsigned appearances; // matches played in (from the start, or brought on)
	unsigned starts;      // appearances from the start
	double minutes;
	int64_t ratingSum; // rating x 100, over appearances (an integer, so the sum is exact)
	double values[MATCH_STATS_PLAYER_VALUES]; // sums are exact: see exactValue in match_stats.c
} PlayerTotals;

typedef struct TeamTotals {
	char name[MATCH_STATS_NAME_LENGTH];
	size_t playerCount;
	size_t playerCapacity;
	PlayerTotals *players;
	double values[MATCH_STATS_TEAM_VALUES];
} TeamTotals;

typedef struct MatchStatsTotals {
	unsigned matches;
	TeamTotals teams[2]; // home, away
} MatchStatsTotals;

// Adds the statistics of a played match, matching players by unique ID across rebuilt line-ups.
bool matchStats_add(MatchStatsTotals *totals, const uint8_t *match);

void matchStats_free(MatchStatsTotals *totals);

// Writes the averages per match next to the run's CSV: "<run output without .csv>-players.csv" and
// "-teams.csv". Returns false if a file cannot be written.
bool matchStats_write(const MatchStatsTotals *totals, const wchar_t *runOutput);
