// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// fm24ctl probe: where the code and data the harness uses are in another build of fm.exe.
//
// Reads fm.exe from disk (read-only; nothing is injected and the game isn't touched) and looks
// for each function of probe_patterns.c in its executable sections:
//   1. every run of at least ANCHOR_LENGTH bytes that must match gives the function anchors
//      (8-byte keys) in a hash table, and one pass over the image counts each anchor's hits;
//   2. a second pass scores the function at each hit of its rarest anchors, on the bytes that
//      must match. A unique best candidate is the function; ties (look-alike functions) take the
//      address shift most found functions share;
//   3. a jmp thunk (no fixed bytes worth searching) is the jmp to its target, found as above.
// Data addresses are then read from the RIP-relative instruction that references them.

#include "commands.h"
#include "inject.h"
#include "paths.h"
#include "probe_patterns.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	STEAM_TIMESTAMP = 1744804480, // the build the patterns come from (src/dll/game.h)
	ANCHOR_LENGTH = 8,
	ANCHOR_STRIDE = 64,        // long runs get an anchor every this many bytes
	ANCHORS_PER_FUNCTION = 2,  // how many of a function's rarest anchors find candidates
	MAX_ANCHOR_HITS = 1 << 16, // anchors more common than this aren't used
	MAX_CANDIDATES = 64,       // best-scoring candidates kept per function
	JMP_REL32 = 0xE9,
	JMP_REL32_LENGTH = 5,
	MAX_SECTIONS = 96,
	TABLE_BITS = 16,
	SECTION_NAME_LENGTH = 8,
	DOS_HEADER_LFANEW = 0x3C,
	PE32_PLUS_MAGIC = 0x20B,
	PERCENT = 100,
};

typedef struct Section {
	char name[SECTION_NAME_LENGTH + 1];
	uint32_t rva;
	uint32_t virtualSize;
	const uint8_t *raw; // the section's bytes in the file
	uint32_t rawSize;
	uint32_t characteristics;
} Section;

typedef struct Image {
	HANDLE file;
	HANDLE mapping;
	const uint8_t *view;
	size_t size;
	uint32_t timestamp;
	uint32_t sizeOfImage;
	Section sections[MAX_SECTIONS];
	size_t sectionCount;
} Image;

typedef struct Anchor {
	uint64_t key;
	uint32_t function;
	uint32_t offset; // into the function
	uint32_t hits;
	bool used;
	int32_t next; // next anchor in the same hash bucket, or -1
} Anchor;

typedef enum Status { NOT_FOUND, FOUND, AMBIGUOUS } Status;

typedef struct Match {
	uint32_t firstAnchor;
	uint32_t anchorCount;
	uint32_t mustMatch;            // bytes the mask requires
	uint32_t matched;              // of mustMatch, at the best candidates
	uint32_t ties[MAX_CANDIDATES]; // the candidates scoring `matched`
	uint32_t tieCount;
	bool tooMany;       // more than MAX_CANDIDATES candidates scored `matched`
	uint32_t firstDiff; // offset of the first required byte that differs (UINT32_MAX if none)
	Status status;
	uint32_t rva; // FOUND: where the function is in this build
	bool byShift; // FOUND by the common address shift among tied candidates
} Match;

typedef struct Probe {
	Image image;
	Anchor *anchors;
	size_t anchorCount;
	int32_t buckets[1 << TABLE_BITS];
	Match *matches;
} Probe;

static bool defaultGamePath(wchar_t *path, size_t size);
static bool openImage(Image *image, const wchar_t *path);
static void closeImage(Image *image);
static bool parseHeaders(Image *image);
static const uint8_t *imageBytes(const Image *image, uint32_t rva, uint32_t length);
static const char *sectionName(const Image *image, uint32_t rva);
static bool mustMatch(const ProbeFunction *function, uint32_t offset);
static uint64_t loadU64(const uint8_t *bytes);
static uint32_t bucketOf(uint64_t key);
static bool buildAnchors(Probe *probe);
static bool addAnchor(Probe *probe, size_t *capacity, uint32_t function, uint32_t offset);
static void scan(Probe *probe, bool collect);
static void chooseAnchors(Probe *probe);
static void considerCandidate(Probe *probe, uint32_t function, uint32_t rva);
static void settle(Match *match);
static void resolveThunks(Probe *probe);
static bool isThunkOf(size_t thunk, size_t target);
static void collectJumpsTo(const Image *image, uint32_t target, Match *match);
static uint32_t score(const Probe *probe, size_t function, uint32_t rva, uint32_t *firstDiff);
static void resolveTies(Probe *probe);
static int64_t commonShift(const Probe *probe, size_t rank);
static void report(FILE *out, const Probe *probe, const wchar_t *path);
static void reportData(FILE *out, const Probe *probe);
static void reportSummary(FILE *out, const Probe *probe);

