// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "job.h"

#include "log.h"
#include "protocol.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum { MAX_SEED_RANGE = 10000000 };

static bool widen(const char *text, wchar_t *out, size_t size);
static bool appendSeed(Job *job, size_t *capacity, uint32_t seed);
static bool parseNumber(const char *text, char **end, unsigned long long *value);
static bool parseRange(const char *part, unsigned long long *first, unsigned long long *last);
static bool parseSeeds(Job *job, char *spec);
static bool parseCommand(Job *job, const char *value);
static bool copyText(const char *value, char *out, size_t size);
static bool parseLine(Job *job, char *key, char *value);
static bool isComplete(const Job *job);

bool job_parse(char *text, Job *job) {
	*job = (Job){.threads = 1};
	bool ok = true;
	for (char *line = text; ok && *line;) {
		size_t length = strcspn(line, "\n");
		char *next = line[length] ? line + length + 1 : line + length;
		line[length] = 0;
		line[strcspn(line, "\r")] = 0;
		char *equals = strchr(line, '=');
		if (equals) {
			*equals = 0;
			ok = parseLine(job, line, equals + 1);
			if (!ok) {
				log_message("bad job line: %s=%s", line, equals + 1);
			}
		}
		line = next;
	}
	if (ok && !isComplete(job)) {
		log_message("job is missing required keys");
		ok = false;
	}
	if (!ok) {
		job_free(job);
	}
	return ok;
}

void job_free(Job *job) {
	free(job->seeds);
	*job = (Job){0};
}

static char result[HARNESS_RESULT_LENGTH]; // the current job's outcome (jobs run one at a time)

void job_writeResult(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int length = vsnprintf(result, sizeof result, format, args);
	va_end(args);
	if (length < 0) {
		result[0] = 0;
		log_message("cannot format the job result");
	}
}

const char *job_takeResult(void) {
	static char taken[HARNESS_RESULT_LENGTH];
	memcpy(taken, result, sizeof taken);
	result[0] = 0;
	return taken;
}

static bool widen(const char *text, wchar_t *out, size_t size) {
	return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, (int)size) > 0;
}

static bool appendSeed(Job *job, size_t *capacity, uint32_t seed) {
	if (job->seedCount == *capacity) {
		size_t grown = *capacity ? *capacity * 2 : 64;
		uint32_t *seeds = realloc(job->seeds, grown * sizeof *seeds);
		if (!seeds) {
			return false;
		}
		job->seeds = seeds;
		*capacity = grown;
	}
	job->seeds[job->seedCount++] = seed;
	return true;
}

// A decimal number at `text`; `end` is left after it. False if there is none or it overflows.
static bool parseNumber(const char *text, char **end, unsigned long long *value) {
	if (*text < '0' || *text > '9') {
		return false;
	}
	errno = 0;
	*value = strtoull(text, end, 10);
	return errno == 0 && *end != text;
}

// "first" or "first-last".
static bool parseRange(const char *part, unsigned long long *first, unsigned long long *last) {
	char *end = NULL;
	if (!parseNumber(part, &end, first)) {
		return false;
	}
	*last = *first;
	if (*end == '-' && !parseNumber(end + 1, &end, last)) {
		return false;
	}
	return *end == 0;
}

// "42", "1-1000" or "1,5,10-20".
static bool parseSeeds(Job *job, char *spec) {
	free(job->seeds);
	job->seeds = NULL;
	job->seedCount = 0;
	job->keepSeed = strcmp(spec, "keep") == 0;
	if (job->keepSeed) {
		return true;
	}
	size_t capacity = 0;
	for (char *part = spec; part && *part;) {
		char *comma = strchr(part, ',');
		if (comma) {
			if (!comma[1]) {
				return false;
			}
			*comma = 0;
		}
		unsigned long long first = 0;
		unsigned long long last = 0;
		if (!parseRange(part, &first, &last) || last < first || last - first > MAX_SEED_RANGE ||
				last > UINT32_MAX) {
			return false;
		}
		for (unsigned long long seed = first; seed <= last; ++seed) {
			if (!appendSeed(job, &capacity, (uint32_t)seed)) {
				return false;
			}
		}
		part = comma ? comma + 1 : NULL;
	}
	return job->seedCount > 0;
}

