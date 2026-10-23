// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "slug.h"

#include <stdint.h>


enum { LATIN_FIRST = 0xC0, LATIN_LAST = 0x17F, SEPARATOR = -1 };

// U+00C0..U+017F without accents (Unicode decompositions); '-' marks the two signs (× ÷).
static const char latin[] = "aaaaaaaceeeeiiiidnooooo-ouuuuyts"
														"aaaaaaaceeeeiiiidnooooo-ouuuuyty"
														"aaaaaaccccccccddddeeeeeeeeeegggg"
														"gggghhhhiiiiiiiiiiiijjkkklllllll"
														"lllnnnnnnnnnoooooooorrrrrrssssss"
														"ssttttttuuuuuuuuuuuuwwyyyzzzzzzs";

// The letters that become two.
static const struct {
	uint32_t codePoint;
	char letters[3];
} twoLetters[] = {
	{0xC6, "ae"},
	{0xDE, "th"},
	{0xDF, "ss"},
	{0xE6, "ae"},
	{0xFE, "th"},
	{0x132, "ij"},
	{0x133, "ij"},
	{0x152, "oe"},
	{0x153, "oe"},
};

// The next code point of `text` (advancing it); U+FFFD for a malformed sequence.
static uint32_t nextCodePoint(const unsigned char **text) {
	const unsigned char *c = *text;
	size_t length = *c < 0x80             ? 1
									: (*c & 0xE0) == 0xC0 ? 2
									: (*c & 0xF0) == 0xE0 ? 3
									: (*c & 0xF8) == 0xF0 ? 4
																				: 0;
	uint32_t value = length == 1 ? *c : length ? *c & (0x7F >> length) : 0xFFFD;
	for (size_t i = 1; i < length; ++i) {
		if ((c[i] & 0xC0) != 0x80) {
			*text = c + i;
			return 0xFFFD;
		}
		value = value << 6 | (c[i] & 0x3F);
	}
	*text = c + (length ? length : 1);
	return value;
}

// The letters `codePoint` becomes (copied to `out`), 0 if it is dropped, or SEPARATOR.
static int lettersOf(uint32_t codePoint, char out[2]) {
	if ((codePoint >= 'a' && codePoint <= 'z') || (codePoint >= 'A' && codePoint <= 'Z')) {
		out[0] = (char)(codePoint | 0x20);
		return 1;
	}
	if (codePoint == '\'' || codePoint == '.' || codePoint == 0x2019) { // ’
		return 0;
	}
	if (codePoint < LATIN_FIRST || codePoint > LATIN_LAST) {
		return SEPARATOR;
	}
	for (size_t i = 0; i < sizeof twoLetters / sizeof twoLetters[0]; ++i) {
		if (twoLetters[i].codePoint == codePoint) {
			out[0] = twoLetters[i].letters[0];
			out[1] = twoLetters[i].letters[1];
			return 2;
		}
	}
	char letter = latin[codePoint - LATIN_FIRST];
	if (letter == '-') {
		return SEPARATOR;
	}
	out[0] = letter;
	return 1;
}

bool slug_fromName(const char *utf8, char *out, size_t size) {
	size_t length = 0;
	bool separate = false;
	for (const unsigned char *text = (const unsigned char *)utf8; *text;) {
		char letters[2];
		int count = lettersOf(nextCodePoint(&text), letters);
		if (count == SEPARATOR) {
			separate = length > 0;
			continue;
		}
		for (int i = 0; i < count; ++i) {
			if (length + (separate ? 2 : 1) >= size) {
				return false;
			}
			if (separate) {
				out[length++] = '-';
				separate = false;
			}
			out[length++] = letters[i];
		}
	}
	if (size) {
		out[length] = 0;
	}
	return length > 0;
}
