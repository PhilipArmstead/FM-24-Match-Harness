// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "rebuild": recomputes everything the game derives from edited player ratings.
//
// The derived values (abilities, match ratings, evaluations) are computed by the game's match
// builder from the database player and the match context, so the faithful way to recompute them is
// to let the builder run:
//
//   O = build(next fixture, config seed)                 unmodified database
//   B = the same build with each player's database ratings temporarily replaced by the config's
//   result[i] = B[i] if B[i] != O[i] (the game recomputed it), else config[i]
//
// so the config's other edits survive. The config is compared as written (its own write records,
// passed by the runner), not after a game load and save, which would drop edits to data the game
// re-derives (e.g. the venue name). O, the config and B must have the same structure (same fixture,
// same squads), otherwise the job refuses.

#include "database.h"
#include "files.h"
#include "fixture.h"
#include "job.h"
#include "labels.h"
#include "log.h"
#include "match.h"
#include "memory.h"
#include "output.h"
#include "trace.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct Rebuild {
	const wchar_t *directory;
	uint32_t seed;
	NextFixture fixture;
	PeopleIndex people;
	Serialised config;   // A: the config's bytes, split into its own write records
	bool *siteKnown;     // per config record: false for the byte part of strings (site not recorded)
	Serialised original; // O
	Serialised edited;   // B
} Rebuild;

typedef struct MergeCounts {
	size_t recomputed;
	size_t keptEdits;
	size_t overridden;
} MergeCounts;

static bool fail(const Rebuild *rebuild, const char *message) {
	job_writeResult(rebuild->directory, "error: %s", message);
	return false;
}

static bool appendKnown(bool **known, size_t count, bool value) {
	bool *grown = realloc(*known, (count + 1) * sizeof **known);
	if (!grown) {
		return false;
	}
	grown[count] = value;
	*known = grown;
	return true;
}

// One records line: "<site>/<caller> <length>", where the site may be "*" or "?" (not recorded).
// Returns false for a malformed line; `known` says whether the site was recorded.
static bool parseRecordLine(const char *line, TraceRecord *record, bool *known) {
	char *end = NULL;
	errno = 0;
	unsigned long site = strtoul(line, &end, 16);
	*known = errno == 0 && end != line && *end == '/';
	unsigned long caller = *known ? strtoul(end + 1, &end, 16) : 0;
	const char *space = strchr(line, ' ');
	if (!space) {
		return false;
	}
	unsigned long long length = strtoull(space + 1, &end, 10);
	if (errno != 0 || end == space + 1 || length > UINT32_MAX) {
		return false;
	}
	record->site = *known ? (uint32_t)site : 0;
	record->caller = *known ? (uint32_t)caller : 0;
	record->length = (uint32_t)length;
	return true;
}

// Splits the config's bytes into the records the runner wrote (one line each).
static bool loadConfigRecords(Rebuild *rebuild, const wchar_t *path) {
	FILE *file = _wfopen(path, L"rb");
	if (!file) {
		return fail(rebuild, "cannot read the config's records");
	}
	char line[128];
	uint32_t offset = 0;
	bool ok = true;
	while (ok && fgets(line, sizeof line, file)) {
		TraceRecord record = {.offset = offset};
		bool known = false;
		if (!parseRecordLine(line, &record, &known)) {
			continue;
		}
		ok = appendKnown(&rebuild->siteKnown, rebuild->config.recordCount, known) &&
				 trace_appendRecord(&rebuild->config, record);
		offset += record.length;
	}
	(void)fclose(file);
	if (!ok || offset != rebuild->config.bytes.length) {
		return fail(rebuild, "the config's records do not cover its bytes");
	}
	return true;
}

static bool buildTraced(Rebuild *rebuild, Serialised *out) {
	uint8_t *match = match_build(rebuild->fixture.fixture, rebuild->seed);
	if (!match) {
		return fail(rebuild, "match allocation failed");
	}
	bool ok = trace_serialize(match, out, true);
	match_freeBuilt(match);
	return ok || fail(rebuild, "serialise/trace failed; see harness.log");
}

