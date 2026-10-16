// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "match_config.h"

#include "json_reader.h"

#include <stdio.h>
#include <string.h>


typedef struct Encoder {
	JsonReader reader;
	ByteBuffer *out;
	ByteBuffer scratch; // strings read from the JSON
	ByteBuffer key;
	ByteBuffer name;
	ByteBuffer player;
	const ConfigVisitor *visitor;
	uint32_t teams[2];
	size_t field; // index of the field being read, for messages
} Encoder;

typedef struct FieldState {
	int values; // how many value keys the field had (must be 1)
	ConfigValueKind kind;
} FieldState;

static bool
parseConfig(Encoder *encoder, const char *json, size_t length, char *error, size_t errorSize);
static bool failField(Encoder *encoder, const char *message);
static bool emitUnsigned(Encoder *encoder, unsigned long long value, size_t width);
static bool emitHex(Encoder *encoder, uint8_t *hex, size_t length);
static bool readText(Encoder *encoder, ByteBuffer *out);
static bool readUnsignedValue(Encoder *encoder, size_t width);
static bool readU8Map(Encoder *encoder);
static bool readStringValue(Encoder *encoder, bool isHex);
static size_t widthOf(const char *key);
static bool readMember(Encoder *encoder, const char *key, FieldState *field);
static bool readField(Encoder *encoder);
static bool readFields(Encoder *encoder);
static bool readTopLevel(Encoder *encoder, bool *formatOk, bool *haveFields);

bool matchConfig_encode(
	const char *json,
	size_t length,
	ByteBuffer *out,
	uint32_t teams[2],
	char *error,
	size_t errorSize
) {
	Encoder encoder = {.out = out};
	bool ok = parseConfig(&encoder, json, length, error, errorSize);
	if (teams) {
		teams[0] = ok ? encoder.teams[0] : 0;
		teams[1] = ok ? encoder.teams[1] : 0;
	}
	return ok;
}

bool matchConfig_visit(
	const char *json,
	size_t length,
	const ConfigVisitor *visitor,
	char *error,
	size_t errorSize
) {
	ByteBuffer bytes = {0};
	Encoder encoder = {.out = &bytes, .visitor = visitor};
	bool ok = parseConfig(&encoder, json, length, error, errorSize);
	buffer_free(&bytes);
	return ok;
}

static bool
parseConfig(Encoder *encoder, const char *json, size_t length, char *error, size_t errorSize) {
	jsonReader_init(&encoder->reader, json, length);
	buffer_clear(encoder->out);
	bool formatOk = false;
	bool haveFields = false;
	bool ok = readTopLevel(encoder, &formatOk, &haveFields);
	buffer_free(&encoder->scratch);
	buffer_free(&encoder->key);
	buffer_free(&encoder->name);
	buffer_free(&encoder->player);
	if (!ok) {
		(void)snprintf(
			error, errorSize, "%s", encoder->reader.error[0] ? encoder->reader.error : "out of memory"
		);
	} else if (!formatOk) {
		(void)snprintf(
			error, errorSize, "missing or unsupported \"format\" (want fm24-match-config/1)"
		);
	} else if (!haveFields) {
		(void)snprintf(error, errorSize, "missing \"fields\"");
	}
	return ok && formatOk && haveFields;
}

static bool failField(Encoder *encoder, const char *message) {
	char text[160];
	(void)snprintf(text, sizeof text, "field %zu: %s", encoder->field, message);
	return jsonReader_fail(&encoder->reader, text);
}

static bool emitUnsigned(Encoder *encoder, unsigned long long value, size_t width) {
	if (width < 8 && value >> (8 * width)) {
		return failField(encoder, "value does not fit its width");
	}
	uint8_t bytes[8];
	for (size_t i = 0; i < width; ++i) {
		bytes[i] = (uint8_t)(value >> (8 * i));
	}
	return buffer_append(encoder->out, bytes, width);
}

static bool emitHex(Encoder *encoder, uint8_t *hex, size_t length) {
	if (length % 2) {
		return failField(encoder, "hex value has odd length");
	}
	for (size_t i = 0; i < length; i += 2) {
		int high = jsonReader_hexDigit(hex[i]);
		int low = jsonReader_hexDigit(hex[i + 1]);
		if (high < 0 || low < 0) {
			return failField(encoder, "bad hex digit");
		}
		hex[i / 2] = (uint8_t)((unsigned)high * 16 + (unsigned)low);
	}
	return buffer_append(encoder->out, hex, length / 2);
}

static bool readText(Encoder *encoder, ByteBuffer *out) {
	if (!jsonReader_readString(&encoder->reader, out)) {
		return false;
	}
	return !memchr(buffer_string(out), 0, out->length) ||
				 jsonReader_fail(&encoder->reader, "NUL in annotation or key");
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
		if (!jsonReader_readString(reader, NULL) || !jsonReader_consume(reader, ':') ||
				!readUnsignedValue(encoder, 1)) {
			return failField(encoder, "bad u8map entry");
		}
	} while (jsonReader_consume(reader, ','));
	return jsonReader_consume(reader, '}') || failField(encoder, "expected , or } in u8map");
}

