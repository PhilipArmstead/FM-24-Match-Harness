// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "list_players": every player, for the viewer's swap prompt. The output is a UTF-8 text file
// of "uniqueId<TAB>forename<TAB>surname<TAB>common name<TAB>club" lines (database_listPlayers).

#include "buffer.h"
#include "database.h"
#include "files.h"
#include "job.h"
#include "log.h"

#include <stdio.h>


void jobs_listPlayers(const Job *job) {
	ByteBuffer text = {0};
	size_t count = 0;
	if (!database_listPlayers(&text, &count)) {
		job_writeResult("error: out of memory listing players");
	} else if (count == 0) {
		job_writeResult("error: no players found; is a save loaded?");
	} else if (!files_write(job->output, text.data, text.length)) {
		job_writeResult("error: cannot write the player list; see harness.log");
	} else {
		log_message("list_players: %zu players", count);
		job_writeResult("ok: %zu players", count);
	}
	buffer_free(&text);
}
