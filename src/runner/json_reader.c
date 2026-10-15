// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "json_reader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum { MAX_NUMBER_LENGTH = 64, MAX_DEPTH = 64 };

static void skipWhitespace(JsonReader *reader);
static bool appendUtf8(ByteBuffer *out, unsigned long codePoint);
static bool readHex4(JsonReader *reader, unsigned long *value);
static bool readUnicodeEscape(JsonReader *reader, ByteBuffer *out);
static bool readEscape(JsonReader *reader, ByteBuffer *out);
static bool numberSyntax(const char *token);
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
	skipWhitespace(reader);
	if (reader->position < reader->end && *reader->position == c) {
		++reader->position;
		return true;
	}
	return false;
}

bool jsonReader_readString(JsonReader *reader, ByteBuffer *out) {
	buffer_clear(out);
	if (!jsonReader_consume(reader, '"')) {
		return jsonReader_fail(reader, "expected string");
	}
	while (reader->position < reader->end && *reader->position != '"') {
		char c = *reader->position++;
		if ((uint8_t)c < 0x20) {
			return jsonReader_fail(reader, "unescaped control character in string");
		}
		bool ok = c == '\\' ? readEscape(reader, out) : buffer_append(out, &c, 1);
		if (!ok) {
			return false;
		}
	}
	if (reader->position >= reader->end) {
		return jsonReader_fail(reader, "unterminated string");
	}
	++reader->position;
	return true;
}

bool jsonReader_readNumber(JsonReader *reader, unsigned long long *value, bool *isUnsigned) {
	skipWhitespace(reader);
	const char *start = reader->position;
	while (reader->position < reader->end && *reader->position &&
				 strchr("+-0123456789.eE", *reader->position)) {
		++reader->position;
	}
	size_t length = (size_t)(reader->position - start);
	if (length == 0 || length >= MAX_NUMBER_LENGTH) {
		return jsonReader_fail(reader, "expected a number");
	}
	char token[MAX_NUMBER_LENGTH];
	memcpy(token, start, length);
	token[length] = 0;
	if (!numberSyntax(token)) {
		return jsonReader_fail(reader, "invalid JSON number");
	}
	*isUnsigned = strpbrk(token, "-.eE") == NULL;
	*value = 0;
	if (*isUnsigned) {
		errno = 0;
		char *stop = NULL;
		*value = strtoull(token, &stop, 10);
		if (errno || *stop) {
			return jsonReader_fail(reader, "integer out of range");
		}
	}
	return true;
}

static const char *skipDigits(const char *text) {
	while (*text >= '0' && *text <= '9') {
		++text;
	}
	return text;
}

static bool numberSyntax(const char *token) {
	const char *p = token;
	if (*p == '-') {
		++p;
	}
	if (*p == '0') {
		++p;
	} else {
		if (*p < '1' || *p > '9') {
			return false;
		}
		p = skipDigits(p + 1);
	}
	if (*p == '.') {
		const char *start = ++p;
		p = skipDigits(p);
		if (p == start) {
			return false;
		}
	}
	if (*p == 'e' || *p == 'E') {
		++p;
		if (*p == '+' || *p == '-') {
			++p;
		}
		const char *start = p;
		p = skipDigits(p);
		if (p == start) {
			return false;
		}
	}
	return *p == 0;
}

bool jsonReader_skipValue(JsonReader *reader) {
	return skipNested(reader, 0);
}

static void skipWhitespace(JsonReader *reader) {
	while (
		reader->position < reader->end && strchr(" \t\r\n", *reader->position) && *reader->position) {
		++reader->position;
	}
}

static bool appendUtf8(ByteBuffer *out, unsigned long codePoint) {
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
	char digits[5] = {0};
	memcpy(digits, reader->position, 4);
	char *stop = NULL;
	*value = strtoul(digits, &stop, 16);
	if (*stop) {
		return jsonReader_fail(reader, "bad \\u escape");
	}
	reader->position += 4;
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
	return buffer_append(out, &to[found - from], 1);
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
	unsigned long long value = 0;
	bool isUnsigned = false;
	return jsonReader_readNumber(reader, &value, &isUnsigned);
}

// Skips the members of an object or array whose opening bracket was consumed.
static bool skipContainer(JsonReader *reader, bool isObject, int depth) {
	char close = isObject ? '}' : ']';
	if (jsonReader_consume(reader, close)) {
		return true;
	}
	ByteBuffer key = {0};
	bool ok = true;
	do {
		if (isObject) {
			ok = jsonReader_readString(reader, &key) &&
					 (jsonReader_consume(reader, ':') || jsonReader_fail(reader, "expected :"));
		}
		ok = ok && skipNested(reader, depth + 1);
	} while (ok && jsonReader_consume(reader, ','));
	buffer_free(&key);
	return ok && (jsonReader_consume(reader, close) ||
								 jsonReader_fail(reader, "expected , or closing bracket"));
}

static bool skipNested(JsonReader *reader, int depth) {
	if (depth > MAX_DEPTH) {
		return jsonReader_fail(reader, "nested too deeply");
	}
	skipWhitespace(reader);
	if (reader->position >= reader->end) {
		return jsonReader_fail(reader, "unexpected end");
	}
	if (*reader->position == '"') {
		ByteBuffer ignored = {0};
		bool ok = jsonReader_readString(reader, &ignored);
		buffer_free(&ignored);
		return ok;
	}
	if (jsonReader_consume(reader, '{')) {
		return skipContainer(reader, true, depth);
	}
	if (jsonReader_consume(reader, '[')) {
		return skipContainer(reader, false, depth);
	}
	return skipLiteral(reader);
}
