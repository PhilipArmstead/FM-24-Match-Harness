// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "develop": a player's training and development simulated month by month with the game's own
// routine (RVA_PLAYER_DEVELOP), each month's result being the next month's input. The output CSV
// has one row per seed and month, month 0 being the starting values. Months alternate 4 and 5
// training weeks (52 a year), and the game's date moves on to each month's development day while
// the routine runs (then back), so the player ages and trains the weeks being predicted.
//
// This changes the game: the routine runs on the game's own player (a copy is not enough, it
// reaches the player through the game's lists; knowledge-base/FM24 Training — Attribute
// Growth.md, section 12) and passes them to other parts of the game (news, contracts, history).
// The simulated values (restoreValues) are put back after each seed and at the end, but the rest
// stays, so users save before and reload afterwards. The simulation runs with the game's other
// threads paused (freeze.h): the routine uses global state they also use.

#include "database.h"
#include "development_file.h"
#include "freeze.h"
#include "game.h"
#include "job.h"
#include "labels.h"
#include "log.h"
#include "memory.h"
#include "rng.h"
#include "schedule.h"
#include "slug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum {
	WEEKS_PER_YEAR = 52,
	MONTHS_PER_YEAR = 12,
	DAYS_PER_WEEK = 7,
	DAY_MASK = 0x1FF,
	// Values per CSV row after its first two columns: CA, PA, weight, height, progress, ratings.
	ROW_VALUES = 4 + TRAINING_CATEGORY_COUNT + PLAYER_RATING_COUNT,
	// u8 in the subobject at [[player+8]+8]: the scheduler skips development while 0x20 is set.
	DEVELOPMENT_FLAGS = 0x142,
	NO_DEVELOPMENT = 0x20,
	INJURY_LIMIT = 32,          // the game's injuries develop handles per player
	PLANNED_INJURY_SIZE = 0x40, // covers every injury field the routines read (to +0x33)
};

// The routine's first bytes, as game.c checks the match functions (only this job calls it).
static const char developPrologue[] =
	"\x55\x41\x57\x41\x56\x41\x55\x41\x54\x56\x57\x53\x48\x81\xEC\xF8";

typedef struct Simulation {
	uint8_t *player;                      // the game's player, developed in place
	uint8_t original[PLAYER_RECORD_SIZE]; // the player when the job started
	int32_t week;                         // the training manager's week
	unsigned firstDays;                   // days until the player's next development day
	uint8_t startDate[4];                 // the game's date (RVA_CURRENT_DATE), put back after
	const uint32_t *seeds;
	size_t seedCount;
	unsigned months;
	FILE *csv;
	bool perSeed;                    // one row per seed and month, not averages
	double *sums;                    // averages: (months + 1) x ROW_VALUES sums
	bool written;                    // every row was written
	const DevelopmentValues *values; // from a development file, or NULL
	DevelopmentSnapshot before;      // the file's fields before `values` were applied
	DevelopmentSnapshot start;       // and as every seed starts
	const char *failure;             // why the simulation did not run, or NULL
	// The file's injury windows as injuries the routine can read (only the fields it reads).
	uint8_t planned[DEVELOPMENT_INJURY_LIMIT][PLANNED_INJURY_SIZE];
} Simulation;

// The injury list the routine sees for one development (swapInjuries): the game's injuries that
// are still current and the planned ones under way, in develop's own vector.
typedef struct InjuryList {
	uint8_t **slot;     // player + PLAYER_INJURIES while swapped, else NULL
	uint8_t *original;  // the game's list there (may be NULL)
	uint8_t *vector[3]; // begin, end, capacity
	uint8_t *entries[INJURY_LIMIT + DEVELOPMENT_INJURY_LIMIT];
} InjuryList;

typedef struct CurrentSchedule {
	uint8_t *player;
	bool found;
	uint16_t sessions[SCHEDULE_SESSIONS];
} CurrentSchedule;

static void simulateAll(void *context);
static void readSchedule(void *context);

