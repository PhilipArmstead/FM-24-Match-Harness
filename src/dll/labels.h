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


enum { LABEL_LENGTH = 112, PLAYER_NAME_LENGTH = 128 };

// What the bits of a bit-set field mean (docs/PLAN.md Phase 17, 21); see labels_flagName.
typedef enum FlagKind {
	FLAGS_NONE,
	FLAGS_TEAM_INSTRUCTIONS,   // team_instructions[flagWord]
	FLAGS_PLAYER_INSTRUCTIONS, // player_instructions[...].bits[flagWord]
	FLAGS_POSITION,            // a formation slot's position mask
	FLAGS_ROLE,                // a formation slot's role mask: role and duty
	FLAGS_TRAITS,              // a player's traits (preferred moves)
	FLAGS_STAND,               // a venue stand's flags: roof, seats, tunnel
} FlagKind;

typedef struct FieldLabel {
	char name[LABEL_LENGTH];         // "" except on the first field of a block and key fields
	char player[PLAYER_NAME_LENGTH]; // the player's name, on the first field of a player block
	bool ratings;                    // the 69-byte positions + attributes field
	bool staffAttributes;            // the 52-byte staff attributes field, written one u8 each
	const char *member;              // the struct member written (site_members.h), or NULL
	FlagKind flags;                  // how to name the field's set bits, if it is a bit set
	unsigned flagWord;               // which word of a multi-word bit set
} FieldLabel;

// One label per record of `serialised` (free() the result); player names come from `people`.
FieldLabel *labels_compute(const Serialised *serialised, const PeopleIndex *people);

// The name of bit `bit` of a field whose label has flags, or NULL if unknown.
const char *labels_flagName(const FieldLabel *label, unsigned bit);

// The names of a ratings field's 69 values: 15 positions, then 54 attributes (database order).
const char *labels_ratingName(size_t index);

// The names of the 11 training categories, in the game's order (strength ... set_pieces).
const char *labels_categoryName(size_t index);

// The names of a staff attributes field's 52 bytes: 0-26 on the 1-20 scale, 27-51 on 1-100.
const char *labels_staffAttributeName(size_t index);
