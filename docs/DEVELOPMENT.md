FM24 match-engine harness development
===

# Layout

```
src/common/  protocol (job pipe, DLL name), byte buffer, files,
             shared tactic role/position names
src/dll/     game.h (addresses, layouts, signatures), memory, stream,
             match, rng, database, fixture, trace, seed_build (per-seed
             rebuilds), sites/site_lookup/site_members, labels (rating
             names), config_writer, output, profiler, job,
             job_run/build/inspect, match_stats, tactic_flags
             (generated), main
src/runner/  main, commands, inject, client, paths, json_reader,
             match_config (encoder and streaming field visitor),
             fixture_data (platform-independent read model),
             fixture_load (JSON reads/binary decoding jobs),
             probe + probe_patterns (generated)
src/ui/      fixture_window (Win32 controls and GDI pitch drawing),
             fixture_export (portable export filename generation), manifest
tests/       custom test runner, fixture reader, job, runner startup and viewer checks
cmake/       MinGW-w64 toolchain file
scripts/     build.sh, format.sh, tidy.sh (env.sh: tool locations)
docs/        PLAN.md: the plan and every finding, phase by phase
tools/       memscan.py: read-only memory scanner for the running fm.exe
             ida_annotations.py: IDAPython names and comments for fm.exe
             fields/: field-name data and generators (tactic_bits.json ->
               gen_tactic_flags.py -> src/dll/tactic_flags.c; site_ops.json
               -> gen_site_members.py), diff_configs.py (compare two configs)
             experiments/: team instruction analysis and ability experiments
             probe/gen_probe_patterns.py: IDAPython generator of
               src/runner/probe_patterns.c
```

# Building

Requirements: MSYS2 MinGW-w64 gcc (`C:/msys64/mingw64`), a native Windows
CMake and Ninja (by default the ones bundled with Visual Studio Build Tools;
see `scripts/env.sh`), clang-format 22+ and the MinGW clang-tidy
(`pacman -S mingw-w64-x86_64-clang-tools-extra`).

```sh
scripts/build.sh     # build into build/, install stripped DLL + runner + tests into bin/
scripts/format.sh    # apply .clang-format (--check to only verify)
scripts/tidy.sh      # clang-tidy with .clang-tidy (warnings are errors)
```

The build script always configures Release mode and C17, including all
regression checks in `fm24_tests.exe`. CMake defaults Release builds to
`-O2 -DNDEBUG`, with link-time optimisation across the shared code, DLL,
runner and tests. Configuration fails if the compiler does not support LTO.
Release links discard unused sections and do not export the DLL's internal
symbols. Debug builds retain their normal flags and exports without LTO.
Fast-math and build-machine-specific CPU instructions are not enabled.

Unstripped binaries stay in `build/` for diagnosis. The script installs
copies into `bin/` and strips them with CMake's selected toolchain utility,
preserving Windows unwind metadata and application resources. Installation
or stripping errors fail the script instead of silently leaving old
binaries in `bin/`. Close a running runner before rebuilding; for a loaded
DLL, see the protocol-version instructions below.

Run `bin/fm24_tests.exe` for the application regression checks, or pass a
suite name (`job`, `dll`, `fixture_data`, `runner`, `runner_main`, `fixture_export` or
`fixture_window`) to run only that suite. Each named test prints `[PASS]`
or `[FAIL]`, followed by a test/failure count; failures return a nonzero
exit code. Assertions report the source location, expression and expected
and actual values without stopping the remaining checks.

The `dll` suite uses synthetic engine objects to check serialisation traces,
edit alignment, player identity across rebuilt line-ups, instruction labels
and UTF-8 exports without loading FM24. The `job` suite also covers repeated
seed keys and malformed or overflowing seed values.