// The config must describe the same fixture as O: same players in the same order, same structure.
static bool
checkSameFixture(Rebuild *rebuild, const PlayerRatings *configPlayers, size_t configCount) {
	size_t originalCount = 0;
	PlayerRatings *originalPlayers =
		labels_players(&rebuild->original, &rebuild->people, &originalCount);
	bool same = originalPlayers && configCount == originalCount &&
							rebuild->config.recordCount == rebuild->original.recordCount;
	for (size_t i = 0; same && i < configCount; ++i) {
		same = configPlayers[i].uniqueId == originalPlayers[i].uniqueId;
	}
	free(originalPlayers);
	return same || fail(
									 rebuild,
									 "the config does not match the next fixture's squads/structure (built for "
									 "another fixture or game state?)"
								 );
}

// Builds B with the config's ratings written into the database, restoring it afterwards. Unless
// `asBuilt`, B must keep O's structure so the two can be merged.
static bool
buildWithConfigRatings(Rebuild *rebuild, const PlayerRatings *players, size_t count, bool asBuilt) {
	uint8_t (*saved)[PLAYER_RATING_COUNT] = calloc(count ? count : 1, sizeof *saved);
	uint8_t **targets = calloc(count ? count : 1, sizeof *targets);
	bool ok = saved && targets;
	for (size_t i = 0; ok && i < count; ++i) {
		uint8_t *person = database_findPerson(&rebuild->people, players[i].uniqueId);
		if (person) {
			targets[i] = database_playerRatings(person);
			memcpy(saved[i], targets[i], PLAYER_RATING_COUNT);
			memcpy(targets[i], players[i].ratings, PLAYER_RATING_COUNT);
		}
	}
	ok = ok && buildTraced(rebuild, &rebuild->edited);
	for (size_t i = 0; targets && i < count; ++i) {
		if (targets[i]) {
			memcpy(targets[i], saved[i], PLAYER_RATING_COUNT);
		}
	}
	free(saved);
	free(targets);
	if (ok && !asBuilt && rebuild->edited.recordCount != rebuild->original.recordCount) {
		return fail(
			rebuild, "the edited ratings changed the match structure (e.g. squad selection); not merged"
		);
	}
	return ok;
}

static bool
sameValue(const Serialised *a, const TraceRecord *x, const Serialised *b, const TraceRecord *y) {
	return x->length == y->length &&
				 memcmp(a->bytes.data + x->offset, b->bytes.data + y->offset, x->length) == 0;
}

static bool mergeRecord(Rebuild *rebuild, Serialised *result, size_t i, MergeCounts *counts) {
	const TraceRecord *original = &rebuild->original.records[i];
	const TraceRecord *config = &rebuild->config.records[i];
	const TraceRecord *edited = &rebuild->edited.records[i];
	if (original->site != edited->site ||
			(rebuild->siteKnown[i] &&
				(original->site != config->site || original->caller != config->caller))) {
		job_writeResult(
			rebuild->directory, "error: field %zu differs in structure between config and rebuild", i
		);
		return false;
	}
	bool gameChanged = !sameValue(&rebuild->edited, edited, &rebuild->original, original);
	bool userChanged = !sameValue(&rebuild->config, config, &rebuild->original, original);
	const Serialised *source = gameChanged ? &rebuild->edited : &rebuild->config;
	const TraceRecord *chosen = gameChanged ? edited : config;
	counts->recomputed += gameChanged;
	counts->keptEdits += userChanged && !gameChanged;
	counts->overridden +=
		userChanged && gameChanged && !sameValue(&rebuild->config, config, &rebuild->edited, edited);
	TraceRecord record = {
		.offset = (uint32_t)result->bytes.length,
		.length = chosen->length,
		.site = edited->site,
		.caller = edited->caller,
		.path = edited->path,
	};
	return trace_appendRecord(result, record) &&
				 buffer_append(&result->bytes, source->bytes.data + chosen->offset, chosen->length);
}

static bool merge(Rebuild *rebuild, Serialised *result, MergeCounts *counts) {
	for (size_t i = 0; i < rebuild->original.recordCount; ++i) {
		if (!mergeRecord(rebuild, result, i, counts)) {
			return false;
		}
	}
	// the merged records use B's call chains: hand the path table over
	result->paths = rebuild->edited.paths;
	result->pathCount = rebuild->edited.pathCount;
	result->pathCapacity = rebuild->edited.pathCapacity;
	rebuild->edited.paths = NULL;
	rebuild->edited.pathCount = 0;
	rebuild->edited.pathCapacity = 0;
	return true;
}

