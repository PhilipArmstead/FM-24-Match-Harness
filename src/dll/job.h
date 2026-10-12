// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Jobs submitted by the runner: a job file of key=value lines, and a one-line result.
#pragma once

#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>


typedef enum JobCommand {
	JOB_NONE,       // no "command=" line: the job is rejected
	JOB_RUN,        // play an input with a list of seeds, write a CSV
	JOB_BUILD_NEXT, // build the human manager's next fixture to .bin or .json
	JOB_EXPORT,     // convert a .bin input into a .json config
	JOB_REBUILD,    // recompute what the game derives from edited player ratings
} JobCommand;

typedef struct Job {
	JobCommand command;
	wchar_t input[HARNESS_PATH_LENGTH];
	wchar_t output[HARNESS_PATH_LENGTH];
	wchar_t records[HARNESS_PATH_LENGTH]; // rebuild: the config's write records ("site length" lines)
	wchar_t profile[HARNESS_PATH_LENGTH]; // run: optional sample file (see profiler.h)
	uint32_t *seeds;                      // run
	size_t seedCount;
	unsigned threads; // run: worker threads (1..HARNESS_MAX_THREADS)
	bool keepSeed;    // run: play the input's own seed once
	bool hasSeed;     // build_next: use `seed` instead of a clock value
	uint32_t seed;
} Job;

// Reads <directory>\job.txt; logs and returns false if it is missing or malformed.
bool job_read(const wchar_t *directory, Job *job);
void job_free(Job *job);

// Writes the job's one-line outcome to <directory>\result.txt (must start with "ok" or "error").
void job_writeResult(const wchar_t *directory, const char *format, ...)
	__attribute__((format(gnu_printf, 2, 3)));

// The job implementations (job_*.c); each reports through job_writeResult and/or the log.
void jobs_run(const Job *job);
void jobs_buildNext(const Job *job, const wchar_t *directory);
void jobs_export(const Job *job, const wchar_t *directory);
void jobs_rebuild(const Job *job, const wchar_t *directory);
