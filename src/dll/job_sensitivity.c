// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "sensitivity": which serialised values the game's match builder derives from each player
// rating.
//
// The human manager's next fixture is built once as a baseline. Then, for each selected player and
// each of their 69 ratings, the rating is changed in the database, the fixture is rebuilt with the
// same seed, every serialised field that differs from the baseline is written to the CSV, and the
// rating is restored. The change is one in-game step (positions +-1 on 1-20, attributes +-5 on
// 1-100) or, with a value list, each listed in-game value in turn. All builds use the same seed, so
// a difference can only come from the edited rating.

#include "database.h"
#include "fixture.h"
#include "job.h"
#include "labels.h"
#include "log.h"
#include "match.h"
#include "memory.h"
#include "sites.h"
#include "trace.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum {
	POSITION_COUNT = 15,
	POSITION_MAX = 20,
	ATTRIBUTE_MAX = 100,
	ATTRIBUTE_STEP = 5, // one in-game point
	DEFAULT_SEED = 1,
	NUMBER_LENGTH = 8, // fields up to this long are printed as unsigned numbers, longer ones as hex
};

typedef struct Probe {
	const Job *job;
	const wchar_t *directory;
	uint32_t seed;
	NextFixture fixture;
	PeopleIndex people;
	Serialised baseline;
	FieldLabel *labels; // per baseline record
	FILE *csv;
	size_t builds;
	size_t rows;
	size_t structureChanges;
} Probe;

static bool fail(const Probe *probe, const char *message) {
	log_message("sensitivity: %s", message);
	job_writeResult(probe->directory, "error: %s", message);
	return false;
}

static bool buildTraced(Probe *probe, Serialised *out) {
	uint8_t *match = match_build(probe->fixture.fixture, probe->seed);
	if (!match) {
		return false;
	}
	bool ok = trace_serialize(match, out, true);
	match_freeBuilt(match);
	++probe->builds;
	return ok;
}

// ASCII case-insensitive substring test (names are UTF-8; non-ASCII bytes must match exactly).
static bool containsIgnoringCase(const char *text, const char *part) {
	size_t length = strlen(part);
	for (const char *start = text; *start; ++start) {
		size_t i = 0;
		while (i < length && start[i] &&
					 tolower((unsigned char)start[i]) == tolower((unsigned char)part[i])) {
			++i;
		}
		if (i == length) {
			return true;
		}
	}
	return length == 0;
}

static bool isSelected(const Probe *probe, uint32_t uniqueId, const char *name) {
	const char *filter = probe->job->player;
	if (filter[0] == 0) {
		return true;
	}
	char *end = NULL;
	unsigned long id = strtoul(filter, &end, 10);
	return (*end == 0 && id == uniqueId) || containsIgnoringCase(name, filter);
}

// A field value: unsigned little-endian decimal up to 8 bytes, hex beyond.
static void valueText(const uint8_t *bytes, uint32_t length, char *out, size_t size) {
	if (length <= NUMBER_LENGTH) {
		uint64_t value = 0;
		memcpy(&value, bytes, length);
		(void)snprintf(out, size, "%llu", (unsigned long long)value);
		return;
	}
	size_t used = 0;
	for (uint32_t i = 0; i < length && used + 3 < size; ++i) {
		used += (size_t)snprintf(out + used, size - used, "%02x", bytes[i]);
	}
}

// The index of the first record of the block record `i` is in (fields are numbered from there).
static size_t blockStart(const Probe *probe, size_t i) {
	while (i > 0 && strcmp(probe->labels[i - 1].block, probe->labels[i].block) == 0) {
		--i;
	}
	return i;
}

typedef struct Edit {
	uint32_t uniqueId;
	const char *name;
	const char *playerBlock;
	size_t rating;
	uint8_t from;
	uint8_t to;
} Edit;

static void writeDifferences(Probe *probe, const Edit *edit, const Serialised *edited) {
	for (size_t i = 0; i < probe->baseline.recordCount; ++i) {
		const TraceRecord *a = &probe->baseline.records[i];
		const TraceRecord *b = &edited->records[i];
		const uint8_t *before = probe->baseline.bytes.data + a->offset;
		const uint8_t *after = edited->bytes.data + b->offset;
		if (probe->labels[i].ratings ||
				(a->length == b->length && memcmp(before, after, a->length) == 0)) {
			continue;
		}
		char oldText[512];
		char newText[512];
		valueText(before, a->length, oldText, sizeof oldText);
		valueText(after, b->length, newText, sizeof newText);
		fprintf(
			probe->csv,
			"%u,\"%s\",%s,%s,%u,%u,%s,%zu,%x/%x,%u,%s,%s\n",
			edit->uniqueId,
			edit->name,
			edit->playerBlock,
			labels_ratingName(edit->rating),
			edit->from,
			edit->to,
			probe->labels[i].block,
			i - blockStart(probe, i),
			b->site,
			b->caller,
			b->length,
			oldText,
			newText
		);
		++probe->rows;
	}
}

