// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Jobs "build_next" (serialise the human manager's next fixture) and "export" (convert a .bin match
// input into a JSON config).

#include "files.h"
#include "fixture.h"
#include "job.h"
#include "log.h"
#include "match.h"
#include "output.h"
#include "trace.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


// The seed MatchBuilder_Create would pick: QueryPerformanceCounter in milliseconds.
static uint32_t clockSeed(void) {
	LARGE_INTEGER counter;
	LARGE_INTEGER frequency;
	QueryPerformanceCounter(&counter);
	QueryPerformanceFrequency(&frequency);
	return (uint32_t)((double)counter.LowPart / ((double)(int32_t)frequency.LowPart / 1000.0) + 0.5);
}

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
	if (ok && output_write(job->output, &serialised, source)) {
		log_message("build_next: wrote %zu bytes", serialised.bytes.length);
		job_writeResult(directory, "ok: %s, %zu bytes", source, serialised.bytes.length);
	} else {
		job_writeResult(directory, "error: serialise or write failed; see harness.log");
	}
	trace_free(&serialised);
}

void jobs_export(const Job *job, const wchar_t *directory) {
	ByteBuffer input = {0};
	if (!files_read(job->input, &input)) {
		job_writeResult(directory, "error: cannot read input");
		return;
	}
	uint8_t *match = match_deserialize(&input);
	if (!match) {
		job_writeResult(directory, "error: deserialize failed");
		buffer_free(&input);
		return;
	}
	Serialised serialised = {0};
	bool ok = trace_serialize(match, &serialised, output_wantsJson(job->output)) &&
						output_write(job->output, &serialised, "exported from .bin");
	match_delete(match);
	if (ok) {
		bool same = serialised.bytes.length == input.length &&
								memcmp(serialised.bytes.data, input.data, input.length) == 0;
		job_writeResult(
			directory,
			"ok: %zu bytes; round trip %s",
			serialised.bytes.length,
			same ? "identical" : "DIFFERS from the input (the game re-serialises differently)"
		);
	} else {
		job_writeResult(directory, "error: serialise/trace failed; see harness.log");
	}
	trace_free(&serialised);
	buffer_free(&input);
}
