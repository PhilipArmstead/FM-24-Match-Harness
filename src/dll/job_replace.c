// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Rebuilds either side with another team, keeping edits outside the replaced team's block.
// The config's home/away overrides let `run` reproduce both replacements for every seed.

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

static bool
findTeams(const Job *job, uint8_t *current[2], uint8_t **team, char *error, size_t size);
static bool reserialise(ByteBuffer *bytes, Serialised *out);
static void writeConfig(const Job *job, SeedBuilder *builder, uint8_t *team, uint32_t seed);

void jobs_replaceTeam(const Job *job) {
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
	uint8_t *current[2] = {0};
	uint8_t *team = NULL;
	SeedBuilder builder;
	bool ok =
		findTeams(job, current, &team, error, sizeof error) &&
		seedBuilder_open(
			&builder, &input, seed, (const uint8_t *[]){current[0], current[1]}, error, sizeof error
		);
	buffer_free(&input);
	if (!ok) {
		log_message("replace team: %s", error);
		job_writeResult("error: %s", error);
		return;
	}
	writeConfig(job, &builder, team, seed);
	seedBuilder_close(&builder);
}

// The input's team overrides (NULL: the fixture's own) and the new team.
static bool
findTeams(const Job *job, uint8_t *current[2], uint8_t **team, char *error, size_t size) {
	for (unsigned side = 0; side < 2; ++side) {
		if (job->teams[side][0] && !database_findTeam(job->teams[side], &current[side], error, size)) {
			return false;
		}
	}
	return database_findTeam(job->team, team, error, size);
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
	unsigned side = job->command == JOB_REPLACE_AWAY;
	const char *sideName = side ? "away" : "home";
	uint8_t *original[2] = {0};
	if (!memory_readPointer(builder->next.fixture, FIXTURE_HOME, &original[0]) ||
			!memory_readPointer(builder->next.fixture, FIXTURE_AWAY, &original[1]) || !original[0] ||
			!original[1]) {
		job_writeResult("error: cannot read the fixture's teams");
		return;
	}
	size_t dropped = 0;
	if (!seedBuilder_dropTeamEdits(builder, side, &dropped)) {
		job_writeResult("error: cannot identify the %s team's edits; see harness.log", sideName);
		return;
	}
	size_t kept = seedBuilder_editCount(builder);
	builder->teams[side] = team == original[side] ? NULL : team;
	const uint8_t *selected[2] = {builder->teams[0] ? builder->teams[0] : original[0],
		builder->teams[1] ? builder->teams[1] : original[1]};
	uint32_t teamIds[2] = {builder->teams[0] ? database_teamUniqueId(builder->teams[0]) : 0,
		builder->teams[1] ? database_teamUniqueId(builder->teams[1]) : 0};
	char homeName[TEAM_NAME_LENGTH];
	char awayName[TEAM_NAME_LENGTH];
	database_clubShortName(selected[0], homeName, sizeof homeName);
	database_clubShortName(selected[1], awayName, sizeof awayName);
	char source[256];
	(void)snprintf(
		source, sizeof source, "%s vs %s, %s, seed %u", homeName, awayName, builder->next.when, seed
	);
	log_message(
		"replace_%s: %s; %zu edits kept, %zu %s edits dropped",
		sideName,
		source,
		kept,
		dropped,
		sideName
	);
	ByteBuffer merged = {0};
	Serialised serialised = {0};
	if (!seedBuilder_build(builder, seed, &merged)) {
		job_writeResult("error: the input's edits cannot be placed in the new build; see harness.log");
	} else if (!reserialise(&merged, &serialised) ||
						 !output_write(job->output, &serialised, source, teamIds, job->full)) {
		job_writeResult("error: serialise or write failed; see harness.log");
	} else {
		job_writeResult(
			"ok: %s (team %u); %zu edits kept, %zu edits to the old %s team dropped",
			source,
			database_teamUniqueId(team),
			kept,
			dropped,
			sideName
		);
	}
	trace_free(&serialised);
	buffer_free(&merged);
}