// Sets one rating to edit->to, rebuilds, writes the differences and restores the rating.
static bool probeValue(Probe *probe, const Edit *edit, uint8_t *ratings) {
	ratings[edit->rating] = edit->to;
	Serialised edited = {0};
	bool ok = buildTraced(probe, &edited);
	ratings[edit->rating] = edit->from;
	if (ok && edited.recordCount != probe->baseline.recordCount) {
		// e.g. the AI picks a different squad: fields no longer line up one to one
		fprintf(
			probe->csv,
			"%u,\"%s\",%s,%s,%u,%u,structure changed,,,,,\n",
			edit->uniqueId,
			edit->name,
			edit->playerBlock,
			labels_ratingName(edit->rating),
			edit->from,
			edit->to
		);
		++probe->structureChanges;
	} else if (ok) {
		writeDifferences(probe, edit, &edited);
	}
	trace_free(&edited);
	return ok || fail(probe, "a build failed; see harness.log");
}

// Probes one rating: one in-game step up (down at the top), or every value of the job's list.
static bool probeRating(Probe *probe, Edit *edit, uint8_t *ratings) {
	bool position = edit->rating < POSITION_COUNT;
	int scale = position ? 1 : ATTRIBUTE_STEP; // database units per in-game point
	int maximum = position ? POSITION_MAX : ATTRIBUTE_MAX;
	edit->from = ratings[edit->rating];
	if (probe->job->valueCount == 0) {
		edit->to = (uint8_t)(edit->from + scale <= maximum ? edit->from + scale : edit->from - scale);
		return probeValue(probe, edit, ratings);
	}
	bool ok = true;
	for (size_t i = 0; ok && i < probe->job->valueCount; ++i) {
		edit->to = (uint8_t)(probe->job->values[i] * scale);
		ok = edit->to == edit->from || probeValue(probe, edit, ratings);
	}
	return ok;
}

static bool probePlayer(Probe *probe, uint32_t uniqueId, const char *playerBlock) {
	uint8_t *person = database_findPerson(&probe->people, uniqueId);
	char name[PLAYER_NAME_LENGTH];
	if (!person || !database_personName(person, name, sizeof name) ||
			!isSelected(probe, uniqueId, name)) {
		return true;
	}
	log_message("sensitivity: %s (%s)", name, playerBlock);
	uint8_t *ratings = database_playerRatings(person);
	Edit edit = {.uniqueId = uniqueId, .name = name, .playerBlock = playerBlock};
	bool ok = true;
	for (edit.rating = 0; ok && edit.rating < PLAYER_RATING_COUNT; ++edit.rating) {
		ok = probeRating(probe, &edit, ratings);
	}
	(void)fflush(probe->csv);
	return ok;
}

static bool probePlayers(Probe *probe) {
	for (size_t i = 0; i < probe->baseline.recordCount; ++i) {
		const TraceRecord *record = &probe->baseline.records[i];
		if (record->site != SITE_PLAYER_UID || record->length != sizeof(uint32_t) ||
				strstr(probe->labels[i].name, ".person_uid") == NULL) {
			continue;
		}
		uint32_t uniqueId = memory_loadU32(probe->baseline.bytes.data + record->offset);
		if (!probePlayer(probe, uniqueId, probe->labels[i].block)) {
			return false;
		}
	}
	return true;
}

static bool prepare(Probe *probe) {
	const char *error = NULL;
	if (!fixture_findNext(&probe->fixture, &error)) {
		return fail(probe, error);
	}
	if (!database_loadPeople(&probe->people)) {
		return fail(probe, "cannot read the people list");
	}
	// The first build after loading a save sets a byte in the fixture (see docs/PLAN.md), so the
	// baseline is the second build, like every probe build after it.
	Serialised warmUp = {0};
	bool ok = buildTraced(probe, &warmUp) && buildTraced(probe, &probe->baseline);
	trace_free(&warmUp);
	if (!ok) {
		return fail(probe, "the baseline build failed; see harness.log");
	}
	probe->labels = labels_compute(&probe->baseline, &probe->people);
	probe->csv = _wfopen(probe->job->output, L"wb");
	if (!probe->labels || !probe->csv) {
		return fail(probe, "out of memory, or cannot open the output file");
	}
	fprintf(
		probe->csv,
		"player_uid,player,player_block,rating,rating_from,rating_to,field_block,field,site,length,"
		"old,new\n"
	);
	return true;
}

void jobs_sensitivity(const Job *job, const wchar_t *directory) {
	Probe probe = {
		.job = job, .directory = directory, .seed = job->hasSeed ? job->seed : DEFAULT_SEED};
	bool ok = prepare(&probe) && probePlayers(&probe);
	if (probe.csv) {
		(void)fclose(probe.csv);
	}
	if (ok) {
		log_message("sensitivity: %zu builds, %zu rows", probe.builds, probe.rows);
		job_writeResult(
			directory,
			"ok: %s, seed %u: %zu builds, %zu changed fields, %zu edits changed the structure",
			probe.fixture.description,
			probe.seed,
			probe.builds,
			probe.rows,
			probe.structureChanges
		);
	}
	free(probe.labels);
	trace_free(&probe.baseline);
	database_freePeople(&probe.people);
}
