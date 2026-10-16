// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config_writer.h"

#include "game.h"
#include "labels.h"
#include "match.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum {
	MAX_MERGED_STRING = 4096,
	STAFF_ATTRIBUTES_MEMBER = 0x60, // MATCH_STAFF offset of the staff attributes (SITE_STAFF_ATTRIBUTES)
};

static void writeEscaped(FILE *file, const uint8_t *text, size_t length);
static void writeEscapedString(FILE *file, const char *text);
static size_t sequenceLength(uint8_t lead);
static bool isText(const uint8_t *text, size_t length);
static bool isStringPair(const Serialised *serialised, size_t i);
static void writeRatings(FILE *file, const uint8_t *value);
static void writeStaffAttributes(
	FILE *file,
	const TraceRecord *record,
	const FieldLabel *label,
	const uint8_t *value,
	bool full
);
static void writeScalar(FILE *file, const uint8_t *value, uint32_t length);
static void writeFlags(FILE *file, const FieldLabel *label, const uint8_t *value, uint32_t length);
static size_t writeField(
	FILE *file,
	const Serialised *serialised,
	const FieldLabel *labels,
	size_t i,
	size_t seedOffset,
	bool full
);
static void writeHeader(
	FILE *file,
	const Serialised *serialised,
	const char *source,
	uint32_t awayTeam,
	bool full
);

bool configWriter_write(
	const wchar_t *path,
	const Serialised *serialised,
	const char *source,
	const PeopleIndex *people,
	uint32_t awayTeam,
	bool full
) {
	FieldLabel *labels = labels_compute(serialised, people);
	if (!labels) {
		return false;
	}
	FILE *file = _wfopen(path, L"wb");
	if (!file) {
		free(labels);
		return false;
	}
	size_t seedOffset = SIZE_MAX;
	(void)match_seedOffset(serialised->bytes.data, serialised->bytes.length, &seedOffset);
	writeHeader(file, serialised, source, awayTeam, full);
	for (size_t i = 0; i < serialised->recordCount;) {
		i += writeField(file, serialised, labels, i, seedOffset, full);
	}
	fprintf(file, "  ]\n}\n");
	bool ok = !ferror(file);
	free(labels);
	return fclose(file) == 0 && ok;
}

static void writeEscaped(FILE *file, const uint8_t *text, size_t length) {
	fputc('"', file);
	for (size_t i = 0; i < length; ++i) {
		uint8_t c = text[i];
		if (c == '"' || c == '\\') {
			fputc('\\', file);
			fputc(c, file);
		} else if (c < 0x20) {
			fprintf(file, "\\u%04x", c);
		} else {
			fputc(c, file);
		}
	}
	fputc('"', file);
}

static void writeEscapedString(FILE *file, const char *text) {
	writeEscaped(file, (const uint8_t *)text, strlen(text));
}

// Length of the UTF-8 sequence started by `lead` (0 if `lead` cannot start one).
static size_t sequenceLength(uint8_t lead) {
	if (lead < 0x80) {
		return 1;
	}
	if (lead >= 0xC2 && lead <= 0xDF) {
		return 2;
	}
	if ((lead >> 4) == 14) {
		return 3;
	}
	return lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
}

// Valid UTF-8 without control characters: safe to show as a JSON string.
static bool isText(const uint8_t *text, size_t length) {
	for (size_t i = 0; i < length;) {
		size_t sequence = sequenceLength(text[i]);
		if (sequence == 0 || text[i] < 0x20 || text[i] == 0x7F || i + sequence > length) {
			return false;
		}
		for (size_t k = 1; k < sequence; ++k) {
			if ((text[i + k] & 0xC0) != 0x80) {
				return false;
			}
			if (sequence >= 3 &&
					((text[i] == 0xE0 && text[i + 1] < 0xA0) || (text[i] == 0xED && text[i + 1] >= 0xA0) ||
						(text[i] == 0xF0 && text[i + 1] < 0x90) || (text[i] == 0xF4 && text[i + 1] >= 0x90))) {
				return false;
			}
		}
		i += sequence;
	}
	return true;
}

// A u32 length immediately followed by a write of exactly that many text bytes is a string.
static bool isStringPair(const Serialised *serialised, size_t i) {
	const TraceRecord *length = &serialised->records[i];
	if (length->length != 4 || i + 1 >= serialised->recordCount) {
		return false;
	}
	const TraceRecord *text = &serialised->records[i + 1];
	uint32_t size = memory_loadU32(serialised->bytes.data + length->offset);
	return size > 0 && size <= MAX_MERGED_STRING && text->length == size &&
				 text->offset == length->offset + 4 && isText(serialised->bytes.data + text->offset, size);
}

static void writeRatings(FILE *file, const uint8_t *value) {
	fprintf(file, "\"u8map\": {");
	for (size_t k = 0; k < PLAYER_RATING_COUNT; ++k) {
		fprintf(file, "%s\"%s\": %u", k ? ", " : "", labels_ratingName(k), value[k]);
	}
	fputc('}', file);
}