int commands_probe(int argc, wchar_t **argv) {
	wchar_t path[HARNESS_PATH_LENGTH];
	if (argc >= 3) {
		(void)wcsncpy(path, argv[2], HARNESS_PATH_LENGTH - 1);
		path[HARNESS_PATH_LENGTH - 1] = 0;
	} else if (!defaultGamePath(path, HARNESS_PATH_LENGTH)) {
		fprintf(stderr, "usage: fm24ctl probe [path\\to\\fm.exe] (or start the game first)\n");
		return 2;
	}
	Probe *probe = calloc(1, sizeof *probe);
	if (!probe) {
		return 1;
	}
	int status = 1;
	probe->matches = calloc(probeFunctionCount, sizeof *probe->matches);
	if (probe->matches && openImage(&probe->image, path) && buildAnchors(probe)) {
		printf("searching %ls for %zu functions...\n", path, probeFunctionCount);
		scan(probe, false);
		chooseAnchors(probe);
		scan(probe, true);
		for (size_t i = 0; i < probeFunctionCount; ++i) {
			settle(&probe->matches[i]);
		}
		resolveTies(probe);
		resolveThunks(probe);
		wchar_t directory[HARNESS_PATH_LENGTH];
		wchar_t output[HARNESS_PATH_LENGTH];
		FILE *file = NULL;
		if (paths_executableDirectory(directory, HARNESS_PATH_LENGTH) &&
				paths_join(output, HARNESS_PATH_LENGTH, directory, L"probe.txt")) {
			file = _wfopen(output, L"w");
		}
		reportSummary(stdout, probe);
		if (file) {
			report(file, probe, path);
			status = fclose(file) == 0 ? 0 : 1;
			printf("full report: %ls (please send this file)\n", output);
		} else {
			report(stdout, probe, path);
			status = 0;
		}
	}
	closeImage(&probe->image);
	free(probe->anchors);
	free(probe->matches);
	free(probe);
	return status;
}

// The running fm.exe's file, if the game is running.
static bool defaultGamePath(wchar_t *path, size_t size) {
	unsigned long pid = inject_findGame();
	if (!pid) {
		return false;
	}
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) {
		return false;
	}
	DWORD length = (DWORD)size;
	bool ok = QueryFullProcessImageNameW(process, 0, path, &length) != 0;
	CloseHandle(process);
	return ok;
}

static bool openImage(Image *image, const wchar_t *path) {
	image->file = CreateFileW(
		path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
	);
	if (image->file == INVALID_HANDLE_VALUE) {
		image->file = NULL;
		fprintf(stderr, "cannot open %ls (%lu)\n", path, GetLastError());
		return false;
	}
	LARGE_INTEGER size;
	if (!GetFileSizeEx(image->file, &size)) {
		return false;
	}
	image->size = (size_t)size.QuadPart;
	image->mapping = CreateFileMappingW(image->file, NULL, PAGE_READONLY, 0, 0, NULL);
	image->view = image->mapping ? MapViewOfFile(image->mapping, FILE_MAP_READ, 0, 0, 0) : NULL;
	if (!image->view) {
		fprintf(stderr, "cannot map %ls (%lu)\n", path, GetLastError());
		return false;
	}
	if (!parseHeaders(image)) {
		fprintf(stderr, "%ls is not a 64-bit Windows executable\n", path);
		return false;
	}
	return true;
}

static void closeImage(Image *image) {
	if (image->view) {
		UnmapViewOfFile(image->view);
	}
	if (image->mapping) {
		CloseHandle(image->mapping);
	}
	if (image->file) {
		CloseHandle(image->file);
	}
}

