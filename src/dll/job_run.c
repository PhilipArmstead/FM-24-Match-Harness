// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "run": plays one match input once per seed and writes a CSV of results.
//
// For a list of seeds the input's fixture is rebuilt by the game with each seed and the input's
// edits re-applied (seed_build.h), so a seed plays as it would in the game. With keep_build the
// seed is only patched into the input, keeping the pre-match values it was built with.
//
// With `threads` > 1 the seeds are shared out between worker threads, each playing whole matches
// with its own copy of the input and its own game stream. Rows are still written in seed order.

#include "database.h"
#include "files.h"
#include "game.h"
#include "job.h"
#include "log.h"
#include "match.h"
#include "match_stats.h"
#include "memory.h"
#include "profiler.h"
#include "seed_build.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


typedef struct SeedResult {
	bool done;
	bool ok;
	MatchResult match;
	double milliseconds;
} SeedResult;

// Shared by the workers of one job.
typedef struct Run {
	const Job *job;
	const ByteBuffer *input;
	size_t seedOffset;
	uint32_t fileSeed;
	size_t total;
	atomic_size_t next;
	atomic_bool failed;
	SeedResult *results;
	CRITICAL_SECTION lock; // guards results, counters, progress, the CSV and details
	size_t written;        // rows written so far (always a prefix of the seed list)
	size_t completed;
	unsigned homeWins;
	unsigned draws;
	uintptr_t progressWindow;
	FILE *csv;
	bool outputFailed;
	LARGE_INTEGER frequency;
	Profiler *profiler;        // samples worker 0 (the job thread)
	MatchStatsTotals *details; // --detailed: player and team statistics summed over the seeds
	SeedBuilder *seedBuilder;  // rebuilds the input per seed; NULL: patch the seed into the input
} Run;

typedef struct Worker {
	Run *run;
	unsigned index;
} Worker;

static double elapsedMilliseconds(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER frequency);
static double threadCpuMilliseconds(void);
static void
markPhase(Profiler *profiler, const char *phase, uint32_t seed, double wall, double cpu);
static bool playSeed(
	Run *run,
	unsigned slot,
	ByteBuffer *input,
	SeedBuilder *builder,
	uint32_t seed,
	Profiler *profiler,
	SeedResult *out
);
static void writeReadyRows(Run *run);
static void reportProgress(Run *run);
static DWORD WINAPI worker(LPVOID parameter);
static void runWorkers(Run *run, unsigned count);
static bool openRun(const Job *job, const ByteBuffer *input, Run *run);
static bool closeRun(Run *run);

void jobs_run(const Job *job) {
	ByteBuffer input = {0};
	Run run;
	if (!files_read(job->input, &input) || !openRun(job, &input, &run)) {
		log_message("cannot start the run");
		job_writeResult("error: cannot start the run; see harness.log");
		buffer_free(&input);
		return;
	}
	SeedBuilder seedBuilder;
	if (!job->keepSeed && !job->keepBuild) {
		char error[512] = "";
		uint8_t *awayTeam = NULL;
		if ((job->awayTeam[0] && !database_findTeam(job->awayTeam, &awayTeam, error, sizeof error)) ||
				!seedBuilder_open(&seedBuilder, &input, run.fileSeed, awayTeam, error, sizeof error)) {
			log_message("cannot rebuild per seed: %s", error);
			job_writeResult("error: %s (--keep-build plays the input with only its seed changed)", error);
			(void)closeRun(&run);
			buffer_free(&input);
			return;
		}
		run.seedBuilder = &seedBuilder;
	}
	unsigned threads = job->threads < run.total ? job->threads : (unsigned)run.total;
	log_message(
		"input %zu bytes, seed at +%zu = %u; %zu runs on %u threads",
		input.length,
		run.seedOffset,
		run.fileSeed,
		run.total,
		threads
	);
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	run.profiler = job->profile[0] ? profiler_start(job->profile) : NULL;
	reportProgress(&run);
	if (job->profile[0] && !run.profiler) {
		atomic_store_explicit(&run.failed, true, memory_order_relaxed);
	} else {
		runWorkers(&run, threads);
	}
	profiler_stop(run.profiler);
	QueryPerformanceCounter(&end);
	bool ok = run.written == run.total && !atomic_load_explicit(&run.failed, memory_order_relaxed);
	if (ok && run.details && !matchStats_write(run.details, job->output)) {
		log_message("cannot write the detailed statistics");
		ok = false;
	}
	if (run.seedBuilder) {
		seedBuilder_close(run.seedBuilder);
	}
	ok = closeRun(&run) && ok;
	job_writeResult(
		"%s: %zu/%zu runs%s%s",
		ok ? "ok" : "error",
		run.written,
		run.total,
		run.seedBuilder ? ", each seed rebuilt by the game" : "",
		ok ? "" : "; see harness.log"
	);
	buffer_free(&input);
	log_message(
		"job finished: %zu/%zu runs in %.1f s",
		run.written,
		run.total,
		elapsedMilliseconds(start, end, run.frequency) / 1000.0
	);
}