The `runner` suite checks single-read config preparation, CLI and CSV numbers,
result-file failures, PE bounds, selected-anchor scans, address-shift ranking
and jump-candidate overflow without connecting to FM24.
Config preparation reads replacement-team metadata while encoding, rather than
loading and parsing the same JSON twice. Field visitors reuse one field buffer
instead of retaining the entire encoded fixture. Strings and hex values are
copied in batches, and discarded annotations allocate no decoding buffers.
Malformed UTF-8, Unicode escapes, hex digits and replacement IDs are rejected.
Probe's second scan indexes only selected anchors; address shifts are ranked
once per resolution/reporting phase.

Per-seed rebuilding omits write tracing and edit alignment when the input
has no edits. Edited inputs with unchanged write sites skip call-chain
parsing and alignment allocations. When tracing is needed, call chains are
interned as numeric RVAs; their text is formatted only once per distinct
chain. CPU timing calls are limited to the profiled worker, and workers
only copy the input when patching seeds rather than rebuilding it.

Use `ctest --test-dir build --output-on-failure` for individual results and the
Release packaging check. Building does not run the tests. To stage only
the two distributable files, without the test executable:

```sh
cmake --install build --config Release --prefix dist --component Runtime --strip
```

The build uses `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` and friends
and is warning-free; `format.sh --check` and `tidy.sh` are clean.

The viewer uses only Windows system libraries (`user32`, `gdi32`,
`comctl32`, `msftedit` and `ole32`, with COM identifiers from `uuid`),
with no third-party UI or parsing dependencies.
Fixture inspection is read-only; `fm24ctl <fixture.json|fixture.bin>` leaves
the existing CLI commands unchanged. With no arguments, it exports the next fixture to
`temp-fixture.json` in the OS temp directory and opens it only if the export
succeeds; failures are printed to the console before exiting. Viewing an
unsaved fixture shows a red warning and an export link in the summary;
clicking it copies the temporary JSON to the current working directory
without overwriting an existing file, then shows the saved filename. A
missing source replaces that line with a red restart message. Viewing an
existing JSON file does not connect to the game. Binary inspection uses an
`inspect` job and a temporary JSON file removed after reading. Inspection
deserialises and traces the existing match without running preparation,
building another fixture or invoking the engine's match runner.

The adjacent Simulation tab calls the existing `commands_run` on a worker
thread, with a timer collecting its exit code on the UI thread. Only one
simulation can run per window; controls are disabled and closing is blocked until it
finishes. Thread bounds reuse the CLI default (logical CPUs minus one,
clamped to 1-16). Seed patterns are limited to 255 characters, checked for
job-safe characters before submission, and fully parsed by the DLL. Outputs
follow the CLI's fixture-based naming, using the saved copy after export.

When configuring CMake directly, use `-DFM24_BUILD_TESTS=ON` to include the
test executable. Its portable reader and filename checks can also be built
on a non-Windows host:

```sh
cc -std=c17 -O2 -Isrc/common -Isrc/runner -Isrc/ui \
  tests/test.c tests/fixture_data_test.c tests/fixture_export_test.c \
  src/ui/fixture_export.c src/runner/fixture_data.c \
  src/runner/match_config.c src/runner/json_reader.c \
  src/common/buffer.c src/common/tactic_names.c -o /tmp/fm24_tests
/tmp/fm24_tests
# Optional integration check against the local Aston Villa/Bristol City baseline:
/tmp/fm24_tests fixture_data tools/experiments/baseline.json
```

Tests use the same dependency-free framework as the Triple Triad project:
include `test.h`, define cases with `TEST(describes_the_behaviour)`, and
use `ASSERT_INT_EQ(actual, expected)` or `ASSERT_BOOL_EQ(actual, expected)`.
Register each case with a descriptive `test_run` call in its suite's
`register...Tests` function. Guard failed setup before dereferencing pointers
or inspecting buffers, since assertions do not abort the test. Keep
pointer-sized and 64-bit comparisons in `ASSERT_BOOL_EQ` to avoid narrowing
them to the integer assertion's `int` parameters.

