// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "site_members.h"


const char *siteMembers_find(uint32_t site) {
	size_t count = 0;
	const SiteMember *members = siteMembers_table(&count);
	size_t low = 0;
	size_t high = count;
	while (low < high) {
		size_t middle = low + (high - low) / 2;
		if (members[middle].site < site) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	return low < count && members[low].site == site ? members[low].member : NULL;
}
