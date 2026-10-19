// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "player_inject.h"

#include "match_config.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>


enum { MAX_PLAYER_FIELDS = 64, MAX_UID_COPIES = 4, FIELD_NAME_LENGTH = 64, PLAYER_TEXT = 256 };

// One value of the player file: its name (a player block's field suffix) and its token.
typedef struct PlayerField {
	char name[FIELD_NAME_LENGTH];
	ConfigValueKind kind;
	size_t length; // encoded bytes
	size_t offset; // the value token in the player file
	size_t tokenLength;
	bool used;
} PlayerField;

typedef struct PlayerFile {
	PlayerField fields[MAX_PLAYER_FIELDS];
	size_t count;
	size_t nameOffset; // the "player" annotation token of the person_uid field
	size_t nameLength;
	uint32_t uniqueId;
	bool hasUniqueId;
	char error[160];
} PlayerFile;

// A replacement of config[offset, offset + length) by `length` bytes of the player file.
typedef struct Patch {
	size_t offset;
	size_t length;
	size_t from;
	size_t fromLength;
} Patch;

typedef struct Injector {
	const char *block;
	size_t blockLength;
	PlayerFile *file;
	bool allowDuplicate;
	// One per value, plus the annotation and the unnamed copies of the unique ID.
	Patch patches[MAX_PLAYER_FIELDS + 1 + MAX_UID_COPIES];
	size_t patchCount;
	bool foundBlock;
	bool inside; // the field being visited belongs to the block (unnamed fields follow the last name)
	bool hasOldUid;                  // the block's person_uid has been seen
	uint32_t oldUid;                 // ... and its value before the injection
	char currentPlayer[PLAYER_TEXT]; // the "player" of the block being visited
	PlayerInjection *result;
	char error[PLAYER_TEXT + 160];
} Injector;

static uint32_t loadU32(const uint8_t *bytes);
static void readPlayerField(void *context, const ConfigField *field);
static void replaceUidCopy(Injector *injector, const ConfigField *field);
static PlayerField *findField(PlayerFile *file, const char *name);
static void
addPatch(Injector *injector, size_t offset, size_t length, size_t from, size_t fromLength);
static void checkCollision(Injector *injector, const ConfigField *field);
static void injectField(void *context, const ConfigField *field);
static void listMissing(PlayerFile *file, PlayerInjection *result);

bool playerInject_apply(
	const char *config,
	size_t configLength,
	const char *player,
	size_t playerLength,
	const char *block,
	bool allowDuplicate,
	ByteBuffer *out,
	PlayerInjection *result,
	char *error,
	size_t errorSize
) {
	buffer_clear(out);
	*result = (PlayerInjection){0};
	PlayerFile file = {0};
	ConfigVisitor fileVisitor = {.context = &file, .field = readPlayerField};
	char visitError[256] = "";
	if (!matchConfig_visitFormat(
				player, playerLength, "fm24-player/1", &fileVisitor, visitError, sizeof visitError
			)) {
		(void)snprintf(error, errorSize, "player file: %s", visitError);
		return false;
	}
	if (file.error[0] || !file.hasUniqueId) {
		(void)snprintf(
			error, errorSize, "player file: %s", file.error[0] ? file.error : "missing person_uid"
		);
		return false;
	}
	Injector injector = {.block = block,
		.blockLength = strlen(block),
		.file = &file,
		.allowDuplicate = allowDuplicate,
		.result = result};
	ConfigVisitor visitor = {.context = &injector, .field = injectField};
	if (!matchConfig_visit(config, configLength, &visitor, visitError, sizeof visitError)) {
		(void)snprintf(error, errorSize, "fixture: %s", visitError);
		return false;
	}
	if (!injector.error[0] && !injector.foundBlock) {
		(void)snprintf(injector.error, sizeof injector.error, "the fixture has no %s", block);
	}
	if (injector.error[0]) {
		(void)snprintf(error, errorSize, "%s", injector.error);
		return false;
	}
	size_t copied = 0;
	bool ok = true;
	for (size_t i = 0; ok && i < injector.patchCount; ++i) {
		const Patch *patch = &injector.patches[i];
		ok = buffer_append(out, config + copied, patch->offset - copied) &&
				 buffer_append(out, player + patch->from, patch->fromLength);
		copied = patch->offset + patch->length;
	}
	if (!ok || !buffer_append(out, config + copied, configLength - copied)) {
		buffer_clear(out);
		(void)snprintf(error, errorSize, "out of memory");
		return false;
	}
	listMissing(&file, result);
	return true;
}

static uint32_t loadU32(const uint8_t *bytes) {
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
				 (uint32_t)bytes[3] << 24;
}