static const char *checkPlayer(const uint8_t *person, const uint8_t *player);
static bool daysToDevelopment(const uint8_t *person, unsigned *days);
static bool simulateSeed(Simulation *sim, uint32_t seed, unsigned months);
static void restoreValues(Simulation *sim);
static void addDays(uint8_t *date, int days);
static void planInjuries(Simulation *sim);
static void swapInjuries(Simulation *sim, bool dropHealed, InjuryList *list);
static bool restoreInjuries(const InjuryList *list);
static bool writeHeader(FILE *csv, bool perSeed);
static bool recordRow(Simulation *sim, uint32_t seed, unsigned month);
static bool writeAverages(Simulation *sim);

// Finds a player the routine can develop; on failure writes the job result and returns false.
static bool
findPlayer(uint32_t uniqueId, uint8_t **person, uint8_t **player, char *name, size_t size) {
	PeopleIndex people;
	if (!database_loadPeople(&people)) {
		job_writeResult("error: cannot read the people list; is a save loaded?");
		return false;
	}
	*person = database_findPerson(&people, uniqueId);
	*player = *person ? database_playerRecord(*person) : NULL;
	name[0] = 0;
	if (*person && !database_personName(*person, name, size)) {
		(void)snprintf(name, size, "(unnamed, UID %u)", uniqueId);
	}
	database_freePeople(&people);
	const char *error = !*person   ? "no person with that unique ID"
											: !*player ? "that person is not a player"
																 : checkPlayer(*person, *player);
	if (error) {
		job_writeResult("error: %s (%u)", error, uniqueId);
	}
	return error == NULL;
}

// `directory`\<name>.json, the name being the person's common name, or else forename and surname,
// as lowercase hyphenated ASCII (slug.h); player-<unique ID> if no letters are left.
static bool defaultOutput(
	const uint8_t *person,
	uint32_t uniqueId,
	const wchar_t *directory,
	wchar_t *out,
	char *fileName,
	size_t fileNameSize
) {
	char name[PLAYER_NAME_LENGTH];
	char slug[PLAYER_NAME_LENGTH];
	if (!database_personCommonName(person, name, sizeof name) &&
			!database_personName(person, name, sizeof name)) {
		name[0] = 0;
	}
	if (!slug_fromName(name, slug, sizeof slug)) {
		(void)snprintf(slug, sizeof slug, "player-%u", uniqueId);
	}
	int written = snprintf(fileName, fileNameSize, "%s.json", slug);
	size_t length = wcslen(directory);
	bool separator = length && directory[length - 1] != L'\\' && directory[length - 1] != L'/';
	if (written < 0 || (size_t)written >= fileNameSize ||
			length + separator + (size_t)written >= HARNESS_PATH_LENGTH) {
		return false;
	}
	wmemcpy(out, directory, length);
	if (separator) {
		out[length++] = L'\\';
	}
	for (int i = 0; i <= written; ++i) { // the name is ASCII; <= copies its terminator
		out[length + (size_t)i] = (wchar_t)fileName[i];
	}
	return true;
}

void jobs_getDevelopment(const Job *job) {
	uint8_t *person = NULL;
	uint8_t *player = NULL;
	char name[PLAYER_NAME_LENGTH];
	if (!findPlayer(job->personId, &person, &player, name, sizeof name)) {
		return;
	}
	wchar_t path[HARNESS_PATH_LENGTH];
	char fileName[PLAYER_NAME_LENGTH + 8] = "";
	const wchar_t *output = job->output;
	if (!output[0]) {
		if (!defaultOutput(
					person, job->personId, job->outputDirectory, path, fileName, sizeof fileName
				)) {
			job_writeResult("error: cannot name the output file; give one");
			return;
		}
		output = path;
	}
	CurrentSchedule current = {.player = player};
	bool hasSchedule = freeze_run(readSchedule, &current) == FREEZE_OK && current.found;
	const char *note = hasSchedule ? "" : " (no training schedule found: not written)";
	log_message("get_development: player record at %p", (void *)player); // for memory tools
	if (!developmentFile_write(
				output, player, job->personId, name, hasSchedule ? current.sessions : NULL
			)) {
		job_writeResult("error: cannot write the output file");
	} else if (fileName[0]) {
		log_message("get_development: %s (%u) -> %s%s", name, job->personId, fileName, note);
		job_writeResult("ok: %s (%u), wrote %s%s", name, job->personId, fileName, note);
	} else {
		log_message("get_development: %s (%u)%s", name, job->personId, note);
		job_writeResult("ok: %s (%u)%s", name, job->personId, note);
	}
}

