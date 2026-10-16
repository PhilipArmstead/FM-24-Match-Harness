// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "seed_build.h"

#include "game.h"
#include "log.h"
#include "match.h"
#include "memory.h"
#include "sites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Every path of a serialisation split into its RVAs (outermost first), so that records' call
// chains can be compared level by level.
typedef struct Chains {
	uint32_t *rvas;
	size_t *starts;  // per path: where its RVAs begin in `rvas`
	size_t *lengths; // per path: how many there are
	size_t deepest;
} Chains;

typedef struct Side {
	const Serialised *serialised;
	Chains chains;
} Side;

// A pointer in the game's data replaced for a build against another away team.
typedef struct Swap {
	uint8_t *slot;
	uint8_t *original;
} Swap;

enum { MAX_SWAPS = 8 };

// Pairs records of the input with records of a build (see align()).
typedef struct Alignment {
	Side input;
	Side built;
	size_t *source; // per built record: the input record it pairs with, or SIZE_MAX
} Alignment;

static void addSwap(Swap *swaps, size_t *count, uint8_t *slot, const uint8_t *expected);
static size_t swapAwayTeam(const SeedBuilder *builder, Swap *swaps);
static void restoreSwaps(const Swap *swaps, size_t count);
static bool
buildSerialised(const SeedBuilder *builder, uint32_t seed, Serialised *out, bool traced);
static bool roundTrip(const ByteBuffer *bytes, Serialised *out);
static bool sameLayout(const Serialised *a, const Serialised *b);
static bool sameRecord(const Serialised *a, const Serialised *b, size_t i);
static bool appendRecord(ByteBuffer *out, const Serialised *from, size_t i);
static bool parseChains(const Serialised *serialised, Chains *chains);
static void freeChains(Chains *chains);
static uint32_t chainKey(const Side *side, size_t record, size_t depth);
static bool sameSites(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1);
static size_t runEnd(const Side *side, size_t from, size_t to, size_t depth, uint32_t key);
static bool sameRuns(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1, size_t depth);
static void alignEnds(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1);
static void align(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1, size_t depth);
static bool alignEdits(Alignment *alignment, const bool *edited, uint32_t seed);
static bool
mergeEdits(const SeedBuilder *builder, const Serialised *built, uint32_t seed, ByteBuffer *out);

bool seedBuilder_open(
	SeedBuilder *builder,
	const ByteBuffer *input,
	uint32_t inputSeed,
	const uint8_t *awayTeam,
	char *error,
	size_t errorSize
) {
	*builder = (SeedBuilder){.awayTeam = awayTeam};
	const char *why = NULL;
	if (!fixture_findNext(&builder->next, &why)) {
		(void)snprintf(error, errorSize, "no next fixture to rebuild: %s", why);
		return false;
	}
	// Compare the input with a build of its own seed, both loaded and serialised again so that the
	// serialiser's own differences (a header flag the deserialiser sets) cancel out.
	Serialised built = {0};
	Serialised reference = {0};
	bool ok = buildSerialised(builder, inputSeed, &built, false) &&
						roundTrip(&built.bytes, &reference) && roundTrip(input, &builder->input);
	trace_free(&built);
	if (!ok) {
		(void)snprintf(error, errorSize, "building or loading the match failed; see harness.log");
	} else if (!sameLayout(&reference, &builder->input)) {
		(void)snprintf(
			error,
			errorSize,
			"the input is not the game's next fixture (%s%s) or its layout was edited",
			builder->next.description,
			awayTeam ? ", against its \"away_team\"" : ""
		);
		ok = false;
	}
	if (ok) {
		builder->edited = calloc(builder->input.recordCount, sizeof *builder->edited);
		ok = builder->edited != NULL;
		if (!ok) {
			(void)snprintf(error, errorSize, "out of memory recording the input's edits");
		}
	}
	for (size_t i = 0; ok && i < builder->input.recordCount; ++i) {
		builder->edited[i] = !sameRecord(&reference, &builder->input, i);
		builder->editCount += builder->edited[i];
	}
	trace_free(&reference);
	if (!ok) {
		seedBuilder_close(builder);
		return false;
	}
	InitializeCriticalSection(&builder->lock);
	log_message(
		"seed builds: %s%s; %zu fields differ from the game's build",
		builder->next.description,
		awayTeam ? " (another away team)" : "",
		seedBuilder_editCount(builder)
	);
	return true;
}

