// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "develop": a player's training and development simulated month by month with the game's own
// routine (RVA_PLAYER_DEVELOP), each month's result being the next month's input. The output CSV
// has one row per seed and month, month 0 being the starting values. Months alternate 4 and 5
// training weeks (52 a year) and the player's date of birth moves back by as many days, so they
// age while the game's date stays put.
//
// This changes the game: the routine runs on the game's own player (a copy is not enough, it
// reaches the player through the game's lists; knowledge-base/FM24 Training — Attribute
// Growth.md, section 12) and passes them to other parts of the game (news, contracts, history).
// The simulated values (restoreValues) are put back after each seed and at the end, but the rest
// stays, so users save before and reload afterwards. The simulation runs with the game's other
// threads paused (freeze.h): the routine uses global state they also use.

#include "database.h"
#include "freeze.h"
#include "game.h"
#include "job.h"
#include "labels.h"
#include "log.h"
#include "memory.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum {
	WEEKS_PER_YEAR = 52,
	MONTHS_PER_YEAR = 12,
	DAYS_PER_WEEK = 7,
	DAY_MASK = 0x1FF,
	// u8 in the subobject at [[player+8]+8]: the scheduler skips development while 0x20 is set.
	DEVELOPMENT_FLAGS = 0x142,
	NO_DEVELOPMENT = 0x20,
};

// The routine's first bytes, as game.c checks the match functions (only this job calls it).
static const char developPrologue[] =
	"\x55\x41\x57\x41\x56\x41\x55\x41\x54\x56\x57\x53\x48\x81\xEC\xF8";

static const char *const categoryNames[TRAINING_CATEGORY_COUNT] = {
	"strength",
	"quickness",
	"gk_shot_stopping",
	"gk_handling",
	"defending",
	"tackling",
	"tactics",
	"ball_control",
	"attacking",
	"shooting",
	"set_pieces",
};

typedef struct Simulation {
	uint8_t *player;                      // the game's player, developed in place
	uint8_t original[PLAYER_RECORD_SIZE]; // the player when the job started
	int32_t week;                         // the training manager's week
	unsigned firstDays;                   // days until the player's next development day
	const uint32_t *seeds;
	size_t seedCount;
	unsigned months;
	FILE *csv;
	bool written; // every row was written
} Simulation;

static void simulateAll(void *context);

static const char *checkPlayer(const uint8_t *person, const uint8_t *player);
static bool daysToDevelopment(const uint8_t *person, unsigned *days);
static bool simulateSeed(Simulation *sim, uint32_t seed, unsigned months);
static void restoreValues(Simulation *sim);
static void ageBy(uint8_t *person, unsigned days);
static bool writeHeader(FILE *csv);
static bool writeRow(FILE *csv, uint32_t seed, unsigned month, const uint8_t *player);

void jobs_develop(const Job *job) {
	PeopleIndex people;
	if (!database_loadPeople(&people)) {
		job_writeResult("error: cannot read the people list; is a save loaded?");
		return;
	}
	uint8_t *person = database_findPerson(&people, job->personId);
	uint8_t *player = person ? database_playerRecord(person) : NULL;
	char name[PLAYER_NAME_LENGTH] = "";
	if (person && !database_personName(person, name, sizeof name)) {
		(void)snprintf(name, sizeof name, "(unnamed, UID %u)", job->personId);
	}
	database_freePeople(&people);
	const char *error = !person   ? "no person with that unique ID"
											: !player ? "that person is not a player"
																: checkPlayer(person, player);
	if (error) {
		job_writeResult("error: %s (%u)", error, job->personId);
		return;
	}
	Simulation *sim = calloc(1, sizeof *sim);
	uint8_t *manager = NULL;
	if (!sim || !memory_readPointer(game_address(RVA_TRAINING_MANAGER), 0, &manager) || !manager ||
			!memory_read(manager + TRAINING_MANAGER_WEEK, &sim->week, sizeof sim->week) ||
			!memory_read(player, sim->original, sizeof sim->original) ||
			!daysToDevelopment(person, &sim->firstDays)) {
		job_writeResult("error: cannot read the training manager, the date or the player");
	} else if (!(sim->csv = _wfopen(job->output, L"wb")) || !writeHeader(sim->csv)) {
		job_writeResult("error: cannot write the output file");
	} else {
		sim->player = player;
		sim->seeds = job->seeds;
		sim->seedCount = job->seedCount;
		sim->months = job->months;
		log_message(
			"develop: %s (%u), %zu seeds x %u months, next development in %u days",
			name,
			job->personId,
			job->seedCount,
			job->months,
			sim->firstDays
		);
		FreezeResult frozen = freeze_run(simulateAll, sim);
		bool written = fclose(sim->csv) == 0 && sim->written;
		sim->csv = NULL;
		if (frozen == FREEZE_NOT_PAUSED) {
			job_writeResult("error: cannot pause the game's threads; is the game idle? See harness.log");
		} else if (frozen == FREEZE_TIMED_OUT) {
			job_writeResult(
				"error: the simulation outlasted the watchdog; restart FM24. See harness.log"
			);
		} else if (!written) {
			job_writeResult("error: cannot write the output file");
		} else {
			job_writeResult(
				"ok: %s (%u), %zu seeds x %u months; reload the save before playing on",
				name,
				job->personId,
				job->seedCount,
				job->months
			);
		}
	}
	if (sim && sim->csv) {
		(void)fclose(sim->csv);
	}
	free(sim);
}