static double elapsedMilliseconds(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER frequency) {
	return 1000.0 * (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
}

// CPU time the calling thread has used, in milliseconds.
static double threadCpuMilliseconds(void) {
	FILETIME created;
	FILETIME exited;
	FILETIME kernel;
	FILETIME user;
	if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
		return 0;
	}
	ULARGE_INTEGER kernelTime = {.LowPart = kernel.dwLowDateTime, .HighPart = kernel.dwHighDateTime};
	ULARGE_INTEGER userTime = {.LowPart = user.dwLowDateTime, .HighPart = user.dwHighDateTime};
	return (double)(kernelTime.QuadPart + userTime.QuadPart) / 10000.0; // 100 ns units
}

// Notes one phase's wall and CPU time in the profile.
static void
markPhase(Profiler *profiler, const char *phase, uint32_t seed, double wall, double cpu) {
	char text[128];
	(void)snprintf(text, sizeof text, "%s seed=%u wall_ms=%.1f cpu_ms=%.1f", phase, seed, wall, cpu);
	profiler_mark(profiler, text);
}

// Plays `seed`: the game's rebuild for it, or `input` with the seed patched in. Returns false if
// the match could not be built or loaded.
static bool playSeed(
	Run *run,
	unsigned slot,
	ByteBuffer *input,
	SeedBuilder *builder,
	uint32_t seed,
	Profiler *profiler,
	SeedResult *out
) {
	ByteBuffer rebuilt = {0};
	ByteBuffer *bytes = input;
	if (builder) {
		if (!seedBuilder_build(builder, seed, &rebuilt)) {
			log_message("seed %u: rebuild failed", seed);
			buffer_free(&rebuilt);
			return false;
		}
		bytes = &rebuilt;
	} else {
		memcpy(input->data + run->seedOffset, &seed, sizeof seed);
	}
	LARGE_INTEGER start;
	LARGE_INTEGER loaded;
	LARGE_INTEGER end;
	double cpuStart = profiler ? threadCpuMilliseconds() : 0;
	double cpuLoaded = 0;
	QueryPerformanceCounter(&start);
	uint8_t *match = match_deserializeOn(slot, bytes);
	buffer_free(&rebuilt);
	if (!match) {
		log_message("seed %u: deserialize failed", seed);
		return false;
	}
	if (profiler) {
		QueryPerformanceCounter(&loaded);
		cpuLoaded = threadCpuMilliseconds();
	}
	if (!run->job->humanControl) {
		match_handToAssistant(match);
	}
	match_play(match);
	QueryPerformanceCounter(&end);
	if (profiler) {
		double cpuEnd = threadCpuMilliseconds();
		double wallLoad = elapsedMilliseconds(start, loaded, run->frequency);
		markPhase(profiler, "deserialize", seed, wallLoad, cpuLoaded - cpuStart);
		markPhase(
			profiler, "play", seed, elapsedMilliseconds(loaded, end, run->frequency), cpuEnd - cpuLoaded
		);
	}
	out->match = match_result(match);
	bool ok = true;
	if (run->details) {
		EnterCriticalSection(&run->lock);
		ok = matchStats_add(run->details, match);
		LeaveCriticalSection(&run->lock);
		if (!ok) {
			log_message("seed %u: out of memory accumulating player statistics", seed);
		}
	}
	out->milliseconds = elapsedMilliseconds(start, end, run->frequency);
	match_delete(match);
	return ok;
}

// Writes the rows that are now complete in seed order. Called with run->lock held.
static void writeReadyRows(Run *run) {
	if (run->outputFailed) {
		return;
	}
	size_t previous = run->written;
	bool ok = true;
	while (
		run->written < run->total && run->results[run->written].done && run->results[run->written].ok) {
		const SeedResult *result = &run->results[run->written];
		if (fprintf(
					run->csv,
					"%u,%u,%u,%u,%u,%016llx,%.1f\n",
					result->match.seed,
					result->match.homeGoals,
					result->match.awayGoals,
					result->match.clockTicks,
					result->match.finished ? 1U : 0U,
					(unsigned long long)result->match.finalRngState,
					result->milliseconds
				) < 0) {
			ok = false;
			break;
		}
		++run->written;
	}
	if (run->written != previous && fflush(run->csv) != 0) {
		ok = false;
	}
	if (!ok) {
		log_message("cannot write the results CSV");
		run->outputFailed = true;
		atomic_store_explicit(&run->failed, true, memory_order_relaxed);
	}
}