// Runs on freeze_run's worker thread: the lookup may create the week's schedule.
static void readSchedule(void *context) {
	CurrentSchedule *current = context;
	current->found = schedule_readCurrent(current->player, current->sessions);
}

// A zeroed Simulation, with room for the averages unless the job wants a row per seed.
static Simulation *newSimulation(const Job *job) {
	Simulation *sim = calloc(1, sizeof *sim);
	if (!sim) {
		return NULL;
	}
	sim->perSeed = job->perSeedBreakdown;
	if (!sim->perSeed &&
			!(sim->sums = calloc(((size_t)job->months + 1) * ROW_VALUES, sizeof *sim->sums))) {
		free(sim);
		return NULL;
	}
	return sim;
}

void jobs_develop(const Job *job) {
	uint8_t *person = NULL;
	uint8_t *player = NULL;
	char name[PLAYER_NAME_LENGTH];
	if (!findPlayer(job->personId, &person, &player, name, sizeof name)) {
		return;
	}
	char error[160] = "";
	DevelopmentValues *values = job->values ? malloc(sizeof *values) : NULL;
	if (job->values && (!values || !developmentFile_parse(job->values, values, error, sizeof error) ||
											 !developmentFile_fits(player, values, error, sizeof error))) {
		job_writeResult("error: %s", values ? error : "out of memory");
		free(values);
		return;
	}
	Simulation *sim = newSimulation(job);
	uint8_t *manager = NULL;
	if (!sim || !memory_readPointer(game_address(RVA_TRAINING_MANAGER), 0, &manager) || !manager ||
			!memory_read(manager + TRAINING_MANAGER_WEEK, &sim->week, sizeof sim->week) ||
			!memory_read(player, sim->original, sizeof sim->original) ||
			!daysToDevelopment(person, &sim->firstDays)) {
		job_writeResult("error: cannot read the training manager, the date or the player");
	} else if (!(sim->csv = _wfopen(job->output, L"wb")) || !writeHeader(sim->csv, sim->perSeed)) {
		job_writeResult("error: cannot write the output file");
	} else {
		sim->player = player;
		sim->seeds = job->seeds;
		sim->seedCount = job->seedCount;
		sim->months = job->months;
		sim->values = values;
		log_message(
			"develop: %s (%u), %zu seeds x %u months, next development in %u days, %zu values set",
			name,
			job->personId,
			job->seedCount,
			job->months,
			sim->firstDays,
			values ? values->count : 0
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
		} else if (sim->failure) {
			job_writeResult("error: %s", sim->failure);
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
	free(sim ? sim->sums : NULL);
	free(sim);
	free(values);
}

// Why this player cannot be simulated, or NULL.
static const char *checkPlayer(const uint8_t *person, const uint8_t *player) {
	if (memcmp(game_address(RVA_PLAYER_DEVELOP), developPrologue, sizeof developPrologue - 1) != 0) {
		return "this game build's development routine is not the one the harness knows";
	}
	// shortcut: db::ACTUAL_PLAYER only; a player who is also staff is a bigger object with the
	// person elsewhere, so the file's person fields would be missed. Support it once its layout is
	// known.
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
	if (!memory_readU32(person, PERSON_ROW_ID, &id) || !memory_readU16(date, 0, &field) ||
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
	sim->perSeed = true; // for comparing single seeds with the game's
	uint8_t *manager = NULL;
	bool ok = memory_readPointer(game_address(RVA_TRAINING_MANAGER), 0, &manager) && manager &&
						memory_read(manager + TRAINING_MANAGER_WEEK, &sim->week, sizeof sim->week) &&
						memory_read(player, sim->original, sizeof sim->original) &&
						(sim->csv = _wfopen(output, L"wb")) != NULL && writeHeader(sim->csv, true);
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
	memcpy(sim->startDate, game_address(RVA_CURRENT_DATE), sizeof sim->startDate);
	planInjuries(sim);
	if (sim->values) {
		developmentFile_save(sim->player, &sim->before);
		developmentFile_apply(sim->player, sim->values);
		memcpy(sim->original, sim->player, sizeof sim->original); // every seed starts from these
	}
	developmentFile_save(sim->player, &sim->start);
	size_t weeks = sim->values ? sim->values->scheduleWeeks : 0;
	if (weeks && !schedule_override(sim->values->schedule, weeks)) {
		sim->failure = "cannot replace the training schedule; see harness.log";
		sim->written = false;
	} else {
		sim->written = true;
	}
	for (size_t i = 0; i < sim->seedCount && sim->written; ++i) {
		sim->written = simulateSeed(sim, sim->seeds[i], sim->months);
	}
	if (weeks) {
		schedule_override(NULL, 0);
	}
	memcpy(game_address(RVA_CURRENT_DATE), sim->startDate, sizeof sim->startDate);
	restoreValues(sim);
	if (sim->values) {
		developmentFile_restore(sim->player, &sim->before);
	}
	if (sim->written && !sim->perSeed) {
		sim->written = writeAverages(sim);
	}
}

// Sets the file's plan for this month (from 0): the last month given repeats.
static void planMonth(uint8_t *player, const DevelopmentValues *values, unsigned month) {
	size_t planned = values->matchMonths;
	if (planned) {
		// The routine moves this to last_total and bands it (knowledge-base section 20).
		// shortcut: the match count isn't planned, as the band reads only the total; plan it if
		// another use of the count turns up.
		uint16_t points = values->matchPoints[month < planned ? month : planned - 1];
		memcpy(player + PLAYER_DEVELOPMENT_PERIOD, &points, sizeof points);
	}
	size_t focused = values->focusMonths;
	uint8_t *record = NULL;
	// The weekly and monthly routines read the focus (knowledge-base section 15); the record
	// exists, made by developmentFile_apply if the player had none.
	if (focused && memory_readPointer(player, PLAYER_FOCUS_RECORD, &record)) {
		record[FOCUS_ID] = values->focus[month < focused ? month : focused - 1];
	}
}

static bool simulateSeed(Simulation *sim, uint32_t seed, unsigned months) {
	PlayerDevelopFn develop = (PlayerDevelopFn)game_function(RVA_PLAYER_DEVELOP);
	restoreValues(sim);
	memcpy(game_address(RVA_CURRENT_DATE), sim->startDate, sizeof sim->startDate);
	schedule_restart();   // a file's schedule starts from its first week for every seed
	rng_seedThread(seed); // the routine draws from this thread's PCG
	if (!recordRow(sim, seed, 0)) {
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
		// The routine dates the weeks it trains back from the game's date, and ages the player by
		// it: move it to this month's development day, or it trains the weeks before the start.
		addDays(game_address(RVA_CURRENT_DATE), (int)days);
		if (sim->values) {
			planMonth(sim->player, sim->values, month);
		}
		// At the real date the game's list is current; once the date moves on, it would have
		// dropped the injuries healed by then.
		InjuryList injuries = {0};
		swapInjuries(sim, days != 0, &injuries);
		develop(sim->player, false, 1);
		if (!restoreInjuries(&injuries)) {
			sim->failure = "the development routine changed the injury list; reload the save";
			return false;
		}
		if (!recordRow(sim, seed, month + 1)) {
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
	// The development file's fields too, training happiness among them: the routine updates it.
	developmentFile_restore(sim->player, &sim->start);
}

// Moves a date (u16 day of year from 1 | other bits, u16 year) by `days`, keeping the bits above
// the day.
static void addDays(uint8_t *date, int days) {
	uint16_t field = memory_loadU16(date);
	int year = memory_loadU16(date + 2);
	int day = (field & DAY_MASK) + days;
	while (day < 1) {
		--year;
		day += isLeap(year) ? 366 : 365;
	}
	while (day > (isLeap(year) ? 366 : 365)) {
		day -= isLeap(year) ? 366 : 365;
		++year;
	}
	field = (uint16_t)((field & ~DAY_MASK) | day);
	uint16_t year16 = (uint16_t)year;
	memcpy(date, &field, sizeof field);
	memcpy(date + 2, &year16, sizeof year16);
}

// Days since 1 January of year 1 for a date as addDays stores it.
static int dayNumber(const uint8_t *date) {
	int year = memory_loadU16(date + 2) - 1;
	return 365 * year + year / 4 - year / 100 + year / 400 + (memory_loadU16(date) & DAY_MASK);
}

// Whether the game would have dropped `injury` by `today`: sub_142741D60 counts days from its end
// in either direction, so a healed one left in the list zeroes every rise (knowledge-base section
// 19, v1383). Injuries timed in weeks, or in the form marked by bit 15, are kept.
static bool isHealed(const uint8_t *injury, int today) {
	uint16_t length = 0;
	int8_t weeks = 0;
	uint8_t start[4];
	return memory_readU16(injury, INJURY_DAYS, &length) &&
				 memory_read(injury + INJURY_WEEKS, &weeks, 1) &&
				 memory_read(injury + INJURY_START, start, 4) && weeks < 0 && !(length & 0x8000) &&
				 today >= dayNumber(start) + length;
}

// The file's injury windows as injuries starting `start` days after the run's first date. Only the
// fields the development and weekly routines read are set (sub_142741D60, sub_1421781F0,
// sub_1438F1890); the pointers stay NULL, as nothing those routines call reads them.
static void planInjuries(Simulation *sim) {
	size_t windows = sim->values ? sim->values->injuryWindows : 0;
	for (size_t i = 0; i < windows; ++i) {
		uint8_t *injury = sim->planned[i];
		memcpy(injury + INJURY_START, sim->startDate, 4);
		addDays(injury + INJURY_START, sim->values->injuryStart[i]);
		memset(injury + INJURY_START + 4, 0xFF, 4); // as in the game's injuries
		memcpy(injury + INJURY_DAYS, &sim->values->injuryDays[i], 2);
		injury[INJURY_TYPE] = 1;
		injury[INJURY_WEEKS] = 0xFF; // timed in days
	}
}

// Points the player's injury list at develop's own for the routine's call when it differs from the
// game's: without the injuries healed by today if `dropHealed`, and with the planned ones under
// way today. Leaves `list` empty, and the game's list in place, otherwise.
static void swapInjuries(Simulation *sim, bool dropHealed, InjuryList *list) {
	// memory_read, not memory_readPointer: no list, or an empty one (NULL begin and end), is common.
	uint8_t *original = NULL;
	uint8_t *bounds[2] = {NULL, NULL};
	if (!memory_read(sim->player + PLAYER_INJURIES, &original, sizeof original) ||
			(original && (!memory_read(original, bounds, sizeof bounds) || bounds[1] < bounds[0] ||
										 (size_t)(bounds[1] - bounds[0]) > INJURY_LIMIT * sizeof(void *)))) {
		return; // shortcut: a list longer than INJURY_LIMIT is left as it is
	}
	uint8_t *first = bounds[0];
	uint8_t *last = bounds[1];
	int today = dayNumber(game_address(RVA_CURRENT_DATE));
	size_t count = 0;
	bool changed = false;
	for (uint8_t **entry = (uint8_t **)first; entry < (uint8_t **)last; ++entry) {
		if (dropHealed && isHealed(*entry, today)) {
			changed = true;
		} else {
			list->entries[count++] = *entry;
		}
	}
	size_t windows = sim->values ? sim->values->injuryWindows : 0;
	for (size_t i = 0; i < windows; ++i) {
		uint8_t *injury = sim->planned[i];
		if (dayNumber(injury + INJURY_START) <= today && !isHealed(injury, today)) {
			list->entries[count++] = injury;
			changed = true;
		}
	}
	if (!changed) {
		return;
	}
	list->slot = (uint8_t **)(sim->player + PLAYER_INJURIES);
	list->original = original;
	list->vector[0] = (uint8_t *)list->entries;
	list->vector[1] = list->vector[2] = (uint8_t *)(list->entries + count);
	*list->slot = (uint8_t *)list->vector;
}

// Puts the game's list back. False if the routine replaced or resized develop's list meanwhile;
// the game's is put back regardless, as develop's is gone with the caller's stack.
static bool restoreInjuries(const InjuryList *list) {
	if (!list->slot) {
		return true;
	}
	bool intact = *list->slot == (uint8_t *)list->vector &&
								list->vector[0] == (uint8_t *)list->entries && list->vector[1] == list->vector[2];
	*list->slot = list->original;
	return intact;
}

// Per-seed rows start "seed,month"; averaged rows "month,seeds" (how many seeds each averages).
static bool writeHeader(FILE *csv, bool perSeed) {
	fputs(perSeed ? "seed,month" : "month,seeds", csv);
	fputs(",current_ability,potential_ability,weight,height", csv);
	for (size_t i = 0; i < TRAINING_CATEGORY_COUNT; ++i) {
		fprintf(csv, ",progress_%s", labels_categoryName(i));
	}
	for (size_t i = 0; i < PLAYER_RATING_COUNT; ++i) {
		fprintf(csv, ",%s", labels_ratingName(i));
	}
	return fputc('\n', csv) != EOF;
}

// A row's values, in writeHeader's order after its first two columns.
static void rowValues(const uint8_t *player, int out[ROW_VALUES]) {
	size_t n = 0;
	out[n++] = memory_loadU16(player + PLAYER_CURRENT_ABILITY);
	out[n++] = memory_loadU16(player + PLAYER_POTENTIAL_ABILITY);
	out[n++] = player[PLAYER_WEIGHT];
	out[n++] = memory_loadU16(player + PLAYER_HEIGHT);
	for (size_t i = 0; i < TRAINING_CATEGORY_COUNT; ++i) {
		int progress = player[PLAYER_TRAINING_PROGRESS + i]; // stored as s8
		out[n++] = progress > INT8_MAX ? progress - 256 : progress;
	}
	for (size_t i = 0; i < PLAYER_RATING_COUNT; ++i) {
		out[n++] = player[PLAYER_RATINGS + i];
	}
}

// Writes the player's values for this seed and month, or adds them to the month's sums.
static bool recordRow(Simulation *sim, uint32_t seed, unsigned month) {
	int values[ROW_VALUES];
	rowValues(sim->player, values);
	if (!sim->perSeed) {
		for (size_t i = 0; i < ROW_VALUES; ++i) {
			sim->sums[(size_t)month * ROW_VALUES + i] += values[i];
		}
		return true;
	}
	fprintf(sim->csv, "%u,%u", seed, month);
	for (size_t i = 0; i < ROW_VALUES; ++i) {
		fprintf(sim->csv, ",%d", values[i]);
	}
	return fputc('\n', sim->csv) != EOF;
}

// One row per month: every value averaged over the seeds.
static bool writeAverages(Simulation *sim) {
	for (unsigned month = 0; month <= sim->months; ++month) {
		fprintf(sim->csv, "%u,%zu", month, sim->seedCount);
		for (size_t i = 0; i < ROW_VALUES; ++i) {
			double sum = sim->sums[(size_t)month * ROW_VALUES + i];
			fprintf(sim->csv, ",%.3f", sum / (double)sim->seedCount);
		}
		if (fputc('\n', sim->csv) == EOF) {
			return false;
		}
	}
	return true;
}