**Rebuilding while the game runs:** Windows identifies a loaded DLL by its
path, so bump `HARNESS_PROTOCOL` in `src/common/protocol.h` and build. CMake
re-configures by itself, the DLL is renamed `fm24-harness-<version>.dll`, and
`build.sh` replaces the old copy in `bin/`. The next command injects the new
DLL. It loads alongside the old one, and the old copy idles on its own pipe
until the game exits.

**Generated code:** after editing `tools/fields/tactic_bits.json`, run
`python -I tools/fields/gen_tactic_flags.py` to regenerate
`src/dll/tactic_flags.c`. After changing an address in `src/dll/game.h`,
`sites.h` or `site_members.c`, regenerate `src/runner/probe_patterns.c` in IDA
on the Steam `fm.exe`:
`REPO = r"<repo>"; exec(open(REPO + r"\tools\probe\gen_probe_patterns.py").read())`.

# Use

Run everything from `bin/`. `harness.log` lives next to the DLL that fm.exe
loaded: a runner sends its job over a named pipe, so a `fm24ctl` in another
folder (e.g. `build/`) with the same protocol version uses that DLL, and its
log, rather than injecting its own.

The DLL runs one job at a time. Runners and viewer windows that submit while
a job is running wait for it (`the harness is running another job; waiting`),
and their time limit includes that wait. A runner that times out stops waiting
but does not cancel DLL work; later jobs still wait for it.

1. Start FM24 and load the save. Wait until the game is **idle** (not
   processing). The engine calls aren't synchronised with the game's own
   threads.
2. Every command that needs the game injects the harness DLL first if this
   version isn't loaded yet. `harness.log` should say
   `ready; waiting for jobs`. `refusing: ...` means a different game build, or a
   software breakpoint on a called function. If a debugger is attached, it may
   pause fm.exe when the DLL loads: resume the game and the injection completes.
   It injects `fm24-harness-<version>.dll` next to `fm24ctl.exe`, or
   `fm24-harness.dll` if that is missing (e.g. a renamed release copy). The
   fallback must be a build of the same `<version>`, or the runner never
   sees it start