static bool parseHeaders(Image *image) {
	const uint8_t *view = image->view;
	if (image->size < DOS_HEADER_LFANEW + 4 || view[0] != 'M' || view[1] != 'Z') {
		return false;
	}
	uint32_t ntOffset = 0;
	memcpy(&ntOffset, view + DOS_HEADER_LFANEW, sizeof ntOffset);
	if (ntOffset > image->size - sizeof(IMAGE_NT_HEADERS64)) {
		return false;
	}
	IMAGE_NT_HEADERS64 nt;
	memcpy(&nt, view + ntOffset, sizeof nt);
	if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != PE32_PLUS_MAGIC) {
		return false;
	}
	image->timestamp = nt.FileHeader.TimeDateStamp;
	image->sizeOfImage = nt.OptionalHeader.SizeOfImage;
	size_t headers =
		ntOffset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
	size_t count = nt.FileHeader.NumberOfSections;
	if (count > MAX_SECTIONS || headers + count * sizeof(IMAGE_SECTION_HEADER) > image->size) {
		return false;
	}
	for (size_t i = 0; i < count; ++i) {
		IMAGE_SECTION_HEADER header;
		memcpy(&header, view + headers + i * sizeof header, sizeof header);
		Section *section = &image->sections[image->sectionCount++];
		memcpy(section->name, header.Name, SECTION_NAME_LENGTH);
		section->name[SECTION_NAME_LENGTH] = 0;
		section->rva = header.VirtualAddress;
		section->virtualSize = header.Misc.VirtualSize;
		section->characteristics = header.Characteristics;
		uint64_t end = (uint64_t)header.PointerToRawData + header.SizeOfRawData;
		section->raw = end <= image->size ? view + header.PointerToRawData : NULL;
		section->rawSize = section->raw ? header.SizeOfRawData : 0;
	}
	return true;
}

// `length` bytes at `rva` as stored in the file, or NULL if they aren't all in one section's data.
static const uint8_t *imageBytes(const Image *image, uint32_t rva, uint32_t length) {
	for (size_t i = 0; i < image->sectionCount; ++i) {
		const Section *section = &image->sections[i];
		if (rva >= section->rva &&
				(uint64_t)rva + length <= (uint64_t)section->rva + section->rawSize) {
			return section->raw + (rva - section->rva);
		}
	}
	return NULL;
}

static const char *sectionName(const Image *image, uint32_t rva) {
	for (size_t i = 0; i < image->sectionCount; ++i) {
		const Section *section = &image->sections[i];
		if (rva >= section->rva && rva - section->rva < section->virtualSize) {
			return section->name;
		}
	}
	return "?";
}

static bool mustMatch(const ProbeFunction *function, uint32_t offset) {
	return probePatternMask[function->maskOffset + offset / 8] >> (offset % 8) & 1;
}

static uint64_t loadU64(const uint8_t *bytes) {
	uint64_t value = 0;
	memcpy(&value, bytes, sizeof value);
	return value;
}

static uint32_t bucketOf(uint64_t key) {
	return (uint32_t)((key * 0x9E3779B97F4A7C15ULL) >> (64 - TABLE_BITS));
}

// Anchors: the start of every run of at least ANCHOR_LENGTH required bytes, and every
// ANCHOR_STRIDE bytes along longer runs.
static bool buildAnchors(Probe *probe) {
	memset(probe->buckets, 0xFF, sizeof probe->buckets);
	size_t capacity = 0;
	for (uint32_t f = 0; f < probeFunctionCount; ++f) {
		const ProbeFunction *function = &probeFunctions[f];
		Match *match = &probe->matches[f];
		match->firstAnchor = (uint32_t)probe->anchorCount;
		uint32_t run = 0;
		for (uint32_t i = 0; i < function->size; ++i) {
			if (!mustMatch(function, i)) {
				run = 0;
				continue;
			}
			++match->mustMatch;
			++run;
			if (run >= ANCHOR_LENGTH && (run - ANCHOR_LENGTH) % ANCHOR_STRIDE == 0 &&
					!addAnchor(probe, &capacity, f, i + 1 - ANCHOR_LENGTH)) {
				return false;
			}
		}
		match->anchorCount = (uint32_t)probe->anchorCount - match->firstAnchor;
	}
	return true;
}

static bool addAnchor(Probe *probe, size_t *capacity, uint32_t function, uint32_t offset) {
	if (probe->anchorCount == *capacity) {
		size_t grown = *capacity ? *capacity * 2 : 1024;
		Anchor *anchors = realloc(probe->anchors, grown * sizeof *anchors);
		if (!anchors) {
			return false;
		}
		probe->anchors = anchors;
		*capacity = grown;
	}
	const uint8_t *bytes = probePatternBytes + probeFunctions[function].byteOffset + offset;
	Anchor *anchor = &probe->anchors[probe->anchorCount];
	*anchor = (Anchor){.key = loadU64(bytes), .function = function, .offset = offset};
	uint32_t bucket = bucketOf(anchor->key);
	anchor->next = probe->buckets[bucket];
	probe->buckets[bucket] = (int32_t)probe->anchorCount++;
	return true;
}