// Why this player cannot be simulated, or NULL.
static const char *checkPlayer(const uint8_t *person, const uint8_t *player) {
	if (memcmp(game_address(RVA_PLAYER_DEVELOP), developPrologue, sizeof developPrologue - 1) != 0) {
		return "this game build's development routine is not the one the harness knows";
	}
	// shortcut: db::ACTUAL_PLAYER only; a player who is also staff is a bigger object with the
	// person elsewhere, so the date of birth would be missed. Support it once its layout is known.
	if (person != player + PLAYER_PERSON) {
		return "players who are also staff are not supported";
	}
	uint8_t *offsets = NULL;
	int32_t flagsBase = 0;
	uint8_t flags = 0;
	if (!memory_readPointer(player, 8, &offsets) || !memory_read(offsets + 8, &flagsBase, 4) ||
			!memory_read(player + flagsBase + DEVELOPMENT_FLAGS, &flags, 1)) {
		return "cannot read the player's development flags";
	}
	return flags & NO_DEVELOPMENT ? "the game does not develop this player" : NULL;
}

static bool isLeap(int year) {
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

// Days from the game's date to the player's next development day: day (id % 28) + 1 of a month,
// id being the u32 at person+8 (sub_144F6FC80). On that day itself, the game has already run it.
static bool daysToDevelopment(const uint8_t *person, unsigned *days) {
	static const int monthLengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint32_t id = 0;
	uint16_t field = 0;
	uint16_t year = 0;
	const uint8_t *date = game_address(RVA_CURRENT_DATE);
	if (!memory_readU32(person, 8, &id) || !memory_readU16(date, 0, &field) ||
			!memory_readU16(date, 2, &year)) {
		return false;
	}
	int day =
		(field & DAY_MASK) - 1; // the field counts from 1 (191 = 10 July 2023, as the game shows)
	int month = 0;
	int length = monthLengths[0];
	while (month < 11 && day >= length) {
		day -= length;
		++month;
		length = monthLengths[month] + (month == 1 && isLeap(year));
	}
	int target = (int)(id % 28); // day of the month, from 0
	*days = (unsigned)(day < target ? target - day : length - day + target);
	return true;
}

bool develop_runHere(
	uint8_t *player,
	const uint32_t *seeds,
	size_t seedCount,
	const wchar_t *output
) {
	Simulation *sim = calloc(1, sizeof *sim);
	if (!sim) {
		return false;
	}
	// firstDays stays 0: the development is due now.
	sim->player = player;
	sim->seeds = seeds;
	sim->seedCount = seedCount;
	sim->months = 1;
	uint8_t *manager = NULL;
	bool ok = memory_readPointer(game_address(RVA_TRAINING_MANAGER), 0, &manager) && manager &&
						memory_read(manager + TRAINING_MANAGER_WEEK, &sim->week, sizeof sim->week) &&
						memory_read(player, sim->original, sizeof sim->original) &&
						(sim->csv = _wfopen(output, L"wb")) != NULL && writeHeader(sim->csv);
	if (ok) {
		simulateAll(sim);
		ok = sim->written;
	}
	if (sim->csv) {
		ok = fclose(sim->csv) == 0 && ok;
	}
	free(sim);
	return ok;
}

// Runs on freeze_run's worker thread while the game's own threads are paused, or on the game's
// thread from develop_runHere.
static void simulateAll(void *context) {
	Simulation *sim = context;
	sim->written = true;
	for (size_t i = 0; i < sim->seedCount && sim->written; ++i) {
		sim->written = simulateSeed(sim, sim->seeds[i], sim->months);
	}
	restoreValues(sim);
}

static bool simulateSeed(Simulation *sim, uint32_t seed, unsigned months) {
	PlayerDevelopFn develop = (PlayerDevelopFn)game_function(RVA_PLAYER_DEVELOP);
	restoreValues(sim);
	rng_seedThread(seed); // the routine draws from this thread's PCG
	if (!writeRow(sim->csv, seed, 0, sim->player)) {
		return false;
	}
	for (unsigned month = 0; month < months; ++month) {
		// Month 1 ends on the player's next development day, as in the game: only the weekly
		// training due before it counts.
		// shortcut: whole weeks until that day, not the game's own weekly tick dates (the mod-7
		// test in sub_1438CFCC0 @0x1438D009B); trace those if month 1 needs to be exact.
		unsigned inYear = (month - 1) % MONTHS_PER_YEAR;
		int32_t weeks = month == 0 ? (int32_t)(sim->firstDays / DAYS_PER_WEEK)
															 : (int32_t)(WEEKS_PER_YEAR * (inYear + 1) / MONTHS_PER_YEAR -
																					 WEEKS_PER_YEAR * inYear / MONTHS_PER_YEAR);
		unsigned days = month == 0 ? sim->firstDays : (unsigned)weeks * DAYS_PER_WEEK;
		// The routine trains the weeks since PLAYER_TRAINING_WEEK (a signed comparison), then
		// sets it to the manager's week again.
		int32_t lastTrained = sim->week - weeks;
		// shortcut: 0 means "never trained" to the game, so in the save's week 4 or 5 a month
		// gets one extra week. Harmless after that.
		memcpy(sim->player + PLAYER_TRAINING_WEEK, &(int32_t){lastTrained ? lastTrained : -1}, 4);
		ageBy(sim->player + PLAYER_PERSON, days);
		develop(sim->player, false, 1);
		if (!writeRow(sim->csv, seed, month + 1, sim->player)) {
			return false;
		}
	}
	return true;
}

// The fields the simulation is about, the only ones put back between seeds and at the end. Nothing
// points into them. Everything else the routine changed stays as it left it: the player's other
// fields describe lists and objects shared with the rest of the game (e.g. the list at +0x178 that
// the player's teardown destroys), and putting them back alone crashed the game later.
static const struct {
	size_t offset;
	size_t size;
} restoredFields[] = {
	{PLAYER_WEIGHT, 1},
	{PLAYER_HEIGHT, 2},
	{PLAYER_HOME_REPUTATION, 6}, // home, current and world
	{PLAYER_TRAINING_WEEK, 4},
	{PLAYER_TRAINING_PROGRESS, TRAINING_CATEGORY_COUNT},
	{PLAYER_CURRENT_ABILITY, 2},
	{PLAYER_POTENTIAL_ABILITY, 2},
	{PLAYER_RATINGS, PLAYER_RATING_COUNT},
	{PLAYER_PERSON + PERSON_DATE_OF_BIRTH, 4},
	{PLAYER_PERSON + PERSON_PERSONALITY, 8}, // youth players' personalities develop too
};

// Puts the simulated fields back as they were when the job started, so every seed starts with the
// same ratings, abilities, progress, build, reputation, personality and age.
static void restoreValues(Simulation *sim) {
	for (size_t i = 0; i < sizeof restoredFields / sizeof restoredFields[0]; ++i) {
		size_t offset = restoredFields[i].offset;
		memcpy(sim->player + offset, sim->original + offset, restoredFields[i].size);
	}
}

// Moves a date of birth (PERSON_DATE_OF_BIRTH: u16 day of year from 1 | other bits, u16 year)
// back by `days`, keeping the bits above the day.
static void ageBy(uint8_t *person, unsigned days) {
	uint8_t *date = person + PERSON_DATE_OF_BIRTH;
	uint16_t field = memory_loadU16(date);
	int year = memory_loadU16(date + 2);
	int day = (field & DAY_MASK) - (int)days;
	while (day < 1) {
		--year;
		day += isLeap(year) ? 366 : 365;
	}
	field = (uint16_t)((field & ~DAY_MASK) | day);
	uint16_t year16 = (uint16_t)year;
	memcpy(date, &field, sizeof field);
	memcpy(date + 2, &year16, sizeof year16);
}

static bool writeHeader(FILE *csv) {
	fputs("seed,month,current_ability,potential_ability,weight,height", csv);
	for (size_t i = 0; i < TRAINING_CATEGORY_COUNT; ++i) {
		fprintf(csv, ",progress_%s", categoryNames[i]);
	}
	for (size_t i = 0; i < PLAYER_RATING_COUNT; ++i) {
		fprintf(csv, ",%s", labels_ratingName(i));
	}
	return fputc('\n', csv) != EOF;
}

static bool writeRow(FILE *csv, uint32_t seed, unsigned month, const uint8_t *player) {
	fprintf(
		csv,
		"%u,%u,%u,%u,%u,%u",
		seed,
		month,
		memory_loadU16(player + PLAYER_CURRENT_ABILITY),
		memory_loadU16(player + PLAYER_POTENTIAL_ABILITY),
		player[PLAYER_WEIGHT],
		memory_loadU16(player + PLAYER_HEIGHT)
	);
	for (size_t i = 0; i < TRAINING_CATEGORY_COUNT; ++i) {
		fprintf(csv, ",%d", (int8_t)player[PLAYER_TRAINING_PROGRESS + i]);
	}
	for (size_t i = 0; i < PLAYER_RATING_COUNT; ++i) {
		fprintf(csv, ",%u", player[PLAYER_RATINGS + i]);
	}
	return fputc('\n', csv) != EOF;
}