bool seedBuilder_dropAwayEdits(SeedBuilder *builder, size_t *dropped) {
	*dropped = 0;
	if (builder->editCount == 0) {
		return true;
	}
	Side side = {.serialised = &builder->input};
	if (!parseChains(&builder->input, &side.chains)) {
		log_message("cannot identify the away team's edits");
		return false;
	}
	for (size_t i = 0; i < builder->input.recordCount; ++i) {
		if (builder->edited[i] && chainKey(&side, i, 0) == SITE_PART_AWAY) {
			builder->edited[i] = false;
			++*dropped;
		}
	}
	builder->editCount -= *dropped;
	freeChains(&side.chains);
	return true;
}

size_t seedBuilder_editCount(const SeedBuilder *builder) {
	return builder->editCount;
}

bool seedBuilder_build(SeedBuilder *builder, uint32_t seed, ByteBuffer *out) {
	Serialised built = {0};
	bool applyEdits = builder->editCount != 0;
	EnterCriticalSection(&builder->lock);
	bool ok = buildSerialised(builder, seed, &built, applyEdits);
	LeaveCriticalSection(&builder->lock);
	if (ok && applyEdits) {
		ok = mergeEdits(builder, &built, seed, out);
	} else if (ok) {
		buffer_free(out);
		*out = built.bytes;
		built.bytes = (ByteBuffer){0};
	}
	trace_free(&built);
	return ok;
}

void seedBuilder_close(SeedBuilder *builder) {
	if (builder->edited) {
		DeleteCriticalSection(&builder->lock);
	}
	trace_free(&builder->input);
	free(builder->edited);
	*builder = (SeedBuilder){0};
}

// Notes `slot` for swapping if it holds `expected`.
static void addSwap(Swap *swaps, size_t *count, uint8_t *slot, const uint8_t *expected) {
	uint8_t *value = NULL;
	if (*count < MAX_SWAPS && memory_readPointer(slot, 0, &value) && value == expected) {
		swaps[(*count)++] = (Swap){slot, value};
	}
}

// Puts builder->awayTeam (if set) in place of the fixture's away team, wherever the build looks
// for it: the game's own fixture (rather than a copy: the team selection the build runs is handed
// the fixture and may keep it), the copies of it among the matches the human manager attends
// (without them the game picks the human's side itself, as for an AI manager) and the home team's
// next-match record. Returns how many pointers were swapped.
static size_t swapAwayTeam(const SeedBuilder *builder, Swap *swaps) {
	uint8_t *fixture = builder->next.fixture;
	uint8_t *home = NULL;
	uint8_t *away = NULL;
	if (!builder->awayTeam || !memory_readPointer(fixture, FIXTURE_HOME, &home) ||
			!memory_readPointer(fixture, FIXTURE_AWAY, &away)) {
		return 0;
	}
	size_t count = 0;
	addSwap(swaps, &count, fixture + FIXTURE_AWAY, away);
	uint8_t *holder = NULL;
	uint8_t *start = NULL;
	uint8_t *end = NULL;
	if (memory_readPointer(game_address(RVA_HUMAN_MATCHES_HOLDER), 0, &holder) &&
			memory_readPointer(holder, HUMAN_MATCHES, &start) &&
			memory_readPointer(holder, HUMAN_MATCHES + sizeof(void *), &end)) {
		for (uint8_t *entry = start; entry < end; entry += sizeof(void *)) {
			uint8_t *match = NULL;
			uint8_t *copy = NULL;
			uint8_t *copyHome = NULL;
			if (memory_readPointer(entry, 0, &match) && memory_readPointer(match, 0, &copy) &&
					copy != fixture && memory_readPointer(copy, FIXTURE_HOME, &copyHome) &&
					copyHome == home) {
				addSwap(swaps, &count, copy + FIXTURE_AWAY, away);
			}
		}
	}
	uint8_t *data = NULL;
	if (memory_readPointer(home, TEAM_IN_GAME_DATA, &data)) {
		addSwap(swaps, &count, data + NEXT_MATCH_OPPONENT, away);
	}
	for (size_t i = 0; i < count; ++i) {
		memcpy(swaps[i].slot, &builder->awayTeam, sizeof builder->awayTeam);
	}
	return count;
}

static void restoreSwaps(const Swap *swaps, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		memcpy(swaps[i].slot, &swaps[i].original, sizeof swaps[i].original);
	}
}

// The game's build of the fixture with `seed` (against builder->awayTeam if set), serialised with
// its write records.
static bool
buildSerialised(const SeedBuilder *builder, uint32_t seed, Serialised *out, bool traced) {
	Swap swaps[MAX_SWAPS];
	size_t swapped = swapAwayTeam(builder, swaps);
	uint8_t *match = match_build(builder->next.fixture, seed);
	bool ok = match && trace_serialize(match, out, traced);
	if (match) {
		match_freeBuilt(match);
	}
	restoreSwaps(swaps, swapped);
	return ok;
}

