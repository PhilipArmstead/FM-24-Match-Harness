// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "schedule.h"

#include "log.h"
#include "memory.h"
#include "session_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	CALL_LENGTH = 5, // E8 rel32
	CALL_OPCODE = 0xE8,
	SCHEDULE_SIZE = SCHEDULE_DAYS * SCHEDULE_DAY_SIZE,
	TRAMPOLINE_SIZE = 16,
	NO_DAY_INFO = 0xFF, // what the game's schedules hold in each day's first 4 bytes
	DAY_MASK = 0x1FF,   // a date's day of the year, from 1, in its low u16
	LINE_SIZE = 1024,
};

static uint8_t (*weekSchedules)[SCHEDULE_SIZE]; // the override, in the game's layout
static size_t weekCount;
static size_t nextWeek;
static uint8_t *trampoline; // jmp [rip+0] to onLookup, within rel32 reach of the call site
static uint32_t lastLogged; // year << 16 | day of the latest week logged

bool schedule_readCurrent(uint8_t *player, uint16_t out[SCHEDULE_SESSIONS]) {
	uint8_t *manager = NULL;
	uint32_t date = 0;
	if (!memory_readPointer(game_address(RVA_TRAINING_MANAGER), 0, &manager) || !manager ||
			!memory_read(game_address(RVA_CURRENT_DATE), &date, sizeof date)) {
		return false;
	}
	uint8_t *team = ((TrainingTeamFn)game_function(RVA_TRAINING_TEAM))(manager, player);
	uint8_t *week =
		team ? ((WeekScheduleFn)game_function(RVA_WEEK_SCHEDULE))(manager, team, &date) : NULL;
	if (!week) {
		return false;
	}
	for (size_t day = 0; day < SCHEDULE_DAYS; ++day) {
		for (size_t slot = 0; slot < SCHEDULE_SLOTS; ++slot) {
			out[day * SCHEDULE_SLOTS + slot] =
				memory_loadU16(week + day * SCHEDULE_DAY_SIZE + SCHEDULE_SESSIONS_AT + 2 * slot);
		}
	}
	return true;
}

// Logs each week of the game's own schedules once per hook: later seeds repeat the dates.
static void logWeek(const uint32_t *weekStart, const uint8_t *week) {
	unsigned year = *weekStart >> 16;
	unsigned day = *weekStart & DAY_MASK;
	uint32_t key = year << 16 | day;
	if (key <= lastLogged) {
		return;
	}
	lastLogged = key;
	static const uint8_t monthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	unsigned month = 0;
	unsigned date = day;
	for (; month < 11; ++month) {
		unsigned length = monthDays[month] + (unsigned)(month == 1 && leap);
		if (date <= length) {
			break;
		}
		date -= length;
	}
	if (!week) {
		log_message("schedule: week from %04u-%02u-%02u: none", year, month + 1, date);
		return;
	}
	char line[LINE_SIZE];
	size_t used = 0;
	for (size_t i = 0; i < SCHEDULE_SESSIONS && used < sizeof line; ++i) {
		uint16_t id = memory_loadU16(
			week + i / SCHEDULE_SLOTS * SCHEDULE_DAY_SIZE + SCHEDULE_SESSIONS_AT +
			2 * (i % SCHEDULE_SLOTS)
		);
		const char *name = session_name(id);
		const char *separator = !i ? "" : i % SCHEDULE_SLOTS ? ", " : " | ";
		used += (size_t)(name ? snprintf(line + used, sizeof line - used, "%s%s", separator, name)
													: snprintf(line + used, sizeof line - used, "%s%u", separator, id));
	}
	log_message(
		"schedule: week from %04u-%02u-%02u (%p): %s", year, month + 1, date, (void *)week, line
	);
}

// Stands in for RVA_WEEK_SCHEDULE at the weekly routine's call while the call is hooked.
static uint8_t *onLookup(uint8_t *manager, uint8_t *team, const uint32_t *weekStart) {
	if (weekCount) {
		return weekSchedules[nextWeek++ % weekCount];
	}
	uint8_t *week = ((WeekScheduleFn)game_function(RVA_WEEK_SCHEDULE))(manager, team, weekStart);
	logWeek(weekStart, week);
	return week;
}

// Points the call at `target` if it calls the lookup or the trampoline now.
static bool redirect(const uint8_t *target) {
	uint8_t *call = game_address(RVA_WEEK_SCHEDULE_CALL);
	int32_t displacement = 0;
	memcpy(&displacement, call + 1, sizeof displacement);
	const uint8_t *current = call + CALL_LENGTH + displacement;
	if (call[0] != CALL_OPCODE ||
			(current != game_address(RVA_WEEK_SCHEDULE) && (!trampoline || current != trampoline))) {
		log_message("schedule: fm.exe+0x%x is not a call of the lookup", RVA_WEEK_SCHEDULE_CALL);
		return false;
	}
	displacement = (int32_t)(target - (call + CALL_LENGTH));
	DWORD protection = 0;
	if (!VirtualProtect(call + 1, sizeof displacement, PAGE_EXECUTE_READWRITE, &protection)) {
		log_message("schedule: cannot unprotect the call (Windows error %lu)", GetLastError());
		return false;
	}
	memcpy(call + 1, &displacement, sizeof displacement);
	VirtualProtect(call + 1, sizeof displacement, protection, &protection);
	FlushInstructionCache(GetCurrentProcess(), call, CALL_LENGTH);
	return true;
}

static void clearWeeks(void) {
	free(weekSchedules);
	weekSchedules = NULL;
	weekCount = 0;
}

bool schedule_override(const uint16_t (*weeks)[SCHEDULE_SESSIONS], size_t count) {
	if (!trampoline) {
		uint8_t *memory = game_allocateNear(TRAMPOLINE_SIZE);
		if (!memory) {
			return false;
		}
		static const uint8_t jump[6] = {0xFF, 0x25, 0, 0, 0, 0}; // jmp [rip+0]
		uint8_t *(*hook)(uint8_t *, uint8_t *, const uint32_t *) = onLookup;
		memcpy(memory, jump, sizeof jump);
		memcpy(memory + sizeof jump, &hook, sizeof hook);
		FlushInstructionCache(GetCurrentProcess(), memory, TRAMPOLINE_SIZE);
		trampoline = memory;
	}
	uint8_t (*schedules)[SCHEDULE_SIZE] = NULL;
	if (count) {
		schedules = malloc(count * sizeof *schedules);
		if (!schedules) {
			return false;
		}
		memset(schedules, NO_DAY_INFO, count * sizeof *schedules);
		for (size_t week = 0; week < count; ++week) {
			for (size_t i = 0; i < SCHEDULE_SESSIONS; ++i) {
				size_t at =
					i / SCHEDULE_SLOTS * SCHEDULE_DAY_SIZE + SCHEDULE_SESSIONS_AT + 2 * (i % SCHEDULE_SLOTS);
				memcpy(schedules[week] + at, &weeks[week][i], 2);
			}
		}
	}
	clearWeeks();
	weekSchedules = schedules;
	weekCount = count;
	nextWeek = 0;
	lastLogged = 0;
	if (!redirect(trampoline)) {
		clearWeeks();
		return false;
	}
	return true;
}

bool schedule_release(void) {
	bool ok = redirect(game_address(RVA_WEEK_SCHEDULE));
	clearWeeks();
	return ok;
}

void schedule_restart(void) {
	nextWeek = 0;
}