static void reportProgress(Run *run) {
	if (run->progressWindow &&
			!PostMessageW(
				(HWND)run->progressWindow, // NOLINT(performance-no-int-to-ptr): cross-process HWND.
				HARNESS_PROGRESS_MESSAGE,
				(WPARAM)((uint64_t)run->homeWins << 32 | run->completed),
				(LPARAM)((uint64_t)run->draws << 32 | run->total)
			)) {
		log_message("cannot report simulation progress (Windows error %lu)", GetLastError());
		run->progressWindow = 0;
	}
}

static DWORD WINAPI worker(LPVOID parameter) {
	const Worker *self = parameter;
	Run *run = self->run;
	game_initThread();
	if (!match_inputStream(self->index)) {
		log_message("worker %u: cannot create a stream", self->index);
		atomic_store_explicit(&run->failed, true, memory_order_relaxed);
		return 1;
	}
	ByteBuffer input = {0}; // a private copy: each worker patches its own seed into it
	SeedBuilder *builder = run->seedBuilder;
	if (!builder && !buffer_append(&input, run->input->data, run->input->length)) {
		log_message("worker %u: out of memory copying the input", self->index);
		atomic_store_explicit(&run->failed, true, memory_order_relaxed);
		return 1;
	}
	Profiler *profiler = self->index == 0 ? run->profiler : NULL;
	while (!atomic_load_explicit(&run->failed, memory_order_relaxed)) {
		size_t index = atomic_fetch_add_explicit(&run->next, 1, memory_order_relaxed);
		if (index >= run->total) {
			break;
		}
		uint32_t seed = run->job->keepSeed ? run->fileSeed : run->job->seeds[index];
		SeedResult result = {0};
		result.ok = playSeed(run, self->index, &input, builder, seed, profiler, &result);
		result.done = true;
		EnterCriticalSection(&run->lock);
		run->results[index] = result;
		writeReadyRows(run);
		run->completed += result.ok;
		run->homeWins += result.ok && result.match.homeGoals > result.match.awayGoals;
		run->draws += result.ok && result.match.homeGoals == result.match.awayGoals;
		reportProgress(run);
		LeaveCriticalSection(&run->lock);
		if (!result.ok) {
			atomic_store_explicit(&run->failed, true, memory_order_relaxed);
			break;
		}
	}
	buffer_free(&input);
	return 0;
}

// Runs worker 0 on this thread and the others on new threads; returns once all have finished.
static void runWorkers(Run *run, unsigned count) {
	Worker workers[HARNESS_MAX_THREADS];
	HANDLE threads[HARNESS_MAX_THREADS];
	unsigned started = 0;
	for (unsigned i = 1; i < count; ++i) {
		workers[i] = (Worker){run, i};
		threads[started] = CreateThread(NULL, 0, worker, &workers[i], 0, NULL);
		if (!threads[started]) {
			log_message("cannot start worker %u; continuing with %u", i, started + 1);
			break;
		}
		++started;
	}
	workers[0] = (Worker){run, 0};
	worker(&workers[0]);
	if (started > 0) {
		WaitForMultipleObjects(started, threads, TRUE, INFINITE);
	}
	for (unsigned i = 0; i < started; ++i) {
		CloseHandle(threads[i]);
	}
}

static bool openRun(const Job *job, const ByteBuffer *input, Run *run) {
	*run = (Run){.job = job,
		.input = input,
		.total = job->keepSeed ? 1 : job->seedCount,
		.progressWindow = job->progressWindow};
	atomic_init(&run->next, 0);
	atomic_init(&run->failed, false);
	QueryPerformanceFrequency(&run->frequency);
	if (!match_seedOffset(input->data, input->length, &run->seedOffset)) {
		log_message("the input is not a serialised GAME_MATCH");
		return false;
	}
	run->fileSeed = memory_loadU32(input->data + run->seedOffset);
	run->results = calloc(run->total, sizeof *run->results);
	run->details = job->detailed ? calloc(1, sizeof *run->details) : NULL;
	run->csv = _wfopen(job->output, L"wb");
	if (!run->results || !run->csv || (job->detailed && !run->details)) {
		log_message("out of memory, or cannot open the output file");
		free(run->results);
		free(run->details);
		if (run->csv) {
			(void)fclose(run->csv);
		}
		return false;
	}
	InitializeCriticalSection(&run->lock);
	if (fprintf(run->csv, "seed,home_goals,away_goals,clock_ticks,finished,rng_final,ms\n") < 0) {
		log_message("cannot write the results CSV header");
		(void)closeRun(run);
		return false;
	}
	return true;
}

static bool closeRun(Run *run) {
	bool ok = !ferror(run->csv);
	if (fclose(run->csv) != 0) {
		ok = false;
	}
	if (!ok) {
		log_message("cannot finish writing the results CSV");
	}
	DeleteCriticalSection(&run->lock);
	if (run->details) {
		matchStats_free(run->details);
	}
	free(run->details);
	free(run->results);
	return ok;
}