// Loads serialised match bytes and serialises the match again, with its write records.
static bool roundTrip(const ByteBuffer *bytes, Serialised *out) {
	ByteBuffer copy = {0};
	if (!buffer_append(&copy, bytes->data, bytes->length)) {
		return false;
	}
	uint8_t *match = match_deserialize(&copy);
	bool ok = match && trace_serialize(match, out, true);
	if (match) {
		match_delete(match);
	}
	buffer_free(&copy);
	return ok;
}

// The same writes in the same order (values, and so string lengths, may differ).
static bool sameLayout(const Serialised *a, const Serialised *b) {
	if (a->recordCount != b->recordCount) {
		return false;
	}
	for (size_t i = 0; i < a->recordCount; ++i) {
		if (a->records[i].site != b->records[i].site) {
			return false;
		}
	}
	return true;
}

static bool sameRecord(const Serialised *a, const Serialised *b, size_t i) {
	const TraceRecord *x = &a->records[i];
	const TraceRecord *y = &b->records[i];
	return x->length == y->length &&
				 memcmp(a->bytes.data + x->offset, b->bytes.data + y->offset, x->length) == 0;
}

static bool appendRecord(ByteBuffer *out, const Serialised *from, size_t i) {
	const TraceRecord *record = &from->records[i];
	return buffer_append(out, from->bytes.data + record->offset, record->length);
}

static bool parseChains(const Serialised *serialised, Chains *chains) {
	*chains = (Chains){0};
	for (size_t i = 0; i < serialised->recordCount; ++i) {
		if (serialised->records[i].path >= serialised->pathCount) {
			log_message("invalid call chain at record %zu", i);
			return false;
		}
	}
	size_t total = 0;
	for (size_t p = 0; p < serialised->pathCount; ++p) {
		for (const char *c = serialised->paths[p]; *c; ++c) {
			total += *c == '>';
		}
		++total;
	}
	size_t paths = serialised->pathCount ? serialised->pathCount : 1;
	chains->rvas = malloc((total ? total : 1) * sizeof *chains->rvas);
	chains->starts = malloc(paths * sizeof *chains->starts);
	chains->lengths = malloc(paths * sizeof *chains->lengths);
	if (!chains->rvas || !chains->starts || !chains->lengths) {
		freeChains(chains);
		return false;
	}
	size_t used = 0;
	for (size_t p = 0; p < serialised->pathCount; ++p) {
		chains->starts[p] = used;
		for (const char *text = serialised->paths[p]; *text && used < total;) {
			char *end = NULL;
			chains->rvas[used++] = (uint32_t)strtoul(text, &end, 16);
			text = *end == '>' ? end + 1 : end + strlen(end);
		}
		chains->lengths[p] = used - chains->starts[p];
		if (chains->lengths[p] > chains->deepest) {
			chains->deepest = chains->lengths[p];
		}
	}
	return true;
}

static void freeChains(Chains *chains) {
	free(chains->rvas);
	free(chains->starts);
	free(chains->lengths);
	*chains = (Chains){0};
}

// A record's call chain at `depth`: the return address there, then the write's own call site,
// then 0.
static uint32_t chainKey(const Side *side, size_t record, size_t depth) {
	const TraceRecord *entry = &side->serialised->records[record];
	size_t length = side->chains.lengths[entry->path];
	if (depth < length) {
		return side->chains.rvas[side->chains.starts[entry->path] + depth];
	}
	return depth == length ? entry->site : 0;
}

static bool sameSites(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1) {
	if (i1 - i0 != j1 - j0) {
		return false;
	}
	for (size_t k = 0; k < i1 - i0; ++k) {
		if (a->input.serialised->records[i0 + k].site != a->built.serialised->records[j0 + k].site) {
			return false;
		}
	}
	return true;
}

// The end of the run of records from `from` whose chain at `depth` is `key`.
static size_t runEnd(const Side *side, size_t from, size_t to, size_t depth, uint32_t key) {
	while (from < to && chainKey(side, from, depth) == key) {
		++from;
	}
	return from;
}

// Whether both ranges are the same sequence of runs at `depth` (e.g. the same parts of a team,
// whatever each part holds).
static bool sameRuns(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1, size_t depth) {
	size_t i = i0;
	size_t j = j0;
	while (i < i1 && j < j1) {
		uint32_t key = chainKey(&a->input, i, depth);
		if (chainKey(&a->built, j, depth) != key) {
			return false;
		}
		i = runEnd(&a->input, i, i1, depth, key);
		j = runEnd(&a->built, j, j1, depth, key);
	}
	return i == i1 && j == j1;
}

