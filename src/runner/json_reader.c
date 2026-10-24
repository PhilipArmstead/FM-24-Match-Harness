// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "json_reader.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>


enum { MAX_DEPTH = 64 };

static bool appendUtf8(ByteBuffer *out, unsigned long codePoint);
static bool readHex4(JsonReader *reader, unsigned long *value);
static bool readUnicodeEscape(JsonReader *reader, ByteBuffer *out);
static bool readEscape(JsonReader *reader, ByteBuffer *out);
static bool readUtf8(JsonReader *reader);
static bool readDigits(JsonReader *reader);
static bool
convertUnsigned(JsonReader *reader, const char *start, const char *end, unsigned long long *value);
static bool skipLiteral(JsonReader *reader);
static bool skipContainer(JsonReader *reader, bool isObject, int depth);
static bool skipNested(JsonReader *reader, int depth);

void jsonReader_init(JsonReader *reader, const char *text, size_t length) {
	*reader = (JsonReader){.begin = text, .position = text, .end = text + length};
}

bool jsonReader_fail(JsonReader *reader, const char *message) {
	if (reader->error[0] == 0) {
		(void)snprintf(
			reader->error,
			sizeof reader->error,
			"%s at byte %td",
			message,
			reader->position - reader->begin
		);
	}
	return false;
}

bool jsonReader_consume(JsonReader *reader, char c) {
	jsonReader_skipWhitespace(reader);
	if (reader->position < reader->end && *reader->position == c) {
		++reader->position;
		return true;
	}
	return false;
}

bool jsonReader_readString(JsonReader *reader, ByteBuffer *out) {
	if (out) {
		buffer_clear(out);
	}
	if (!jsonReader_consume(reader, '"')) {
		return jsonReader_fail(reader, "expected string");
	}
	while (reader->position < reader->end && *reader->position != '"') {
		const char *start = reader->position;
		while (
			reader->position < reader->end && *reader->position != '"' && *reader->position != '\\') {
			uint8_t c = (uint8_t)*reader->position;
			if (c < 0x20) {
				return jsonReader_fail(reader, "unescaped control character in string");
			}
			if (c < 0x80) {
				++reader->position;
			} else if (!readUtf8(reader)) {
				return false;
			}
		}
		if (out && !buffer_append(out, start, (size_t)(reader->position - start))) {
			return jsonReader_fail(reader, "out of memory");
		}
		if (reader->position < reader->end && *reader->position == '\\') {
			++reader->position;
			if (!readEscape(reader, out)) {
				return jsonReader_fail(reader, "out of memory");
			}
		}
	}
	if (reader->position >= reader->end) {
		return jsonReader_fail(reader, "unterminated string");
	}
	++reader->position;
	return true;
}