static bool loadConfig(Rebuild *rebuild, const Job *job) {
	size_t seedOffset = 0;
	if (!files_read(job->input, &rebuild->config.bytes)) {
		return fail(rebuild, "cannot read input");
	}
	if (!match_seedOffset(rebuild->config.bytes.data, rebuild->config.bytes.length, &seedOffset)) {
		return fail(rebuild, "input is not a serialised GAME_MATCH");
	}
	rebuild->seed = memory_loadU32(rebuild->config.bytes.data + seedOffset);
	return loadConfigRecords(rebuild, job->records);
}

static bool prepare(Rebuild *rebuild, const Job *job) {
	const char *error = NULL;
	if (!loadConfig(rebuild, job)) {
		return false;
	}
	if (!fixture_findNext(&rebuild->fixture, &error)) {
		return fail(rebuild, error);
	}
	if (!database_loadPeople(&rebuild->people)) {
		return fail(rebuild, "cannot read the people list");
	}
	return buildTraced(rebuild, &rebuild->original);
}

static void finish(
	Rebuild *rebuild,
	const Job *job,
	Serialised *result,
	const MergeCounts *counts,
	size_t players
) {
	char source[320];
	(void)snprintf(
		source,
		sizeof source,
		"%s, %s, seed %u, rebuilt with edited ratings",
		rebuild->fixture.description,
		rebuild->fixture.when,
		rebuild->seed
	);
	if (!output_write(job->output, result, source)) {
		(void)fail(rebuild, "cannot write output");
		return;
	}
	log_message(
		"rebuild: %zu players, %zu fields recomputed, %zu other edits kept, %zu edits replaced by "
		"recomputed values",
		players,
		counts->recomputed,
		counts->keptEdits,
		counts->overridden
	);
	job_writeResult(
		rebuild->directory,
		"ok: %zu fields recomputed by the game, %zu other edits kept, %zu edited fields replaced by "
		"recomputed values",
		counts->recomputed,
		counts->keptEdits,
		counts->overridden
	);
}

// --as-built: B itself, the game's own build with the config's ratings.
static void finishAsBuilt(Rebuild *rebuild, const Job *job, size_t players) {
	char source[320];
	(void)snprintf(
		source,
		sizeof source,
		"%s, %s, seed %u, built with the config's ratings",
		rebuild->fixture.description,
		rebuild->fixture.when,
		rebuild->seed
	);
	if (!output_write(job->output, &rebuild->edited, source)) {
		(void)fail(rebuild, "cannot write output");
		return;
	}
	bool sameStructure = rebuild->edited.recordCount == rebuild->original.recordCount;
	log_message("rebuild: as built with %zu players' ratings", players);
	job_writeResult(
		rebuild->directory,
		"ok: the game's build with the config's ratings for %zu players (other edits ignored; "
		"structure %s)",
		players,
		sameStructure ? "unchanged" : "CHANGED"
	);
}

void jobs_rebuild(const Job *job, const wchar_t *directory) {
	Rebuild rebuild = {.directory = directory};
	Serialised result = {0};
	MergeCounts counts = {0};
	size_t playerCount = 0;
	PlayerRatings *players = NULL;
	bool ok = prepare(&rebuild, job);
	if (ok) {
		players = labels_players(&rebuild.config, &rebuild.people, &playerCount);
		ok = players && checkSameFixture(&rebuild, players, playerCount) &&
				 buildWithConfigRatings(&rebuild, players, playerCount, job->asBuilt) &&
				 (job->asBuilt || merge(&rebuild, &result, &counts));
	}
	if (ok && job->asBuilt) {
		finishAsBuilt(&rebuild, job, playerCount);
	} else if (ok) {
		finish(&rebuild, job, &result, &counts, playerCount);
	}
	free(players);
	free(rebuild.siteKnown);
	trace_free(&result);
	trace_free(&rebuild.config);
	trace_free(&rebuild.original);
	trace_free(&rebuild.edited);
	database_freePeople(&rebuild.people);
}