static void readPlayerField(void *context, const ConfigField *field) {
	PlayerFile *file = context;
	if (file->error[0]) {
		return;
	}
	if (!field->name[0] || strlen(field->name) >= FIELD_NAME_LENGTH || findField(file, field->name)) {
		(void)snprintf(
			file->error, sizeof file->error, "field %zu: missing, long or repeated name", file->count
		);
		return;
	}
	if (file->count == MAX_PLAYER_FIELDS) {
		(void)snprintf(file->error, sizeof file->error, "more than %d fields", MAX_PLAYER_FIELDS);
		return;
	}
	PlayerField *entry = &file->fields[file->count++];
	*entry = (PlayerField){
		.kind = field->kind,
		.length = field->length,
		.offset = field->valueOffset,
		.tokenLength = field->valueLength,
	};
	memcpy(entry->name, field->name, strlen(field->name) + 1);
	if (strcmp(field->name, "person_uid") == 0) {
		if (field->kind != CONFIG_U32) {
			(void)snprintf(file->error, sizeof file->error, "person_uid must be a u32");
			return;
		}
		file->uniqueId = loadU32(field->bytes);
		file->hasUniqueId = true;
		file->nameOffset = field->playerOffset;
		file->nameLength = field->playerLength;
	}
}

static PlayerField *findField(PlayerFile *file, const char *name) {
	for (size_t i = 0; i < file->count; ++i) {
		if (strcmp(file->fields[i].name, name) == 0) {
			return &file->fields[i];
		}
	}
	return NULL;
}

static void
addPatch(Injector *injector, size_t offset, size_t length, size_t from, size_t fromLength) {
	if (injector->patchCount == sizeof injector->patches / sizeof injector->patches[0]) {
		(void)snprintf(injector->error, sizeof injector->error, "%s is named twice", injector->block);
		return;
	}
	injector->patches[injector->patchCount++] = (Patch){offset, length, from, fromLength};
}

// Another player block already holding the injected player's unique ID would give the match the
// same person twice (the engine plays it, but only allowDuplicate asks for it).
static void checkCollision(Injector *injector, const ConfigField *field) {
	static const char suffix[] = ".person_uid";
	size_t length = strlen(field->name);
	if (injector->allowDuplicate || field->kind != CONFIG_U32 || length < sizeof suffix ||
			strcmp(field->name + length - (sizeof suffix - 1), suffix) != 0 ||
			!strstr(field->name, ".player[")) {
		return;
	}
	if (loadU32(field->bytes) == injector->file->uniqueId) {
		(void)snprintf(
			injector->error,
			sizeof injector->error,
			"%s (%s) is already this player; use --allow-duplicate to inject anyway",
			field->name,
			injector->currentPlayer
		);
	}
}

static void injectField(void *context, const ConfigField *field) {
	Injector *injector = context;
	if (injector->error[0]) {
		return;
	}
	if (field->player[0]) {
		(void)snprintf(injector->currentPlayer, sizeof injector->currentPlayer, "%s", field->player);
	}
	const char *name = field->name;
	if (name[0]) {
		injector->inside = strncmp(name, injector->block, injector->blockLength) == 0 &&
											 (name[injector->blockLength] == 0 || name[injector->blockLength] == '.');
	}
	if (!injector->inside) {
		checkCollision(injector, field);
		return;
	}
	if (!name[0]) {
		replaceUidCopy(injector, field);
		return;
	}
	PlayerFile *file = injector->file;
	if (name[injector->blockLength] == 0) {
		injector->foundBlock = true;
		if (field->playerLength && file->nameLength) {
			addPatch(
				injector, field->playerOffset, field->playerLength, file->nameOffset, file->nameLength
			);
		}
		return;
	}
	const char *suffix = name + injector->blockLength + 1;
	if (strcmp(suffix, "person_uid") == 0 && field->kind == CONFIG_U32) {
		injector->oldUid = loadU32(field->bytes);
		injector->hasOldUid = true;
	}
	PlayerField *value = findField(file, suffix);
	if (!value) {
		return;
	}
	if (value->kind != field->kind || value->length != field->length) {
		(void)snprintf(
			injector->error, sizeof injector->error, "%s has another type in the player file", name
		);
		return;
	}
	value->used = true;
	++injector->result->replaced;
	addPatch(injector, field->valueOffset, field->valueLength, value->offset, value->tokenLength);
}

// The person record in a player block holds unnamed copies of the unique ID after the named one;
// the match identifies the player by one of them (docs/FIXTURE.md), so they change with it.
static void replaceUidCopy(Injector *injector, const ConfigField *field) {
	PlayerField *uid = findField(injector->file, "person_uid");
	if (!injector->hasOldUid || !uid || field->kind != CONFIG_U32 ||
			loadU32(field->bytes) != injector->oldUid) {
		return;
	}
	if (injector->result->uidCopies == MAX_UID_COPIES) {
		(void)snprintf(
			injector->error,
			sizeof injector->error,
			"%s has more than %d copies of its unique ID",
			injector->block,
			MAX_UID_COPIES
		);
		return;
	}
	++injector->result->uidCopies;
	addPatch(injector, field->valueOffset, field->valueLength, uid->offset, uid->tokenLength);
}

static void listMissing(PlayerFile *file, PlayerInjection *result) {
	size_t length = 0;
	for (size_t i = 0; i < file->count; ++i) {
		const PlayerField *field = &file->fields[i];
		if (field->used) {
			continue;
		}
		++result->missing;
		int written = snprintf(
			result->missingNames + length,
			sizeof result->missingNames - length,
			"%s%s",
			length ? ", " : "",
			field->name
		);
		if (written > 0 && (size_t)written < sizeof result->missingNames - length) {
			length += (size_t)written;
		}
	}
}