// One pass over every executable section: counts anchor hits, or (collect) scores the function
// at each hit of the anchors chooseAnchors picked.
static void scan(Probe *probe, bool collect) {
	const Image *image = &probe->image;
	for (size_t s = 0; s < image->sectionCount; ++s) {
		const Section *section = &image->sections[s];
		if (!(section->characteristics & IMAGE_SCN_MEM_EXECUTE) || section->rawSize < ANCHOR_LENGTH) {
			continue;
		}
		for (uint32_t i = 0; i + ANCHOR_LENGTH <= section->rawSize; ++i) {
			uint64_t key = loadU64(section->raw + i);
			for (int32_t a = probe->buckets[bucketOf(key)]; a >= 0; a = probe->anchors[a].next) {
				Anchor *anchor = &probe->anchors[a];
				if (anchor->key != key) {
					continue;
				}
				if (!collect) {
					++anchor->hits;
				} else if (anchor->used && section->rva + i >= anchor->offset) {
					considerCandidate(probe, anchor->function, section->rva + i - anchor->offset);
				}
			}
		}
	}
}

// Marks each function's ANCHORS_PER_FUNCTION rarest anchors that were found at all.
static void chooseAnchors(Probe *probe) {
	for (size_t f = 0; f < probeFunctionCount; ++f) {
		const Match *match = &probe->matches[f];
		for (int pick = 0; pick < ANCHORS_PER_FUNCTION; ++pick) {
			Anchor *best = NULL;
			for (uint32_t a = 0; a < match->anchorCount; ++a) {
				Anchor *anchor = &probe->anchors[match->firstAnchor + a];
				if (!anchor->used && anchor->hits > 0 && anchor->hits <= MAX_ANCHOR_HITS &&
						(!best || anchor->hits < best->hits)) {
					best = anchor;
				}
			}
			if (best) {
				best->used = true;
			}
		}
	}
}

// Scores the function at `rva` and keeps it if it is among the best so far.
static void considerCandidate(Probe *probe, uint32_t function, uint32_t rva) {
	Match *match = &probe->matches[function];
	for (uint32_t i = 0; i < match->tieCount; ++i) {
		if (match->ties[i] == rva) {
			return;
		}
	}
	uint32_t firstDiff = UINT32_MAX;
	uint32_t matched = score(probe, function, rva, &firstDiff);
	if (matched == 0 || matched < match->matched) {
		return;
	}
	if (matched > match->matched) {
		match->matched = matched;
		match->tieCount = 0;
		match->tooMany = false;
		match->firstDiff = firstDiff;
	}
	if (match->tieCount == MAX_CANDIDATES) {
		match->tooMany = true;
		return;
	}
	match->ties[match->tieCount++] = rva;
}

static void settle(Match *match) {
	if (match->tieCount == 0) {
		match->status = NOT_FOUND;
		match->firstDiff = UINT32_MAX;
	} else if (match->tieCount == 1 && !match->tooMany) {
		match->status = FOUND;
		match->rva = match->ties[0];
	} else {
		match->status = AMBIGUOUS;
	}
}

// How many required bytes of the function match at `rva` (0 if it doesn't fit in the file).
static uint32_t score(const Probe *probe, size_t function, uint32_t rva, uint32_t *firstDiff) {
	const ProbeFunction *pattern = &probeFunctions[function];
	const uint8_t *bytes = imageBytes(&probe->image, rva, pattern->size);
	if (!bytes) {
		return 0;
	}
	const uint8_t *expected = probePatternBytes + pattern->byteOffset;
	uint32_t matched = 0;
	for (uint32_t i = 0; i < pattern->size; ++i) {
		if (!mustMatch(pattern, i)) {
			continue;
		}
		if (bytes[i] == expected[i]) {
			++matched;
		} else if (*firstDiff == UINT32_MAX) {
			*firstDiff = i;
		}
	}
	return matched;
}

// Look-alike functions score the same at each other's addresses. Take the candidate whose shift
// (this build's address minus the Steam one) is the most common among the uniquely found ones.
static void resolveTies(Probe *probe) {
	for (size_t rank = 0; rank < MAX_CANDIDATES; ++rank) {
		int64_t shift = commonShift(probe, rank);
		if (shift == INT64_MIN) {
			break;
		}
		for (size_t f = 0; f < probeFunctionCount; ++f) {
			Match *match = &probe->matches[f];
			for (uint32_t i = 0; match->status == AMBIGUOUS && i < match->tieCount; ++i) {
				if ((int64_t)match->ties[i] - probeFunctions[f].rva == shift) {
					match->status = FOUND;
					match->rva = match->ties[i];
					match->byShift = true;
				}
			}
		}
	}
}

