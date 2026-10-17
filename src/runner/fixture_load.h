// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fixture_data.h"

#include <wchar.h>


// JSON is read locally. Binary input is decoded by the running game's harness, never played.
bool fixtureLoad_open(const wchar_t *path, FixtureData *out, char *error, size_t errorSize);

// Copies or converts the complete fixture, replacing the destination only after writing succeeds.
// JSON-to-binary conversion is local; binary-to-JSON conversion needs the running game's harness.
bool fixtureLoad_save(const wchar_t *input, const wchar_t *output, char *error, size_t errorSize);

// Applies venue edits and derived fields before saving either format; NULL keeps the input intact.
bool fixtureLoad_saveVenue(
	const wchar_t *input,
	const wchar_t *output,
	const FixtureNumber values[VENUE_FIELD_COUNT],
	char *error,
	size_t errorSize
);
