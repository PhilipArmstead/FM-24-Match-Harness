// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Which struct member each serialiser write call site writes, found in IDA from the instruction
// that loads the write's source pointer (tools/fields/). "STRUCT+0xNN" is a member of the object
// the serialiser writes, "STRUCT[element]" an element of an array in it, "(computed)" a local value
// (a count, flag or conversion) and "(constant)" a fixed byte from the game's data.
#pragma once

#include <stddef.h>
#include <stdint.h>


typedef struct SiteMember {
	uint32_t site; // RVA of the write's call site (its return address)
	const char *member;
} SiteMember;

// The generated table (site_members.c), sorted by site.
const SiteMember *siteMembers_table(size_t *count);

// The member written at `site`, or NULL if unknown.
const char *siteMembers_find(uint32_t site);