// A jmp thunk is found as the one jmp rel32 in the executable sections that lands on its target.
static void resolveThunks(Probe *probe) {
	for (size_t t = 0; t < probeFunctionCount; ++t) {
		const ProbeFunction *thunk = &probeFunctions[t];
		Match *match = &probe->matches[t];
		const uint8_t *opcode = probePatternBytes + thunk->byteOffset;
		if (match->status != NOT_FOUND || thunk->size != JMP_REL32_LENGTH || opcode[0] != JMP_REL32) {
			continue;
		}
		for (size_t f = 0; f < probeFunctionCount; ++f) {
			if (isThunkOf(t, f) && probe->matches[f].status == FOUND) {
				collectJumpsTo(&probe->image, probe->matches[f].rva, match);
				match->matched = match->tieCount ? match->mustMatch : 0;
				settle(match);
			}
		}
	}
}

// Adds every jmp rel32 in the executable sections that lands on `target` to the match's ties.
static void collectJumpsTo(const Image *image, uint32_t target, Match *match) {
	for (size_t s = 0; s < image->sectionCount; ++s) {
		const Section *section = &image->sections[s];
		if (!(section->characteristics & IMAGE_SCN_MEM_EXECUTE)) {
			continue;
		}
		for (uint32_t i = 0; i + JMP_REL32_LENGTH <= section->rawSize; ++i) {
			if (section->raw[i] != JMP_REL32) {
				continue;
			}
			int32_t displacement = 0;
			memcpy(&displacement, section->raw + i + 1, sizeof displacement);
			int64_t lands = (int64_t)section->rva + i + JMP_REL32_LENGTH + displacement;
			if (lands == target && match->tieCount < MAX_CANDIDATES) {
				match->ties[match->tieCount++] = section->rva + i;
			}
		}
	}
}

// Whether function `thunk` is the jmp thunk the generator found `target` through: the target's
// uses read "<the thunk's uses> (thunk target)".
static bool isThunkOf(size_t thunk, size_t target) {
	const char *uses = probeFunctions[thunk].uses;
	const char *targetUses = probeFunctions[target].uses;
	size_t length = strlen(uses);
	return strncmp(targetUses, uses, length) == 0 &&
				 strcmp(targetUses + length, " (thunk target)") == 0;
}

// The rank-th most common shift among the uniquely found functions (INT64_MIN if none).
static int64_t commonShift(const Probe *probe, size_t rank) {
	int64_t previous = INT64_MAX;
	size_t previousCount = SIZE_MAX;
	int64_t result = INT64_MIN;
	for (size_t r = 0; r <= rank; ++r) {
		result = INT64_MIN;
		size_t bestCount = 0;
		for (size_t f = 0; f < probeFunctionCount; ++f) {
			const Match *match = &probe->matches[f];
			if (match->status != FOUND || match->byShift) {
				continue;
			}
			int64_t shift = (int64_t)match->rva - probeFunctions[f].rva;
			size_t count = 0;
			for (size_t g = 0; g < probeFunctionCount; ++g) {
				const Match *other = &probe->matches[g];
				count += other->status == FOUND && !other->byShift &&
								 (int64_t)other->rva - probeFunctions[g].rva == shift;
			}
			// Order: by count, then by shift, strictly after the previous rank.
			bool afterPrevious = count < previousCount || (count == previousCount && shift > previous);
			if (afterPrevious && (count > bestCount || (count == bestCount && shift < result))) {
				bestCount = count;
				result = shift;
			}
		}
		if (result == INT64_MIN) {
			return INT64_MIN;
		}
		previous = result;
		previousCount = bestCount;
	}
	return result;
}

