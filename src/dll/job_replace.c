// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "replace_away": an input rebuilt by the game against another away team, written as a JSON
// config. The input's edits are re-applied (seed_build.h), except those to the away team it
// replaces. The config's "away_team" names the new team, so that `run` rebuilds it the same way.

#include "database.h"
#include "files.h"
#include "game.h"
#include "job.h"
#include "log.h"
#include "match.h"
#include "memory.h"
#include "output.h"
#include "seed_build.h"
#include "trace.h"

#include <stdio.h>
#include <string.h>


enum { TEAM_NAME_LENGTH = 64 };

static bool findTeams(const Job *job, uint8_t **current, uint8_t **team, char *error, size_t size);
static bool reserialise(ByteBuffer *bytes, Serialised *out);
static void writeConfig(const Job *job, SeedBuilder *builder, uint8_t *team, uint32_t seed);

void jobs_replaceAway(const Job *job) {
	ByteBuffer input = {0};
	size_t seedOffset = 0;
	if (!output_wantsJson(job->output)) {
		job_writeResult("error: the output must be a .json config");
		return;
	}
	if (!files_read(job->input, &input) || !match_seedOffset(input.data, input.length, &seedOffset)) {
		job_writeResult("error: cannot read the input, or it is not a serialised match");
		buffer_free(&input);
		return;
	}
	uint32_t seed = memory_loadU32(input.data + seedOffset);
	char error[512] = "";
	uint8_t *current = NULL;
	uint8_t *team = NULL;
	SeedBuilder builder;
	bool ok = findTeams(job, &current, &team, error, sizeof error) &&
						seedBuilder_open(&builder, &input, seed, current, error, sizeof error);
	buffer_free(&input);
	if (!ok) {
		log_message("replace_away: %s", error);
		job_writeResult("error: %s", error);
		return;
	}
	writeConfig(job, &builder, team, seed);
	seedBuilder_close(&builder);
}

// The team the input was built against (NULL: the fixture's own) and the new one.
static bool findTeams(const Job *job, uint8_t **current, uint8_t **team, char *error, size_t size) {
	return (!job->awayTeam[0] || database_findTeam(job->awayTeam, current, error, size)) &&
				 database_findTeam(job->team, team, error, size);
}

// Loads serialised match bytes and serialises them again with their write records.
static bool reserialise(ByteBuffer *bytes, Serialised *out) {
	uint8_t *match = match_deserialize(bytes);
	bool ok = match && trace_serialize(match, out, true);
	if (match) {
		match_delete(match);
	}
	return ok;
}

// Builds the fixture against the new team with the input's seed and edits, and writes it.
static void writeConfig(const Job *job, SeedBuilder *builder, uint8_t *team, uint32_t seed) {
	uint8_t *home = NULL;
	uint8_t *fixtureAway = NULL;
	(void)memory_readPointer(builder->next.fixture, FIXTURE_HOME, &home);
	(void)memory_readPointer(builder->next.fixture, FIXTURE_AWAY, &fixtureAway);
	if (team == home) {
		job_writeResult("error: that is the home team");
		return;
	}
	if (fixtureAway == database_findHumanTeam()) {
		job_writeResult("error: your team is the away side; only an opponent can be replaced");
		return;
	}
	size_t dropped = 0;
	if (!seedBuilder_dropAwayEdits(builder, &dropped)) {
		job_writeResult("error: cannot identify the away team's edits; see harness.log");
		return;
	}
	size_t kept = seedBuilder_editCount(builder);
	builder->awayTeam = team == fixtureAway ? NULL : team;
	char homeName[TEAM_NAME_LENGTH];
	char awayName[TEAM_NAME_LENGTH];
	database_clubShortName(home, homeName, sizeof homeName);
	database_clubShortName(team, awayName, sizeof awayName);
	char source[256];
	(void)snprintf(
		source, sizeof source, "%s vs %s, %s, seed %u", homeName, awayName, builder->next.when, seed
	);
	log_message("replace_away: %s; %zu edits kept, %zu away edits dropped", source, kept, dropped);
	ByteBuffer merged = {0};
	Serialised serialised = {0};
	if (!seedBuilder_build(builder, seed, &merged)) {
		job_writeResult("error: the input's edits cannot be placed in the new build; see harness.log");
	} else if (!reserialise(&merged, &serialised) ||
						 !output_write(
							 job->output,
							 &serialised,
							 source,
							 builder->awayTeam ? database_teamUniqueId(team) : 0,
							 job->full
						 )) {
		job_writeResult("error: serialise or write failed; see harness.log");
	} else {
		job_writeResult(
			"ok: %s (team %u); %zu edits kept, %zu edits to the old away team dropped",
			source,
			database_teamUniqueId(team),
			kept,
			dropped
		);
	}
	trace_free(&serialised);
	buffer_free(&merged);
}
