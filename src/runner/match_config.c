// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "match_config.h"

#include "json_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct Encoder {
	JsonReader reader;
	ByteBuffer *out;
	ConfigRecords *records;
	ByteBuffer scratch; // strings read from the JSON
	size_t field;       // index of the field being read, for messages
} Encoder;

typedef struct FieldState {
	char site[32];
	size_t start;  // offset in the output where the field's bytes begin
	int values;    // how many value keys the field had (must be 1)
	bool isString; // written by the game as two records
} FieldState;

static bool failField(Encoder *encoder, const char *message);
static bool appendRecord(ConfigRecords *records, const char *site, size_t length);
static bool emitUnsigned(Encoder *encoder, unsigned long long value, size_t width);
static bool emitHex(Encoder *encoder, const char *hex, size_t length);
static bool readUnsignedValue(Encoder *encoder, size_t width);
static bool readU8Map(Encoder *encoder);
static bool readStringValue(Encoder *encoder, bool isHex, FieldState *field);
static size_t widthOf(const char *key);
static bool readMember(Encoder *encoder, const char *key, FieldState *field);
static bool recordField(Encoder *encoder, const FieldState *field);
static bool readField(Encoder *encoder);
static bool readFields(Encoder *encoder);
static bool readTopLevel(Encoder *encoder, bool *formatOk, bool *haveFields);

bool matchConfig_encode(
	const char *json,
	size_t length,
	ByteBuffer *out,
	ConfigRecords *records,
	char *error,
	size_t errorSize
) {
	Encoder encoder = {.out = out, .records = records};
	jsonReader_init(&encoder.reader, json, length);
	buffer_clear(out);
	if (records) {
		records->count = 0;
	}
	bool formatOk = false;
	bool haveFields = false;
	bool ok = readTopLevel(&encoder, &formatOk, &haveFields);
	buffer_free(&encoder.scratch);
	if (!ok) {
		(void)snprintf(error, errorSize, "%s", encoder.reader.error);
	} else if (!formatOk) {
		(void)snprintf(
			error, errorSize, "missing or unsupported \"format\" (want fm24-match-config/1)"
		);
	} else if (!haveFields) {
		(void)snprintf(error, errorSize, "missing \"fields\"");
	}
	return ok && formatOk && haveFields;
}

void matchConfig_freeRecords(ConfigRecords *records) {
	free(records->items);
	*records = (ConfigRecords){0};
}

static bool failField(Encoder *encoder, const char *message) {
	char text[160];
	(void)snprintf(text, sizeof text, "field %zu: %s", encoder->field, message);
	return jsonReader_fail(&encoder->reader, text);
}

static bool appendRecord(ConfigRecords *records, const char *site, size_t length) {
	if (records->count == records->capacity) {
		size_t capacity = records->capacity ? records->capacity * 2 : 65536;
		ConfigRecord *items = realloc(records->items, capacity * sizeof *items);
		if (!items) {
			return false;
		}
		records->items = items;
		records->capacity = capacity;
	}
	ConfigRecord *record = &records->items[records->count++];
	(void)snprintf(record->site, sizeof record->site, "%s", site);
	record->length = length;
	return true;
}

static bool emitUnsigned(Encoder *encoder, unsigned long long value, size_t width) {
	if (width < 8 && value >> (8 * width)) {
		return failField(encoder, "value does not fit its width");
	}
	for (size_t i = 0; i < width; ++i) {
		uint8_t byte = (uint8_t)(value >> (8 * i));
		if (!buffer_append(encoder->out, &byte, 1)) {
			return false;
		}
	}
	return true;
}

static bool emitHex(Encoder *encoder, const char *hex, size_t length) {
	if (length % 2) {
		return failField(encoder, "hex value has odd length");
	}
	for (size_t i = 0; i < length; i += 2) {
		char pair[3] = {hex[i], hex[i + 1], 0};
		char *stop = NULL;
		unsigned long byte = strtoul(pair, &stop, 16);
		uint8_t value = (uint8_t)byte;
		if (*stop || !buffer_append(encoder->out, &value, 1)) {
			return failField(encoder, "bad hex digit");
		}
	}
	return true;
}

static bool readUnsignedValue(Encoder *encoder, size_t width) {
	unsigned long long value = 0;
	bool isUnsigned = false;
	if (!jsonReader_readNumber(&encoder->reader, &value, &isUnsigned)) {
		return false;
	}
	return isUnsigned ? emitUnsigned(encoder, value, width)
										: failField(encoder, "expected an unsigned integer");
}

static bool readU8Map(Encoder *encoder) {
	JsonReader *reader = &encoder->reader;
	if (!jsonReader_consume(reader, '{')) {
		return failField(encoder, "u8map must be an object");
	}
	if (jsonReader_consume(reader, '}')) {
		return true;
	}
	do {
		if (!jsonReader_readString(reader, &encoder->scratch) || !jsonReader_consume(reader, ':') ||
				!readUnsignedValue(encoder, 1)) {
			return failField(encoder, "bad u8map entry");
		}
	} while (jsonReader_consume(reader, ','));
	return jsonReader_consume(reader, '}') || failField(encoder, "expected , or } in u8map");
}