// Writes a staff attributes record as one named u8 field per byte; the encoder writes them back
// as consecutive bytes, so the record is unchanged. Leaves the last field's object open.
static void writeStaffAttributes(
	FILE *file,
	const TraceRecord *record,
	const FieldLabel *label,
	const uint8_t *value,
	bool full
) {
	for (size_t k = 0; k < STAFF_ATTRIBUTE_COUNT; ++k) {
		if (k) {
			fprintf(file, "},\n    {");
		}
		if (full) {
			fprintf(file, "\"site\": \"%x/%x\", \"p\": %u, ", record->site, record->caller, record->path);
		}
		char name[LABEL_LENGTH + 48];
		(void)snprintf(name, sizeof name, "%s.%s", label->name, labels_staffAttributeName(k));
		fprintf(file, "\"name\": ");
		writeEscapedString(file, name);
		if (full) {
			fprintf(file, ", \"member\": \"MATCH_STAFF+0x%zx\"", STAFF_ATTRIBUTES_MEMBER + k);
		}
		fprintf(file, ", \"u8\": %u", value[k]);
	}
}

static void writeScalar(FILE *file, const uint8_t *value, uint32_t length) {
	switch (length) {
		case 1: fprintf(file, "\"u8\": %u", value[0]); break;
		case 2: fprintf(file, "\"u16\": %u", memory_loadU16(value)); break;
		case 4: fprintf(file, "\"u32\": %u", memory_loadU32(value)); break;
		case 8: fprintf(file, "\"u64\": %llu", (unsigned long long)memory_loadU64(value)); break;
		default:
			fprintf(file, "\"hex\": \"");
			for (uint32_t k = 0; k < length; ++k) {
				fprintf(file, "%02x", value[k]);
			}
			fputc('"', file);
			break;
	}
}

// Writes "flags": the names of the set bits of a tactic bit set ("bit N" if unknown), if any.
static void writeFlags(FILE *file, const FieldLabel *label, const uint8_t *value, uint32_t length) {
	if (label->flags == FLAGS_NONE || (length != 4 && length != 8)) {
		return;
	}
	uint64_t bits = length == 4 ? memory_loadU32(value) : memory_loadU64(value);
	fprintf(file, "\"flags\": [");
	const char *separator = "";
	for (unsigned bit = 0; bit < length * 8; ++bit) {
		if (!(bits >> bit & 1)) {
			continue;
		}
		const char *name = labels_flagName(label, bit);
		fprintf(file, "%s", separator);
		if (name) {
			writeEscapedString(file, name);
		} else {
			fprintf(file, "\"bit %u\"", bit);
		}
		separator = ", ";
	}
	fprintf(file, "], ");
}

// Writes record i (and i + 1 when they form a string); returns how many records it consumed.
static size_t writeField(
	FILE *file,
	const Serialised *serialised,
	const FieldLabel *labels,
	size_t i,
	size_t seedOffset,
	bool full
) {
	const TraceRecord *record = &serialised->records[i];
	const uint8_t *value = serialised->bytes.data + record->offset;
	const FieldLabel *label = &labels[i];
	size_t consumed = 1;
	fprintf(file, "    {");
	if (label->staffAttributes) {
		writeStaffAttributes(file, record, label, value, full);
		fprintf(file, "}%s\n", i + 1 < serialised->recordCount ? "," : "");
		return consumed;
	}
	if (full) {
		fprintf(file, "\"site\": \"%x/%x\", \"p\": %u, ", record->site, record->caller, record->path);
	}
	if (label->name[0]) {
		fprintf(file, "\"name\": ");
		writeEscapedString(file, label->name);
		fprintf(file, ", ");
	}
	if (full && label->member) {
		fprintf(file, "\"member\": \"%s\", ", label->member);
	}
	if (label->player[0]) {
		fprintf(file, "\"player\": ");
		writeEscapedString(file, label->player);
		fprintf(file, ", ");
	}
	writeFlags(file, label, value, record->length);
	if (label->ratings) {
		writeRatings(file, value);
	} else if (isStringPair(serialised, i)) {
		fprintf(file, "\"str\": ");
		writeEscaped(
			file, serialised->bytes.data + serialised->records[i + 1].offset, memory_loadU32(value)
		);
		consumed = 2;
	} else {
		writeScalar(file, value, record->length);
	}
	if (record->offset == seedOffset) {
		fprintf(file, ", \"role\": \"seed\"");
	}
	fprintf(file, "}%s\n", i + consumed < serialised->recordCount ? "," : "");
	return consumed;
}

static void writeHeader(
	FILE *file,
	const Serialised *serialised,
	const char *source,
	uint32_t awayTeam,
	bool full
) {
	const uint8_t *bytes = serialised->bytes.data;
	fprintf(
		file,
		"{\n  \"format\": \"fm24-match-config/1\",\n  \"game_build\": %u,\n",
		(unsigned)game_timestamp()
	);
	fprintf(file, "  \"engine_version\": \"%u.%u.%u.%u\",\n", bytes[2], bytes[3], bytes[4], bytes[5]);
	fprintf(file, "  \"source\": ");
	writeEscapedString(file, source);
	if (awayTeam) {
		fprintf(file, ",\n  \"away_team\": %u", (unsigned)awayTeam);
	}
	if (full) {
		fprintf(file, ",\n  \"bytes\": %zu,\n  \"paths\": [\n", serialised->bytes.length);
		for (size_t i = 0; i < serialised->pathCount; ++i) {
			fprintf(
				file, "    \"%s\"%s\n", serialised->paths[i], i + 1 < serialised->pathCount ? "," : ""
			);
		}
		fprintf(file, "  ]");
	}
	fprintf(file, ",\n  \"fields\": [\n");
}