static bool readUtf8(JsonReader *reader) {
	uint8_t first = (uint8_t)*reader->position;
	unsigned length = first >= 0xC2 && first <= 0xDF   ? 2
										: first >= 0xE0 && first <= 0xEF ? 3
										: first >= 0xF0 && first <= 0xF4 ? 4
																										 : 0;
	if (!length || (size_t)(reader->end - reader->position) < length) {
		return jsonReader_fail(reader, "invalid UTF-8");
	}
	uint32_t codePoint = first & (0x7FU >> length);
	for (unsigned i = 1; i < length; ++i) {
		uint8_t next = (uint8_t)reader->position[i];
		if ((next & 0xC0) != 0x80) {
			return jsonReader_fail(reader, "invalid UTF-8");
		}
		codePoint = (codePoint << 6) | (next & 0x3F);
	}
	if ((length == 3 && codePoint < 0x800) || (length == 4 && codePoint < 0x10000) ||
			(codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF) {
		return jsonReader_fail(reader, "invalid UTF-8");
	}
	reader->position += length;
	return true;
}

static bool readDigits(JsonReader *reader) {
	const char *start = reader->position;
	while (reader->position < reader->end && *reader->position >= '0' && *reader->position <= '9') {
		++reader->position;
	}
	return reader->position != start;
}

bool jsonReader_readNumber(JsonReader *reader, unsigned long long *value, bool *isUnsigned) {
	jsonReader_skipWhitespace(reader);
	const char *start = reader->position;
	bool negative = reader->position < reader->end && *reader->position == '-';
	if (negative) {
		++reader->position;
	}
	if (reader->position < reader->end && *reader->position == '0') {
		++reader->position;
	} else if (!readDigits(reader)) {
		return jsonReader_fail(reader, "expected a number");
	}
	const char *integerEnd = reader->position;
	*isUnsigned = !negative;
	if (reader->position < reader->end && *reader->position == '.') {
		*isUnsigned = false;
		++reader->position;
		if (!readDigits(reader)) {
			return jsonReader_fail(reader, "invalid JSON fraction");
		}
	}
	if (reader->position < reader->end && (*reader->position == 'e' || *reader->position == 'E')) {
		*isUnsigned = false;
		++reader->position;
		if (reader->position < reader->end && (*reader->position == '+' || *reader->position == '-')) {
			++reader->position;
		}
		if (!readDigits(reader)) {
			return jsonReader_fail(reader, "invalid JSON exponent");
		}
	}
	if (reader->position < reader->end && *reader->position &&
			strchr("+-0123456789.eE", *reader->position)) {
		return jsonReader_fail(reader, "invalid JSON number");
	}
	if (!value) {
		return true;
	}
	*value = 0;
	return !*isUnsigned || convertUnsigned(reader, start, integerEnd, value);
}

static bool
convertUnsigned(JsonReader *reader, const char *start, const char *end, unsigned long long *value) {
	for (const char *digit = start; digit < end; ++digit) {
		unsigned number = (unsigned)(*digit - '0');
		if (*value > (ULLONG_MAX - number) / 10) {
			return jsonReader_fail(reader, "integer out of range");
		}
		*value = *value * 10 + number;
	}
	return true;
}

int jsonReader_hexDigit(uint8_t c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

bool jsonReader_skipValue(JsonReader *reader) {
	return skipNested(reader, 0);
}

// Whitespace and `//` line comments (development files explain their fields in them).
void jsonReader_skipWhitespace(JsonReader *reader) {
	for (;;) {
		while (
			reader->position < reader->end && strchr(" \t\r\n", *reader->position) && *reader->position) {
			++reader->position;
		}
		if (reader->end - reader->position < 2 || memcmp(reader->position, "//", 2) != 0) {
			return;
		}
		while (reader->position < reader->end && *reader->position != '\n') {
			++reader->position;
		}
	}
}

static bool appendUtf8(ByteBuffer *out, unsigned long codePoint) {
	if (!out) {
		return true;
	}
	uint8_t bytes[4];
	size_t length = 0;
	if (codePoint < 0x80) {
		bytes[length++] = (uint8_t)codePoint;
	} else if (codePoint < 0x800) {
		bytes[length++] = (uint8_t)(0xC0 | (codePoint >> 6));
		bytes[length++] = (uint8_t)(0x80 | (codePoint & 0x3F));
	} else if (codePoint < 0x10000) {
		bytes[length++] = (uint8_t)(0xE0 | (codePoint >> 12));
		bytes[length++] = (uint8_t)(0x80 | ((codePoint >> 6) & 0x3F));
		bytes[length++] = (uint8_t)(0x80 | (codePoint & 0x3F));
	} else {
		bytes[length++] = (uint8_t)(0xF0 | (codePoint >> 18));
		bytes[length++] = (uint8_t)(0x80 | ((codePoint >> 12) & 0x3F));
		bytes[length++] = (uint8_t)(0x80 | ((codePoint >> 6) & 0x3F));
		bytes[length++] = (uint8_t)(0x80 | (codePoint & 0x3F));
	}
	return buffer_append(out, bytes, length);
}

static bool readHex4(JsonReader *reader, unsigned long *value) {
	if (reader->end - reader->position < 4) {
		return jsonReader_fail(reader, "truncated \\u escape");
	}
	*value = 0;
	for (unsigned i = 0; i < 4; ++i) {
		int digit = jsonReader_hexDigit((uint8_t)*reader->position++);
		if (digit < 0) {
			return jsonReader_fail(reader, "bad \\u escape");
		}
		*value = (*value << 4) | (unsigned)digit;
	}
	return true;
}

static bool readUnicodeEscape(JsonReader *reader, ByteBuffer *out) {
	unsigned long codePoint = 0;
	if (!readHex4(reader, &codePoint)) {
		return false;
	}
	if (codePoint >= 0xD800 && codePoint < 0xDC00) { // surrogate pair
		unsigned long low = 0;
		if (reader->end - reader->position < 2 || reader->position[0] != '\\' ||
				reader->position[1] != 'u') {
			return jsonReader_fail(reader, "lone surrogate");
		}
		reader->position += 2;
		if (!readHex4(reader, &low) || low < 0xDC00 || low > 0xDFFF) {
			return jsonReader_fail(reader, "bad surrogate");
		}
		codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
	} else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
		return jsonReader_fail(reader, "lone surrogate");
	}
	return appendUtf8(out, codePoint);
}

static bool readEscape(JsonReader *reader, ByteBuffer *out) {
	static const char from[] = "\"\\/bfnrt";
	static const char to[] = "\"\\/\b\f\n\r\t";
	if (reader->position >= reader->end) {
		return jsonReader_fail(reader, "truncated escape");
	}
	char c = *reader->position++;
	if (c == 'u') {
		return readUnicodeEscape(reader, out);
	}
	const char *found = c ? strchr(from, c) : NULL;
	if (!found) {
		return jsonReader_fail(reader, "unknown escape");
	}
	return !out || buffer_append(out, &to[found - from], 1);
}

static bool skipLiteral(JsonReader *reader) {
	static const char *const literals[] = {"true", "false", "null"};
	for (size_t i = 0; i < sizeof literals / sizeof literals[0]; ++i) {
		size_t length = strlen(literals[i]);
		if ((size_t)(reader->end - reader->position) >= length &&
				memcmp(reader->position, literals[i], length) == 0) {
			reader->position += length;
			return true;
		}
	}
	bool isUnsigned = false;
	return jsonReader_readNumber(reader, NULL, &isUnsigned);
}

// Skips the members of an object or array whose opening bracket was consumed.
static bool skipContainer(JsonReader *reader, bool isObject, int depth) {
	char close = isObject ? '}' : ']';
	if (jsonReader_consume(reader, close)) {
		return true;
	}
	bool ok = true;
	do {
		if (isObject) {
			ok = jsonReader_readString(reader, NULL) &&
					 (jsonReader_consume(reader, ':') || jsonReader_fail(reader, "expected :"));
		}
		ok = ok && skipNested(reader, depth + 1);
	} while (ok && jsonReader_consume(reader, ','));
	return ok && (jsonReader_consume(reader, close) ||
								 jsonReader_fail(reader, "expected , or closing bracket"));
}

static bool skipNested(JsonReader *reader, int depth) {
	if (depth > MAX_DEPTH) {
		return jsonReader_fail(reader, "nested too deeply");
	}
	jsonReader_skipWhitespace(reader);
	if (reader->position >= reader->end) {
		return jsonReader_fail(reader, "unexpected end");
	}
	if (*reader->position == '"') {
		return jsonReader_readString(reader, NULL);
	}
	if (jsonReader_consume(reader, '{')) {
		return skipContainer(reader, true, depth);
	}
	if (jsonReader_consume(reader, '[')) {
		return skipContainer(reader, false, depth);
	}
	return skipLiteral(reader);
}
