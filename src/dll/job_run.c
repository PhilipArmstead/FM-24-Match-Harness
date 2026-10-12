// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "run": plays one match input once per seed and writes a CSV of results.
//
// With `threads` > 1 the seeds are shared out between worker threads, each playing whole matches
// with its own copy of the input and its own game stream. Rows are still written in seed order.

#include "files.h"
#include "game.h"
#include "job.h"
#include "log.h"
#include "match.h"
#include "memory.h"
#include "profiler.h"

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
	volatile LONG next;   // index of the next seed to hand out
	volatile LONG failed; // set when a match cannot be loaded; workers stop taking seeds
	SeedResult *results;
	CRITICAL_SECTION lock; // guards results[].done/ok, written and the CSV
	size_t written;        // rows written so far (always a prefix of the seed list)
	FILE *csv;
	Profiler *profiler; // samples worker 0 (the job thread)
} Run;

typedef struct Worker {
	Run *run;
	unsigned index;
} Worker;

// One game stream per worker slot, created on first use and kept: the game owns its storage.
static DataBuffer *streams[HARNESS_MAX_THREADS];

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

// Plays `input` with `seed` (patched into it); returns false if the match could not be loaded.
static bool playSeed(
	DataBuffer *stream,
	ByteBuffer *input,
	size_t seedOffset,
	uint32_t seed,
	Profiler *profiler,
	SeedResult *out
) {
	memcpy(input->data + seedOffset, &seed, sizeof seed);
	LARGE_INTEGER frequency;
	LARGE_INTEGER start;
	LARGE_INTEGER loaded;
	LARGE_INTEGER end;
	QueryPerformanceFrequency(&frequency);
	double cpuStart = threadCpuMilliseconds();
	QueryPerformanceCounter(&start);
	uint8_t *match = match_deserializeWith(stream, input);
	if (!match) {
		log_message("seed %u: deserialize failed", seed);
		return false;
	}
	QueryPerformanceCounter(&loaded);
	double cpuLoaded = threadCpuMilliseconds();
	match_play(match);
	QueryPerformanceCounter(&end);
	if (profiler) {
		double cpuEnd = threadCpuMilliseconds();
		double wallLoad = elapsedMilliseconds(start, loaded, frequency);
		markPhase(profiler, "deserialize", seed, wallLoad, cpuLoaded - cpuStart);
		markPhase(
			profiler, "play", seed, elapsedMilliseconds(loaded, end, frequency), cpuEnd - cpuLoaded
		);
	}
	out->match = match_result(match);
	out->milliseconds = elapsedMilliseconds(start, end, frequency);
	match_delete(match);
	return true;
}

// Writes the rows that are now complete in seed order. Called with run->lock held.
static void writeReadyRows(Run *run) {
	while (
		run->written < run->total && run->results[run->written].done && run->results[run->written].ok) {
		const SeedResult *result = &run->results[run->written];
		fprintf(
			run->csv,
			"%u,%u,%u,%u,%u,%016llx,%.1f\n",
			result->match.seed,
			result->match.homeGoals,
			result->match.awayGoals,
			result->match.clockTicks,
			result->match.finished ? 1U : 0U,
			(unsigned long long)result->match.finalRngState,
			result->milliseconds
		);
		++run->written;
	}
	(void)fflush(run->csv);
}

static DWORD WINAPI worker(LPVOID parameter) {
	const Worker *self = parameter;
	Run *run = self->run;
	game_initThread();
	if (!streams[self->index] && !(streams[self->index] = stream_create())) {
		log_message("worker %u: cannot create a stream", self->index);
		InterlockedExchange(&run->failed, 1);
		return 1;
	}
	ByteBuffer input = {0}; // a private copy: each worker patches its own seed into it
	if (!buffer_append(&input, run->input->data, run->input->length)) {
		InterlockedExchange(&run->failed, 1);
		return 1;
	}
	Profiler *profiler = self->index == 0 ? run->profiler : NULL;
	for (;;) {
		LONG index = InterlockedIncrement(&run->next) - 1;
		if (run->failed || (size_t)index >= run->total) {
			break;
		}
		uint32_t seed = run->job->keepSeed ? run->fileSeed : run->job->seeds[index];
		SeedResult result = {0};
		result.ok = playSeed(streams[self->index], &input, run->seedOffset, seed, profiler, &result);
		result.done = true;
		EnterCriticalSection(&run->lock);
		run->results[index] = result;
		writeReadyRows(run);
		LeaveCriticalSection(&run->lock);
		if (!result.ok) {
			InterlockedExchange(&run->failed, 1);
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
	*run = (Run){.job = job, .input = input, .total = job->keepSeed ? 1 : job->seedCount};
	if (!match_seedOffset(input->data, input->length, &run->seedOffset)) {
		log_message("the input is not a serialised GAME_MATCH");
		return false;
	}
	run->fileSeed = memory_loadU32(input->data + run->seedOffset);
	run->results = calloc(run->total, sizeof *run->results);
	run->csv = _wfopen(job->output, L"wb");
	if (!run->results || !run->csv) {
		log_message("out of memory, or cannot open the output file");
		free(run->results);
		if (run->csv) {
			(void)fclose(run->csv);
		}
		return false;
	}
	fprintf(run->csv, "seed,home_goals,away_goals,clock_ticks,finished,rng_final,ms\n");
	InitializeCriticalSection(&run->lock);
	return true;
}

void jobs_run(const Job *job) {
	ByteBuffer input = {0};
	Run run;
	if (!files_read(job->input, &input) || !openRun(job, &input, &run)) {
		log_message("cannot start the run");
		buffer_free(&input);
		return;
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
	LARGE_INTEGER frequency;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);
	run.profiler = job->profile[0] ? profiler_start(job->profile) : NULL;
	runWorkers(&run, threads);
	profiler_stop(run.profiler);
	QueryPerformanceCounter(&end);
	(void)fclose(run.csv);
	DeleteCriticalSection(&run.lock);
	free(run.results);
	buffer_free(&input);
	log_message(
		"job finished: %zu/%zu runs in %.1f s",
		run.written,
		run.total,
		elapsedMilliseconds(start, end, frequency) / 1000.0
	);
}