static void report(FILE *out, const Probe *probe, const wchar_t *path) {
	const Image *image = &probe->image;
	fprintf(out, "fm24ctl probe (protocol %ls)\n", HARNESS_PROTOCOL);
	fprintf(out, "file: %ls\n", path);
	fprintf(
		out,
		"PE timestamp: %u (the supported Steam build is %u)\nsize of image: 0x%X\n\nsections:\n",
		image->timestamp,
		(unsigned)STEAM_TIMESTAMP,
		image->sizeOfImage
	);
	for (size_t i = 0; i < image->sectionCount; ++i) {
		const Section *section = &image->sections[i];
		fprintf(
			out,
			"  %-8s rva 0x%08X size 0x%08X raw 0x%08X flags 0x%08X\n",
			section->name,
			section->rva,
			section->virtualSize,
			section->rawSize,
			section->characteristics
		);
	}
	fprintf(out, "\nfunctions (Steam rva -> this build; match = required bytes that match):\n");
	for (size_t f = 0; f < probeFunctionCount; ++f) {
		const ProbeFunction *function = &probeFunctions[f];
		const Match *match = &probe->matches[f];
		fprintf(out, "  %-36s 0x%08X ", function->name, function->rva);
		if (match->status == FOUND) {
			fprintf(
				out,
				"-> 0x%08X %-8s shift %+lld, match %u/%u",
				match->rva,
				sectionName(image, match->rva),
				(long long)match->rva - function->rva,
				match->matched,
				match->mustMatch
			);
			if (match->matched < match->mustMatch) {
				fprintf(out, " (first difference at +0x%X)", match->firstDiff);
			}
			fprintf(out, "%s", match->byShift ? " (look-alikes; chosen by shift)" : "");
		} else if (match->status == AMBIGUOUS) {
			fprintf(out, "-> ambiguous, %u candidates:", match->tieCount);
			for (uint32_t i = 0; i < match->tieCount; ++i) {
				fprintf(out, " 0x%08X", match->ties[i]);
			}
		} else {
			fprintf(out, "-> not found");
		}
		fprintf(out, "%s [%s]\n", match->tooMany ? " (too many candidates)" : "", function->uses);
	}
	reportData(out, probe);
	fprintf(out, "\n");
	reportSummary(out, probe);
}

static void reportData(FILE *out, const Probe *probe) {
	fprintf(out, "\ndata (Steam rva -> this build):\n");
	for (size_t d = 0; d < probeDataCount; ++d) {
		const ProbeData *data = &probeData[d];
		const Match *match = &probe->matches[data->function];
		fprintf(out, "  %-36s 0x%08X ", data->name, data->rva);
		const uint8_t *instruction =
			match->status == FOUND
				? imageBytes(&probe->image, match->rva + data->instruction, data->instructionSize)
				: NULL;
		if (!instruction) {
			fprintf(out, "-> unknown (%s not found)\n", probeFunctions[data->function].name);
			continue;
		}
		int32_t displacement = 0;
		memcpy(&displacement, instruction + data->operand, sizeof displacement);
		int64_t rva = (int64_t)match->rva + data->instruction + data->instructionSize + displacement;
		fprintf(
			out,
			"-> 0x%08llX %-8s shift %+lld (via %s+0x%X)\n",
			(long long)rva,
			sectionName(&probe->image, (uint32_t)rva),
			(long long)(rva - data->rva),
			probeFunctions[data->function].name,
			data->instruction
		);
	}
}

static void reportSummary(FILE *out, const Probe *probe) {
	size_t exact = 0;
	size_t partial = 0;
	size_t ambiguous = 0;
	size_t missing = 0;
	for (size_t f = 0; f < probeFunctionCount; ++f) {
		const Match *match = &probe->matches[f];
		if (match->status == FOUND && match->matched == match->mustMatch) {
			++exact;
		} else if (match->status == FOUND) {
			++partial;
		} else if (match->status == AMBIGUOUS) {
			++ambiguous;
		} else {
			++missing;
		}
	}
	fprintf(
		out,
		"PE timestamp %u%s\n",
		probe->image.timestamp,
		probe->image.timestamp == STEAM_TIMESTAMP ? " (the supported Steam build)" : ""
	);
	fprintf(
		out,
		"functions: %zu identical, %zu found with differences, %zu ambiguous, %zu not found "
		"(of %zu)\n",
		exact,
		partial,
		ambiguous,
		missing,
		probeFunctionCount
	);
	for (size_t rank = 0; rank < 4; ++rank) {
		int64_t shift = commonShift(probe, rank);
		if (shift == INT64_MIN) {
			break;
		}
		size_t count = 0;
		for (size_t f = 0; f < probeFunctionCount; ++f) {
			const Match *match = &probe->matches[f];
			count += match->status == FOUND && (int64_t)match->rva - probeFunctions[f].rva == shift;
		}
		fprintf(
			out,
			"  shift %+lld: %zu functions (%zu%%)\n",
			(long long)shift,
			count,
			count * PERCENT / probeFunctionCount
		);
	}
}
