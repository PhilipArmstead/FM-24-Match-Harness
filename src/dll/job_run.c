// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "run": plays one match input once per seed and writes a CSV of results.

#include "files.h"
#include "job.h"
#include "log.h"
#include "match.h"
#include "memory.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


static double elapsedMilliseconds(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER frequency) {
	return 1000.0 * (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
}

// Plays the input with one seed; returns false if the match could not be loaded.
static bool
playSeed(ByteBuffer *input, size_t seedOffset, uint32_t seed, bool deleteMatch, FILE *csv) {
	memcpy(input->data + seedOffset, &seed, sizeof seed);
	LARGE_INTEGER frequency;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);
	uint8_t *match = match_deserialize(input);
	if (!match) {
		log_message("seed %u: deserialize failed", seed);
		return false;
	}
	match_play(match);
	QueryPerformanceCounter(&end);
	MatchResult result = match_result(match);
	fprintf(
		csv,
		"%u,%u,%u,%u,%u,%016llx,%.1f\n",
		result.seed,
		result.homeGoals,
		result.awayGoals,
		result.clockTicks,
		result.finished ? 1U : 0U,
		(unsigned long long)result.finalRngState,
		elapsedMilliseconds(start, end, frequency)
	);
	(void)fflush(csv);
	if (deleteMatch) {
		match_delete(match);
	}
	return true;
}

void jobs_run(const Job *job) {
	ByteBuffer input = {0};
	size_t seedOffset = 0;
	if (!files_read(job->input, &input) || !match_seedOffset(input.data, input.length, &seedOffset)) {
		log_message("cannot read the input, or it is not a serialised GAME_MATCH");
		buffer_free(&input);
		return;
	}
	uint32_t fileSeed = memory_loadU32(input.data + seedOffset);
	log_message("input %zu bytes, seed at +%zu = %u", input.length, seedOffset, fileSeed);
	FILE *csv = _wfopen(job->output, L"wb");
	if (!csv) {
		log_message("cannot open the output file");
		buffer_free(&input);
		return;
	}
	fprintf(csv, "seed,home_goals,away_goals,clock_ticks,finished,rng_final,ms\n");
	size_t total = job->keepSeed ? 1 : job->seedCount;
	size_t done = 0;
	while (done < total &&
				 playSeed(
					 &input, seedOffset, job->keepSeed ? fileSeed : job->seeds[done], job->deleteMatch, csv
				 )) {
		++done;
	}
	(void)fclose(csv);
	buffer_free(&input);
	log_message("job finished: %zu/%zu runs", done, total);
}