3. `fm24ctl run <input> [seeds] [out.csv]`,
   where `<input>` is a `.bin` input or a `.json` config; e.g.
   `fm24ctl run next.json` plays the config's own seed into `next.csv`.
   It prints a result line (`ok: N/N runs...` or `error: ...`) and a
   summary
   - seeds: `keep` (the input's own seed; the default), `42`, `1-1000`, or
     `1,5,10-20`. A seed plays exactly as the game would with that seed
     (`GAME_MATCH__init_from_fixture`'s seed): the seed also draws pre-match
     values (attendance, the officials' match-day ratings, a random number
     per player, weather), so for each seed `run` has the game rebuild the
     input's fixture with that seed and re-applies the input's edits. An
     unedited dump then matches the game; edited fields keep your values.
     This needs the input to be the human manager's next fixture in the
     running game (built in the current game state); otherwise `run` stops
     with an error. To compare with the game, dump the fixture after clicking
     "Simulate Match" and before "Continue", and run it before "Continue":
     until the team is submitted, a build picks the human side's line-up
     itself, differently for each seed. If a seed's build has a different
     structure (another line-up or AI tactic) where the input was edited,
     that seed fails (see `harness.log`). The input and the build are
     aligned part by part along the serialiser's call chains (`align()` in
     `seed_build.c`), so a difference only stops edits inside the parts that
     differ
   - `--keep-build`: play the input as it is, changing only its seed. The
     pre-match values stay those of the seed it was built with, so results
     differ from the game's for other seeds, but it works for any input
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
     rating (over appearances), then goals, assists, shots, on target,
     blocked and long shots, xG, xA, key passes, clear-cut and half chances,
     passes (and progressive passes), crosses, dribbles, tackles,
     interceptions, clearances, blocks and shots blocked, pressures
     (attempted, inferred; completed), headers, fouls
     committed and suffered, offsides, cards, set pieces taken, goalkeeper
     saves, goals conceded, possession won and lost, high-intensity sprints
     and distance (km). Teams: goals, possession, average rating (as the
     game's match statistics screen shows it) and the team totals. The data
     is the engine's own per-player stats objects. The values match the
     game's statistics screens, team and player (checked on one match). The
     members are listed in `src/dll/match_stats.c` and `docs/PLAN.md`
     (Phase 18). Player rows are ordered by unique ID and retain each person's
     totals when a rebuilt line-up changes order or introduces another player.
     Reports are written only after every seed succeeds; a CSV or detailed-report
     write failure makes the job fail rather than reporting success.
   - both teams' in-match changes (substitutions etc.) are made by the game's
     AI, as when the game simulates a match nobody watches. Before kick-off
     the harness resets the human manager's control flags to "no match plan,
     not in control", so the Match Plan of whoever built the input doesn't
     carry over. AI-managed teams are left as they are
   - `--human-control`: keep the input's flags instead. A team built with a
     human in control then makes no tactical changes (nobody is there to make
     them) and may not even replace injured players
4. `fm24ctl get-next-fixture <out.json|out.bin> [--seed N] [--full]` builds
   the human manager's next fixture (~0.2 s). The seed defaults to a clock
   value, like the game. The same seed gives a byte-identical file, because
   the seed also seeds the thread-local RNG the pre-match setup uses.
   - `--full` (JSON only): also write the research keys: `bytes` and `paths`
     in the header, and `site`, `p` and `member` on each field. Without it
     the file keeps only the values and the labels (`name`, `player`,
     `flags`, `role`), about 1.5 MB instead of 5.4 MB. Both encode to the
     same `.bin`
5. `fm24ctl encode <in.json> <out.bin>` converts a config to a `.bin` input
   (no game needed).
   `fm24ctl list-teams <out.txt>` writes `uniqueId<TAB>short name` lines for
   every club's first team, the teams `database_findTeam` resolves names to
   (`database_listTeams`, ~1 s). The viewer loads it in the background the
   first time either team is edited, to fill its filterable picker; a
   picked team is submitted by ID, so clubs that share a short name resolve.
   `fm24ctl replace-home|replace-away <in.json> <team> <out.json> [--full]`
   rebuilds either side (`src/dll/job_replace.c`). `<team>` is a
   `db::TEAM` unique ID or a club short name (`database_findTeam`). The
   input's edits are re-applied as in `run`, except those to the replaced
   team. The output's `home_team`/`away_team` headers preserve both sides'
   overrides for later replacements and per-seed builds. For each build the
   seed builder swaps the teams in three places and restores them afterwards
   (`swapTeams` in `seed_build.c`):
   - the fixture's home (`+0x08`) and away (`+0x10`) pointers;
   - the copies of the fixture among the human's matches
     (`[fm+0x6374488]+0x130`). Without these the builder doesn't give the human
     manager control (`sub_143456590`) and picks the human's side itself;
   - each original team's next-match opponent (`TEAM+0xA0` -> `+0x38`).
   All required pointers are collected before mutation; exceeding the fixed
   swap capacity fails without changing the game. The viewer shares one
   replacement worker between the home and away editors.
6. `fm24ctl probe [path\to\fm.exe]` reports where another build of `fm.exe`
   (e.g. the Epic Games one) has the code and data the harness uses. It reads
   the file only: nothing is injected, and the game isn't needed (without a
   path it probes the running game's file). It prints a summary and writes
   `probe.txt` next to `fm24ctl.exe`:
   - every function the harness calls, reads data through, or labels
     configs by (100), with its address in that build, the shift from the
     Steam address and how many of its fixed bytes match;
   - the six data addresses of `game.h`, read from an instruction that
     references them;
   - the most common shifts.

   The patterns are the Steam build's functions with every byte that moves
   between builds masked: RIP-relative operands, calls and jumps leaving the
   function, and address-sized immediates and displacements
   (`tools/probe/gen_probe_patterns.py`). Look-alike functions are told apart
   by the shift most functions share, and the one `jmp` thunk by its target.
   On the Steam build all 100 functions are identical at shift 0.

CSV columns: `seed, home_goals, away_goals, clock_ticks (4/s), finished,
rng_final, ms`.

## Match configs (`fm24-match-config/1`)

`fields` lists every write the game's serialiser makes, in order, so encoding
is lossless. The encoder reads only `format` and each field's value; every
other key is an annotation, except `home_team` and `away_team` (unique IDs of
teams substituted for the fixture's original sides), which `run` passes
to the DLL. `site`, `p`, `member` and the header's `paths`
and `bytes` are written only with `get-next-fixture --full`. User-facing
field meanings are in `docs/FIXTURE.md`.

- **Value:** each field has exactly one: `u8`, `u16`, `u32`, `u64`, `str`
  (u32 length + UTF-8), `hex`, or `u8map`.
- **Location** (`--full`): `site` gives the `<call site>/<caller>` RVAs of
  the write (named parts are in `src/dll/sites.h`), and `p` indexes the
  `paths` table of serialiser call chains.
- **Seed:** the seed field has `"role": "seed"`.
- **Labels:** blocks carry a `"name"` (`match`, `referee`, `venue`, `fixture`,
  `home.player[3]`, `away.manager`, `home.backroom[2]`, ...), and player
  blocks also carry `"player"`. Fields whose meaning is known are named too:
  `.person_uid`, `.ratings`, `.ability` (caps every role rating),
  `.leadership_value`, `.role_rating[winger@AMR]` with its `.role` and
  `.position` bytes, and the team counts and flags. The weather
  (`match.weather.*`), the venue's fields (`venue.pitch.*`, `.stadium.*`,
  `.attendance`, `.stand[i].*`, `.section[i].*`, ...) and the attendance's
  copies in `fixture` and `fixture_extra` are named by write site or member
  (`labelVenueField` in `src/dll/labels.c`, sites in `src/dll/sites.h`).
- **Tactics:** each manager's tactic is under `<side>.manager.tactic`
  (`docs/PLAN.md`, Phase 15). It holds `takers`, the active `plan[0]` and a
  second `plan[1]`, `set_pieces`, and `formation[i]`. A plan names
  `.mentality` (1-7), `.team_instructions[0..2]` and each formation slot's
  `.slot[i].position`, `.slot[i].role` (role bit plus a duty bit) and
  `.slot[i].player_instructions[role].bits[0..1]`.
- **Members** (`--full`): most fields also carry `"member"`, the game object member the
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

- Only the Steam (PE timestamp 1744804480) and Epic Games (1744804949)
  builds are supported; the DLL refuses any other. `fm24ctl probe` maps
  another build's addresses as a first step towards supporting it.
- On the Epic build, configs lack five tactic plan labels (`.mentality`,
  `.team_instructions`, `.slot[i].position`, `.slot[i].role`,
  `.slot[i].player_instructions[role]`) and, with `--full`, the `member`
  notes of functions in `.arch`: that code differs between the builds
  (`docs/PLAN.md`, Phase 20). Values are unaffected.

- With `--keep-build`, patching the seed in an input is not the game
  choosing that seed: pre-match data (attendance etc.) also depends on the
  build-time RNG.
- Rebuilding per seed needs the input to be the human manager's next
  fixture, in the same game state; once the match is played in the game,
  use `--keep-build`.
- One input = one fixture with fixed line-ups and tactics.
- A game-produced .bin and its JSON export differ in one header byte (offset
  21, a flag the deserialiser sets). Match results are identical.
- `get-next-fixture` (and every per-seed rebuild) sets one byte (`+0x46`) of the real
  fixture in the save and leave it set, as the game's own match setup
  probably does. The first build after loading a save therefore differs in
  that byte from later builds with the same seed.
