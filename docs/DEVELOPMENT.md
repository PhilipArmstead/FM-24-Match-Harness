FM24 match-engine harness development
===

# Layout

```
src/common/  protocol (job file, events, DLL name), byte buffer, files
src/dll/     game.h (addresses, layouts, signatures), memory, stream,
             match, rng, database, fixture, trace, sites, labels
             (rating names), config_writer, output, profiler, job,
             job_run/build/sensitivity, match_stats, tactic_flags, main
src/runner/  main, commands, inject, client, paths, json_reader,
             match_config (encoder)
cmake/       MinGW-w64 toolchain file
scripts/     build.sh, format.sh, tidy.sh (env.sh: tool locations)
docs/        PLAN.md: the plan and every finding, phase by phase
tools/       memscan.py: read-only memory scanner for the running fm.exe
```

# Building

Requirements: MSYS2 MinGW-w64 gcc (`C:/msys64/mingw64`), a native Windows
CMake and Ninja (by default the ones bundled with Visual Studio Build Tools;
see `scripts/env.sh`), clang-format 22+ and the MinGW clang-tidy
(`pacman -S mingw-w64-x86_64-clang-tools-extra`).

```sh
scripts/build.sh     # build into build/, copy DLL + fm24ctl.exe to bin/
scripts/format.sh    # apply .clang-format (--check to only verify)
scripts/tidy.sh      # clang-tidy with .clang-tidy (warnings are errors)
```

The build uses `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` and friends
and is warning-free; `format.sh --check` and `tidy.sh` are clean.

**Rebuilding while the game runs:** Windows identifies a loaded DLL by its
path, so bump `HARNESS_PROTOCOL` in `src/common/protocol.h` and build; the
next command injects the new DLL. It loads alongside the old one, and the old
copy idles on its own events.

# Use

Run everything from `bin/`. The job file, result file and `harness.log` live
next to the DLL there.

1. Start FM24 and load the save. Wait until the game is **idle** (not
   processing). The engine calls aren't synchronised with the game's own
   threads.
2. Every command that needs the game injects the harness DLL first if this
   version isn't loaded yet. `harness.log` should say
   `ready; waiting for jobs`. `refusing: ...` means a different game build, or a
   software breakpoint on a called function. If a debugger is attached, it may
   pause fm.exe when the DLL loads: resume the game and the injection completes.
   It injects `fm24_harness_p<N>.dll` next to `fm24ctl.exe`, or
   `fm24_harness.dll` if that is missing (e.g. a renamed release copy). The
   fallback must be a build of the same protocol `<N>`, or the runner never
   sees it start
