// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "list_teams": every club's first team, for the viewer's away team picker. The output is a
// UTF-8 text file of "uniqueId<TAB>short name" lines (database_listTeams).

#include "buffer.h"
#include "database.h"
#include "files.h"
#include "job.h"
#include "log.h"

#include <stdio.h>


void jobs_listTeams(const Job *job) {
	ByteBuffer text = {0};
	size_t count = 0;
	if (!database_listTeams(&text, &count)) {
		job_writeResult("error: out of memory listing teams");
	} else if (count == 0) {
		job_writeResult("error: no teams found; is a save loaded?");
	} else if (!files_write(job->output, text.data, text.length)) {
		job_writeResult("error: cannot write the team list; see harness.log");
	} else {
		log_message("list_teams: %zu teams", count);
		job_writeResult("ok: %zu teams", count);
	}
	buffer_free(&text);
}