static bool readStringValue(Encoder *encoder, bool isHex, FieldState *field) {
	if (!jsonReader_readString(&encoder->reader, &encoder->scratch)) {
		return false;
	}
	const char *text = buffer_string(&encoder->scratch);
	size_t length = encoder->scratch.length;
	if (isHex) {
		return emitHex(encoder, text, length);
	}
	field->isString = true;
	return emitUnsigned(encoder, length, 4) && buffer_append(encoder->out, text, length);
}

static size_t widthOf(const char *key) {
	static const struct {
		const char *key;
		size_t width;
	} widths[] = {{"u8", 1}, {"u16", 2}, {"u32", 4}, {"u64", 8}};
	for (size_t i = 0; i < sizeof widths / sizeof widths[0]; ++i) {
		if (strcmp(key, widths[i].key) == 0) {
			return widths[i].width;
		}
	}
	return 0;
}

// Reads the value of one member of a field object.
static bool readMember(Encoder *encoder, const char *key, FieldState *field) {
	size_t width = widthOf(key);
	if (strcmp(key, "site") == 0) {
		if (!jsonReader_readString(&encoder->reader, &encoder->scratch)) {
			return false;
		}
		(void)snprintf(field->site, sizeof field->site, "%s", buffer_string(&encoder->scratch));
		return true;
	}
	bool isValue =
		width || strcmp(key, "u8map") == 0 || strcmp(key, "str") == 0 || strcmp(key, "hex") == 0;
	if (!isValue) {
		return jsonReader_skipValue(&encoder->reader);
	}
	++field->values;
	if (width) {
		return readUnsignedValue(encoder, width);
	}
	if (strcmp(key, "u8map") == 0) {
		return readU8Map(encoder);
	}
	return readStringValue(encoder, strcmp(key, "hex") == 0, field);
}

static bool recordField(Encoder *encoder, const FieldState *field) {
	if (!encoder->records) {
		return true;
	}
	size_t length = encoder->out->length - field->start;
	const char *site = field->site[0] ? field->site : "?";
	if (!field->isString) {
		return appendRecord(encoder->records, site, length);
	}
	// the game writes a string as two records: u32 length, then the bytes (site not recorded here)
	return appendRecord(encoder->records, site, 4) &&
				 (length == 4 || appendRecord(encoder->records, "*", length - 4));
}

static bool readField(Encoder *encoder) {
	JsonReader *reader = &encoder->reader;
	FieldState field = {.start = encoder->out->length};
	ByteBuffer key = {0};
	bool ok = jsonReader_consume(reader, '{') || failField(encoder, "expected a field object");
	if (ok && !jsonReader_consume(reader, '}')) {
		do {
			ok = jsonReader_readString(reader, &key) &&
					 (jsonReader_consume(reader, ':') || failField(encoder, "expected :")) &&
					 readMember(encoder, buffer_string(&key), &field);
		} while (ok && jsonReader_consume(reader, ','));
		ok = ok && (jsonReader_consume(reader, '}') || failField(encoder, "expected , or }"));
	}
	buffer_free(&key);
	if (ok && field.values != 1) {
		return failField(encoder, "needs exactly one of u8/u16/u32/u64/str/hex/u8map");
	}
	return ok && recordField(encoder, &field);
}

static bool readFields(Encoder *encoder) {
	JsonReader *reader = &encoder->reader;
	if (!jsonReader_consume(reader, '[')) {
		return jsonReader_fail(reader, "fields must be an array");
	}
	if (jsonReader_consume(reader, ']')) {
		return true;
	}
	do {
		if (!readField(encoder)) {
			return false;
		}
		++encoder->field;
	} while (jsonReader_consume(reader, ','));
	return jsonReader_consume(reader, ']') || jsonReader_fail(reader, "expected , or ] in fields");
}

// Reads the top-level object; sets the flags for the members a config must have.
static bool readTopLevel(Encoder *encoder, bool *formatOk, bool *haveFields) {
	JsonReader *reader = &encoder->reader;
	ByteBuffer key = {0};
	bool ok =
		jsonReader_consume(reader, '{') || jsonReader_fail(reader, "config must be a JSON object");
	if (ok && !jsonReader_consume(reader, '}')) {
		do {
			ok = jsonReader_readString(reader, &key) &&
					 (jsonReader_consume(reader, ':') || jsonReader_fail(reader, "expected :"));
			if (ok && strcmp(buffer_string(&key), "format") == 0) {
				ok = jsonReader_readString(reader, &encoder->scratch);
				*formatOk = ok && strcmp(buffer_string(&encoder->scratch), "fm24-match-config/1") == 0;
			} else if (ok && strcmp(buffer_string(&key), "fields") == 0) {
				ok = readFields(encoder);
				*haveFields = ok;
			} else if (ok) {
				ok = jsonReader_skipValue(reader);
			}
		} while (ok && jsonReader_consume(reader, ','));
		ok = ok && (jsonReader_consume(reader, '}') ||
								 jsonReader_fail(reader, "expected , or } at top level"));
	}
	buffer_free(&key);
	return ok;
}