static bool readStringValue(Encoder *encoder, bool isHex) {
	if (!jsonReader_readString(&encoder->reader, &encoder->scratch)) {
		return false;
	}
	const char *text = buffer_string(&encoder->scratch);
	size_t length = encoder->scratch.length;
	if (isHex) {
		return emitHex(encoder, encoder->scratch.data, length);
	}
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
	bool isValue =
		width || strcmp(key, "u8map") == 0 || strcmp(key, "str") == 0 || strcmp(key, "hex") == 0;
	if (!isValue) {
		if (encoder->visitor && (strcmp(key, "name") == 0 || strcmp(key, "player") == 0)) {
			return readText(encoder, strcmp(key, "name") == 0 ? &encoder->name : &encoder->player);
		}
		return jsonReader_skipValue(&encoder->reader);
	}
	++field->values;
	if (width) {
		field->kind = (ConfigValueKind)width;
		return readUnsignedValue(encoder, width);
	}
	if (strcmp(key, "u8map") == 0) {
		field->kind = CONFIG_RATINGS;
		return readU8Map(encoder);
	}
	field->kind = strcmp(key, "str") == 0 ? CONFIG_STRING : CONFIG_HEX;
	return readStringValue(encoder, strcmp(key, "hex") == 0);
}

static bool readField(Encoder *encoder) {
	JsonReader *reader = &encoder->reader;
	FieldState field = {0};
	if (encoder->visitor) {
		buffer_clear(encoder->out);
	}
	size_t start = encoder->out->length;
	buffer_clear(&encoder->name);
	buffer_clear(&encoder->player);
	bool ok = jsonReader_consume(reader, '{') || failField(encoder, "expected a field object");
	if (ok && !jsonReader_consume(reader, '}')) {
		do {
			ok = readText(encoder, &encoder->key) &&
					 (jsonReader_consume(reader, ':') || failField(encoder, "expected :")) &&
					 readMember(encoder, buffer_string(&encoder->key), &field);
		} while (ok && jsonReader_consume(reader, ','));
		ok = ok && (jsonReader_consume(reader, '}') || failField(encoder, "expected , or }"));
	}
	if (ok && field.values != 1) {
		return failField(encoder, "needs exactly one of u8/u16/u32/u64/str/hex/u8map");
	}
	if (ok && encoder->visitor && encoder->visitor->field) {
		ConfigField value = {
			.name = buffer_string(&encoder->name),
			.player = buffer_string(&encoder->player),
			.kind = field.kind,
			.bytes = encoder->out->data ? encoder->out->data + start : NULL,
			.length = encoder->out->length - start,
		};
		encoder->visitor->field(encoder->visitor->context, &value);
	}
	return ok;
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

static bool readTopMember(Encoder *encoder, const char *key, bool *formatOk, bool *haveFields) {
	JsonReader *reader = &encoder->reader;
	if (strcmp(key, "format") == 0) {
		bool ok = readText(encoder, &encoder->scratch);
		*formatOk = ok && strcmp(buffer_string(&encoder->scratch), "fm24-match-config/1") == 0;
		return ok;
	}
	if (strcmp(key, "fields") == 0) {
		if (*haveFields) {
			return jsonReader_fail(reader, "duplicate fields");
		}
		*haveFields = readFields(encoder);
		return *haveFields;
	}
	if (encoder->visitor && encoder->visitor->source && strcmp(key, "source") == 0) {
		if (!readText(encoder, &encoder->scratch)) {
			return false;
		}
		encoder->visitor->source(encoder->visitor->context, buffer_string(&encoder->scratch));
		return true;
	}
	if (strcmp(key, "home_team") == 0 || strcmp(key, "away_team") == 0) {
		unsigned long long value = 0;
		bool isUnsigned = false;
		if (!jsonReader_readNumber(reader, &value, &isUnsigned)) {
			return false;
		}
		if (!isUnsigned || value > UINT32_MAX) {
			return jsonReader_fail(reader, "team override must be an unsigned 32-bit integer");
		}
		encoder->teams[strcmp(key, "away_team") == 0] = (uint32_t)value;
		return true;
	}
	return jsonReader_skipValue(reader);
}

// Reads the top-level object; sets the flags for the members a config must have.
static bool readTopLevel(Encoder *encoder, bool *formatOk, bool *haveFields) {
	JsonReader *reader = &encoder->reader;
	ByteBuffer key = {0};
	bool ok =
		jsonReader_consume(reader, '{') || jsonReader_fail(reader, "config must be a JSON object");
	if (ok && !jsonReader_consume(reader, '}')) {
		do {
			ok = readText(encoder, &key) &&
					 (jsonReader_consume(reader, ':') || jsonReader_fail(reader, "expected :")) &&
					 readTopMember(encoder, buffer_string(&key), formatOk, haveFields);
		} while (ok && jsonReader_consume(reader, ','));
		ok = ok && (jsonReader_consume(reader, '}') ||
								 jsonReader_fail(reader, "expected , or } at top level"));
	}
	buffer_free(&key);
	if (ok) {
		while (
			reader->position < reader->end && strchr(" \t\r\n", *reader->position) && *reader->position) {
			++reader->position;
		}
		ok = reader->position == reader->end || jsonReader_fail(reader, "trailing data");
	}
	return ok;
}
