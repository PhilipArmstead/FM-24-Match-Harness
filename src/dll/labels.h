// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Human-readable labels for the fields of a traced serialisation: which part of the match each
// block is ("referee", "home.player[3]", ...), who each player is, and which field holds a player's
// ratings.
#pragma once

#include "database.h"
#include "game.h"
#include "trace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


enum { LABEL_LENGTH = 64, PLAYER_NAME_LENGTH = 128 };

typedef struct FieldLabel {
	char name[LABEL_LENGTH];         // "" except on the first field of a block and key fields
	char player[PLAYER_NAME_LENGTH]; // the player's name, on the first field of a player block
	bool ratings;                    // the 69-byte positions + attributes field
} FieldLabel;

// One label per record of `serialised` (free() the result); player names come from `people`.
FieldLabel *labels_compute(const Serialised *serialised, const PeopleIndex *people);

typedef struct PlayerRatings {
	uint32_t uniqueId; // 0 if no database person was found for the block
	uint8_t ratings[PLAYER_RATING_COUNT];
} PlayerRatings;

// The players of a serialised match in order, with their ratings (free() the result).
PlayerRatings *
labels_players(const Serialised *serialised, const PeopleIndex *people, size_t *count);

// The names of a ratings field's 69 values: 15 positions, then 54 attributes (constants.h order).
const char *labels_ratingName(size_t index);
