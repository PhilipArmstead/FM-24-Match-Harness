// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "schedule.h"

#include "log.h"
#include "memory.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	CALL_LENGTH = 5, // E8 rel32
	CALL_OPCODE = 0xE8,
	SCHEDULE_SIZE = SCHEDULE_DAYS * SCHEDULE_DAY_SIZE,
	TRAMPOLINE_SIZE = 16,
	NO_DAY_INFO = 0xFF, // what the game's schedules hold in each day's first 4 bytes
};

static uint8_t (*weekSchedules)[SCHEDULE_SIZE]; // the override, in the game's layout
static size_t weekCount;
static size_t nextWeek;
static uint8_t *trampoline; // jmp [rip+0] to onLookup, within rel32 reach of the call site

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

// Stands in for RVA_WEEK_SCHEDULE at the weekly routine's call while an override is set.
static uint8_t *onLookup(const uint8_t *manager, const uint8_t *team, const uint32_t *weekStart) {
	(void)manager;
	(void)team;
	(void)weekStart;
	return weekSchedules[nextWeek++ % weekCount];
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

bool schedule_override(const uint16_t (*weeks)[SCHEDULE_SESSIONS], size_t count) {
	if (!count) {
		bool ok = !weekCount || redirect(game_address(RVA_WEEK_SCHEDULE));
		free(weekSchedules);
		weekSchedules = NULL;
		weekCount = 0;
		return ok;
	}
	if (!trampoline) {
		uint8_t *memory = game_allocateNear(TRAMPOLINE_SIZE);
		if (!memory) {
			return false;
		}
		static const uint8_t jump[6] = {0xFF, 0x25, 0, 0, 0, 0}; // jmp [rip+0]
		uint8_t *(*hook)(const uint8_t *, const uint8_t *, const uint32_t *) = onLookup;
		memcpy(memory, jump, sizeof jump);
		memcpy(memory + sizeof jump, &hook, sizeof hook);
		FlushInstructionCache(GetCurrentProcess(), memory, TRAMPOLINE_SIZE);
		trampoline = memory;
	}
	uint8_t (*schedules)[SCHEDULE_SIZE] = malloc(count * sizeof *schedules);
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
	free(weekSchedules);
	weekSchedules = schedules;
	weekCount = count;
	nextWeek = 0;
	if (!redirect(trampoline)) {
		schedule_override(NULL, 0);
		return false;
	}
	return true;
}

void schedule_restart(void) {
	nextWeek = 0;
}
