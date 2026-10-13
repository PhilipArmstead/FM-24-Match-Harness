// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "build_next": serialise the human manager's next fixture.

#include "fixture.h"
#include "job.h"
#include "log.h"
#include "match.h"
#include "output.h"
#include "trace.h"

#include <stdio.h>
#include <windows.h>


static uint32_t clockSeed(void);

void jobs_buildNext(const Job *job, const wchar_t *directory) {
	NextFixture next;
	const char *error = NULL;
	if (!fixture_findNext(&next, &error)) {
		log_message("build_next: %s", error);
		job_writeResult(directory, "error: %s", error);
		return;
	}
	uint32_t seed = job->hasSeed ? job->seed : clockSeed();
	log_message(
		"build_next: %s, %s, fixture %p, seed %u",
		next.description,
		next.when,
		(void *)next.fixture,
		seed
	);
	uint8_t *match = match_build(next.fixture, seed);
	if (!match) {
		job_writeResult(directory, "error: match allocation failed");
		return;
	}
	Serialised serialised = {0};
	bool ok = trace_serialize(match, &serialised, output_wantsJson(job->output));
	match_freeBuilt(match);
	char source[256];
	(void)snprintf(source, sizeof source, "%s, %s, seed %u", next.description, next.when, seed);
	if (ok && output_write(job->output, &serialised, source, job->full)) {
		log_message("build_next: wrote %zu bytes", serialised.bytes.length);
		job_writeResult(directory, "ok: %s, %zu bytes", source, serialised.bytes.length);
	} else {
		job_writeResult(directory, "error: serialise or write failed; see harness.log");
	}
	trace_free(&serialised);
}

// The seed MatchBuilder_Create would pick: QueryPerformanceCounter in milliseconds.
static uint32_t clockSeed(void) {
	LARGE_INTEGER counter;
	LARGE_INTEGER frequency;
	QueryPerformanceCounter(&counter);
	QueryPerformanceFrequency(&frequency);
	return (uint32_t)((double)counter.LowPart / ((double)(int32_t)frequency.LowPart / 1000.0) + 0.5);
}