static bool parseCommand(Job *job, const char *value) {
	static const struct {
		const char *name;
		JobCommand command;
	} commands[] = {
		{"run", JOB_RUN},
		{"build_next", JOB_BUILD_NEXT},
		{"inspect", JOB_INSPECT},
		{"replace_away", JOB_REPLACE_AWAY},
		{"replace_home", JOB_REPLACE_HOME},
		{"list_teams", JOB_LIST_TEAMS},
	};
	for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
		if (strcmp(value, commands[i].name) == 0) {
			job->command = commands[i].command;
			return true;
		}
	}
	return false;
}

static bool copyText(const char *value, char *out, size_t size) {
	size_t length = strlen(value);
	if (length >= size) {
		return false;
	}
	memcpy(out, value, length + 1);
	return true;
}

static bool parseSeed(Job *job, const char *value) {
	if (strcmp(value, "random") == 0) {
		job->hasSeed = false;
		job->seed = 0;
		return true;
	}
	char *end = NULL;
	unsigned long long seed = 0;
	if (!parseNumber(value, &end, &seed) || *end != 0 || seed > UINT32_MAX) {
		return false;
	}
	job->hasSeed = true;
	job->seed = (uint32_t)seed;
	return true;
}

static bool parseThreads(Job *job, const char *value) {
	char *end = NULL;
	unsigned long long threads = 0;
	if (!parseNumber(value, &end, &threads) || *end != 0 || threads < 1 ||
			threads > HARNESS_MAX_THREADS) {
		return false;
	}
	job->threads = (unsigned)threads;
	return true;
}

static bool parseLine(Job *job, char *key, char *value) {
	if (strcmp(key, "command") == 0) {
		return parseCommand(job, value);
	}
	if (strcmp(key, "team") == 0) {
		return copyText(value, job->team, sizeof job->team);
	}
	if (strcmp(key, "home_team") == 0 || strcmp(key, "away_team") == 0) {
		unsigned side = strcmp(key, "away_team") == 0;
		return copyText(value, job->teams[side], sizeof job->teams[side]);
	}
	if (strcmp(key, "input") == 0) {
		return widen(value, job->input, HARNESS_PATH_LENGTH);
	}
	if (strcmp(key, "output") == 0) {
		return widen(value, job->output, HARNESS_PATH_LENGTH);
	}
	if (strcmp(key, "profile") == 0) {
		return widen(value, job->profile, HARNESS_PATH_LENGTH);
	}
	if (strcmp(key, "seeds") == 0) {
		return parseSeeds(job, value);
	}
	if (strcmp(key, "seed") == 0) {
		return parseSeed(job, value);
	}
	if (strcmp(key, "full") == 0) {
		job->full = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "keep_build") == 0) {
		job->keepBuild = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "human_control") == 0) {
		job->humanControl = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "detailed") == 0) {
		job->detailed = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "threads") == 0) {
		return parseThreads(job, value);
	}
	if (strcmp(key, "progress_window") == 0) {
		char *end = NULL;
		unsigned long long window = 0;
		if (!parseNumber(value, &end, &window) || *end != 0 || window > UINTPTR_MAX) {
			return false;
		}
		job->progressWindow = (uintptr_t)window;
		return true;
	}
	return true; // unknown keys are ignored
}

static bool isComplete(const Job *job) {
	bool hasOutput = job->output[0] != 0;
	bool hasInput = job->input[0] != 0;
	switch (job->command) {
		case JOB_NONE: return false;
		case JOB_RUN: return hasInput && hasOutput && (job->keepSeed || job->seedCount > 0);
		case JOB_BUILD_NEXT: return hasOutput;
		case JOB_INSPECT: return hasInput && hasOutput;
		case JOB_REPLACE_AWAY:
		case JOB_REPLACE_HOME: return hasInput && hasOutput && job->team[0] != 0;
		case JOB_LIST_TEAMS: return hasOutput;
	}
	return false;
}