// Pairs the records two ranges share at their start and at their end.
static void alignEnds(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1) {
	const TraceRecord *input = a->input.serialised->records;
	const TraceRecord *built = a->built.serialised->records;
	size_t shorter = i1 - i0 < j1 - j0 ? i1 - i0 : j1 - j0;
	size_t head = 0;
	while (head < shorter && input[i0 + head].site == built[j0 + head].site) {
		a->source[j0 + head] = i0 + head;
		++head;
	}
	size_t tail = 0;
	while (head + tail < shorter && input[i1 - 1 - tail].site == built[j1 - 1 - tail].site) {
		a->source[j1 - 1 - tail] = i1 - 1 - tail;
		++tail;
	}
}

// Pairs the input's records [i0, i1) with the build's [j0, j1). Where their writes differ, both
// are split into the parts written by each call at `depth` of the serialiser's call chains (the
// match's blocks, then a team's parts, then a tactic's, ...) and, if both have the same parts in
// the same order, each pair of parts is aligned one level deeper. Otherwise only the records the
// two ranges share at their start and end are paired.
static void align(const Alignment *a, size_t i0, size_t i1, size_t j0, size_t j1, size_t depth) {
	if (sameSites(a, i0, i1, j0, j1)) {
		for (size_t j = j0; j < j1; ++j) {
			a->source[j] = i0 + (j - j0);
		}
		return;
	}
	size_t deepest = a->input.chains.deepest > a->built.chains.deepest ? a->input.chains.deepest
																																		 : a->built.chains.deepest;
	if (depth > deepest || !sameRuns(a, i0, i1, j0, j1, depth)) {
		alignEnds(a, i0, i1, j0, j1);
		return;
	}
	size_t i = i0;
	size_t j = j0;
	while (i < i1 && j < j1) {
		uint32_t key = chainKey(&a->input, i, depth);
		size_t iEnd = runEnd(&a->input, i, i1, depth, key);
		size_t jEnd = runEnd(&a->built, j, j1, depth, key);
		align(a, i, iEnd, j, jEnd, depth + 1);
		i = iEnd;
		j = jEnd;
	}
}

static bool alignEdits(Alignment *alignment, const bool *edited, uint32_t seed) {
	size_t inputCount = alignment->input.serialised->recordCount;
	size_t builtCount = alignment->built.serialised->recordCount;
	alignment->source = malloc((builtCount ? builtCount : 1) * sizeof *alignment->source);
	bool *placed = calloc(inputCount ? inputCount : 1, sizeof *placed);
	bool ok = alignment->source && placed &&
						parseChains(alignment->input.serialised, &alignment->input.chains) &&
						parseChains(alignment->built.serialised, &alignment->built.chains);
	if (!ok) {
		log_message("seed %u: cannot align the input's edits", seed);
	} else {
		for (size_t j = 0; j < builtCount; ++j) {
			alignment->source[j] = SIZE_MAX;
		}
		align(alignment, 0, inputCount, 0, builtCount, 0);
		for (size_t j = 0; j < builtCount; ++j) {
			if (alignment->source[j] < inputCount) {
				placed[alignment->source[j]] = true;
			}
		}
	}
	for (size_t i = 0; ok && i < inputCount; ++i) {
		if (edited[i] && !placed[i]) {
			log_message(
				"seed %u: the game's build has a different structure around edited field #%zu", seed, i
			);
			ok = false;
		}
	}
	free(placed);
	return ok;
}

// The game's build for `seed` with the input's edits re-applied. The build's structure can differ
// from the input's (the AI manager picks its tactic with the seed, which changes how many player
// instructions it writes; another away team has another squad), so the two are aligned by
// align(). An edit that cannot be paired with a record of the build fails the seed.
static bool
mergeEdits(const SeedBuilder *builder, const Serialised *built, uint32_t seed, ByteBuffer *out) {
	const Serialised *input = &builder->input;
	size_t builtCount = built->recordCount;
	Alignment a = {.input = {.serialised = input}, .built = {.serialised = built}};
	bool ok = sameLayout(input, built) || alignEdits(&a, builder->edited, seed);
	if (ok) {
		buffer_clear(out);
	}
	for (size_t j = 0; ok && j < builtCount; ++j) {
		size_t i = a.source ? a.source[j] : j;
		bool fromInput = i != SIZE_MAX && builder->edited[i];
		ok = appendRecord(out, fromInput ? input : built, fromInput ? i : j);
	}
	freeChains(&a.input.chains);
	freeChains(&a.built.chains);
	free(a.source);
	return ok;
}