3. `fm24ctl run <input> [seeds] [out.csv]`,
   where `<input>` is a `.bin` input or a `.json` config; e.g.
   `fm24ctl run next.json` plays the config's own seed into `next.csv`
   - seeds: `keep` (the input's own seed; the default), `42`, `1-1000`, or
     `1,5,10-20`
   - output: the input's name with a `.csv` extension unless given. An
     argument of only digits, `,` and `-` is read as seeds, so name the output
     e.g. `2024.csv`, not `2024`
   - one match takes about 3 s, but matches run in parallel: by default on
     one worker thread per logical CPU less one (11 on a 6-core/12-thread
     CPU, ~0.4 s per match on average). Results are identical whatever the
     thread count, and rows stay in seed order. Under load each match's `ms`
     rises to ~4-5 s
   - `--threads N` (1-16): set the worker count (1 = one match at a time)
   - `--profile <samples.txt>`: sample the engine's stack about every 1 ms
     (worker 0 only; see `src/dll/profiler.h` for the format)
   - `--detailed`: also write `<output>-players.csv` and `<output>-teams.csv`,
     with each statistic averaged per match over all the seeds. Players (both
     squads, substitutes included): appearances, starts, minutes, average
     rating (over appearances), then goals, assists, shots, on target, xG,
     xA, key passes, clear-cut chances, passes, crosses, dribbles, tackles,
     interceptions, clearances, blocks, headers, fouls, offsides, cards, set
     pieces taken, goalkeeper saves, goals conceded and distance (km). Teams:
     goals, possession and the team totals. The data is the engine's own
     per-player stats objects
   - the human side makes **no substitutions** in a harness run, not even for
     injuries: an injured or sent-off player is not replaced, so the team
     plays on a man short (this is why its starters can average under 90
     minutes)
4. `fm24ctl get-next-fixture <out.json|out.bin> [--seed N]` builds the human
   manager's next fixture (~0.2 s). The seed defaults to a clock value, like
   the game. The same seed gives a byte-identical file, because the seed also
   seeds the thread-local RNG the pre-match setup uses.
5. `fm24ctl export <input.bin> <out.json>` converts an input to a config
   (uses the game's serialiser). `fm24ctl encode <in.json> <out.bin>`
   converts back (no game needed).
6. `fm24ctl sensitivity <out.csv> [--player NAME|UID] [--values 1-20]
[--seed N]` measures which values the game derives from each rating. It
   builds the next fixture once as a baseline. Then, for each selected player
   (default: everyone in the fixture) and each of their 69 ratings, it changes
   the rating in the database, rebuilds with the same seed, writes every field
   that differs from the baseline, and restores the rating.
   - default: one in-game step (+1, or -1 at the top); `--values`: each listed
     in-game value instead (`1-20` gives full response curves)
   - ~0.12 s per build: one player with `--values 1-20` takes ~2.5 min
   - CSV: `player_uid, player, player_block, rating, rating_from, rating_to`
     (database units: positions 1-20, attributes 1-100), then the changed
     field (`field_block, field` = record index in that block, `site`,
     `length, old, new`). A row saying `structure changed` means the edit
     changed the match's layout (e.g. the player dropped out of the squad), so
     its fields can't be compared

CSV columns: `seed, home_goals, away_goals, clock_ticks (4/s), finished,
rng_final, ms`.

## Match configs (`fm24-match-config/1`)

`fields` lists every write the game's serialiser makes, in order, so encoding
is lossless.

- **Value:** each field has exactly one: `u8`, `u16`, `u32`, `u64`, `str`
  (u32 length + UTF-8), `hex`, or `u8map`.
- **Location:** `site` gives the `<call site>/<caller>` RVAs of the write
  (named parts are in `src/dll/sites.h`), and `p` indexes the `paths` table of serialiser
  call chains.
- **Seed:** the seed field has `"role": "seed"`.
- **Labels:** blocks carry a `"name"` (`match`, `referee`, `venue`, `fixture`,
  `home.player[3]`, `away.manager`, `home.backroom[2]`, ...), and player
  blocks also carry `"player"`. Fields whose meaning is known are named too:
  `.person_uid`, `.ratings`, `.ability` (caps every role rating),
  `.leadership_value`, `.role_rating[winger@AMR]` with its `.role` and
  `.position` bytes, and the team counts and flags.
- **Tactics:** each manager's tactic is under `<side>.manager.tactic`
  (`docs/PLAN.md`, Phase 15). It holds `takers`, the active `plan[0]` and a
  second `plan[1]`, `set_pieces`, and `formation[i]`. A plan names
  `.mentality` (1-7), `.team_instructions[0..2]` and each formation slot's
  `.slot[i].position`, `.slot[i].role` (role bit plus a duty bit) and
  `.slot[i].player_instructions[role].bits[0..1]`.
- **Members:** most fields also carry `"member"`, the game object member the
  serialiser wrote (found in IDA, `tools/fields/`): `MATCH_PLAYER_DATA+0x60`,
  `FIXTURE_RESULT+0x46`, ... Unnamed serialisers appear as `fn_<rva>+0xNN`;
  `STRUCT[element]` is an array element, `(computed)` a value the serialiser
  calculated (a count or flag) and `(constant)` a fixed byte. All of these are
  annotations; the encoder ignores them.
- **Ratings:** each player's ratings are a `u8map`: 15 positions (1-20),
  then 54 attributes (1-100 = in-game value x 5), named in
  `src/dll/labels.c`.

Values and strings (including their length) can be edited freely. Adding or
removing entries changes the structure and is not safe yet. Edited ratings
can be played as they are: the engine plays from the raw attributes, and the
values the game derives from them (ability, role ratings) don't affect results
(`docs/PLAN.md`, Phase 12).

# Known limits

- **Patching the seed in an input is not the game choosing that seed.**
  Pre-match data (attendance etc.) also depends on the build-time RNG.
- One input = one fixture with fixed line-ups and tactics.
- A game-produced .bin and its JSON export differ in one header byte (offset
  21, a flag the deserialiser sets). Match results are identical.
- `get-next-fixture` and `sensitivity` set one byte (`+0x46`) of the real
  fixture in the save and leave it set, as the game's own match setup
  probably does. The first build after loading a save therefore differs in
  that byte from later builds with the same seed.
