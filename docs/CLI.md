# CLI reference

`fm24ctl.exe` exports, edits, views and simulates FM24 fixtures.

Examples use PowerShell from the executable's directory. In Command Prompt,
use `fm24ctl.exe` instead of `.\fm24ctl.exe`. Quote paths and names containing
spaces. `<...>` means a required argument; `[...]` means optional.

## Requirements and conventions

Game-connected commands need Windows 10+, FM24 running with a save loaded,
and the game **idle**, not processing. Supported builds are Steam
(PE timestamp `1744804480`) and Epic Games (`1744804949`), match engine 24.2.
Exporting or rebuilding needs the human manager's upcoming fixture.

| Invocation                     | Needs FM24?                                 | Purpose                                    |
|--------------------------------|---------------------------------------------|--------------------------------------------|
| No arguments                   | Yes                                         | Export and open the next fixture           |
| `<fixture.json>`               | No, to view                                 | Open an existing JSON fixture              |
| `<fixture.bin>`                | Yes                                         | Decode and open an existing binary fixture |
| `get-next-fixture`             | Yes                                         | Export the next fixture                    |
| `run`                          | Yes                                         | Simulate one or more matches               |
| `encode`                       | No                                          | Convert fixture JSON to binary             |
| `replace-home`, `replace-away` | Yes                                         | Rebuild one side with another team         |
| `list-teams`                   | Yes                                         | Export team IDs and names                  |
| `get-player`                   | Yes                                         | Export a player's database values          |
| `inject-player`                | Only when given a player ID                 | Copy player values into a fixture          |
| `probe`                        | Only to discover an omitted executable path | Inspect a game executable on disk          |

Relative paths use the console's current directory. Create destination
directories first. Explicit output files and simulation results are overwritten
without prompting; do not use an input file as a results/profile path.
JSON simulation and team replacement, and player injection by ID, also need
write access beside `fm24ctl.exe` for temporary job inputs.

There is no global configuration file or global flag. Use command arguments
and editable fixture JSON; see [Editing a fixture file](FIXTURE.md) for its
fields and constraints. Flags take separate values (`--threads 4`, not
`--threads=4`); there are no short aliases or dedicated help/version flags.
An unknown command, including `--help`, prints usage and returns exit code 2.

## Export the next fixture: `get-next-fixture`

```text
fm24ctl get-next-fixture <output.json|output.bin> [--seed N] [--full]
```

Build the human manager's next fixture and write editable JSON or binary match
data. The seed controls match randomness and pre-match values such as attendance,
officials, weather and per-player random values.

| Flag       | Default            | Meaning                                                                                                                           |
|------------|--------------------|-----------------------------------------------------------------------------------------------------------------------------------|
| `--seed N` | Clock-derived seed | Decimal integer from `0` to `4294967295`                                                                                          |
| `--full`   | Off                | Add research annotations to JSON: header `bytes`/`paths` and field `site`/`p`/`member`; no effect on binary data or match results |

Options follow the output path, in either order. For a reproducible comparison
with the real match, export after **Simulate Match**/**Submit Team** and before
**Continue**/**Go to Match**, then run before advancing the game. Before team
submission, the game may choose a different human line-up for each seed.

```powershell
.\fm24ctl.exe get-next-fixture next-fixture.json
.\fm24ctl.exe get-next-fixture next-fixture.json --seed 42 --full
.\fm24ctl.exe get-next-fixture next-fixture.bin --seed 42
```

## Simulate fixtures: `run`

```text
fm24ctl run <input.json|input.bin> [seeds] [output.csv]
    [--threads N] [--profile <samples.txt>] [--detailed]
    [--human-control] [--keep-build]
```

JSON is encoded automatically. The default output replaces the input's
extension with `.csv`; for example, `next-fixture.json` produces
`next-fixture.csv`. An explicit output path may be given with or without seeds.
Flags can appear anywhere after `run`.

### Seeds

| Pattern           | Meaning                                                            |
|-------------------|--------------------------------------------------------------------|
| Omitted or `keep` | Play the stored fixture once with its own seed, without rebuilding |
| `42`              | Play seed 42                                                       |
| `1-1000`          | Play every seed in the inclusive range                             |
| `1,5,10-20`       | Combine individual seeds and ranges, in that order                 |
| `42,42,42`        | Repeat a seed; duplicates are retained                             |

Numeric seeds are decimal unsigned 32-bit integers (`0`-`4294967295`).
Do not include spaces or mix `keep` with numeric seeds. The pattern must fit
in 255 characters. Ranges must not descend and must satisfy `last - first <= 10000000`
(at most 10000001 matches per range). Large batches can take hours.

With explicit numeric seeds, the game rebuilds the fixture for each seed and
re-applies your edits. This reproduces its pre-match randomness, but requires
the input to match the human manager's next fixture in the current game state.
A seed fails if the rebuilt structure cannot accommodate an edit, such as an
edited player no longer in the line-up. See `harness.log` for the field.

Use `--keep-build` for older or otherwise non-rebuildable inputs. It changes
only the seed, leaving the stored attendance, weather and other pre-match
values unchanged; other seeds can therefore differ from the game's results.

A positional argument made solely of digits, commas and hyphens is treated
as seeds, not an output path. Use a filename such as `2024.csv`, not `2024`.

### Flags

| Flag                      | Default                                          | Meaning                                                                                                              |
|---------------------------|--------------------------------------------------|----------------------------------------------------------------------------------------------------------------------|
| `--threads N`             | Logical CPU count minus one, clamped to `1`-`16` | Set `1`-`16` workers; at most one worker per requested match                                                         |
| `--profile <samples.txt>` | Off                                              | Sample worker 0's stack about every 1 ms into a text file, with phase/timing comments; not a profile of every worker |
| `--detailed`              | Off                                              | Also write averaged player and team statistics                                                                       |
| `--human-control`         | Off                                              | Preserve the input's human-control and match-plan flags instead of handing human-managed teams to the AI             |
| `--keep-build`            | Off                                              | Skip per-seed rebuilding; patch only the seed into the input                                                         |

By default, the AI handles in-match changes for both teams. With
`--human-control`, a human-controlled side has nobody making decisions and
may not substitute even an injured player. AI-managed teams are unaffected.
Thread count does not change results; CSV rows remain in seed-list order.

```powershell
.\fm24ctl.exe run next-fixture.json
.\fm24ctl.exe run next-fixture.json results.csv
.\fm24ctl.exe run next-fixture.json keep results.csv
.\fm24ctl.exe run next-fixture.json 1
.\fm24ctl.exe run next-fixture.json 1-10 results.csv --threads 4
.\fm24ctl.exe run next-fixture.json 42,99,300-400 lots-of-results.csv --detailed
.\fm24ctl.exe run next-fixture.bin 1-100 --keep-build --human-control
.\fm24ctl.exe run next-fixture.json 42 --threads 1 --profile samples.txt
```

### Results

The console prints the first ten results, win/draw/loss totals and average
goals. The main CSV has one row per completed match:

| Column                     | Meaning                                                   |
|----------------------------|-----------------------------------------------------------|
| `seed`                     | Match seed                                                |
| `home_goals`, `away_goals` | Final score                                               |
| `clock_ticks`              | Match clock, four ticks per second                        |
| `finished`                 | Whether the engine reports the match finished (`1` = yes) |
| `rng_final`                | Final random-number state, useful for exact comparisons   |
| `ms`                       | Match load/play elapsed time in milliseconds              |

For `results.csv`, `--detailed` adds `results-players.csv` and
`results-teams.csv`. A trailing `.csv` is removed before adding these suffixes;
otherwise they are appended to the output name. Reports include the engine's
player and team statistics, averaged per match: goals, assists, possession,
shots, xG/xA, passing, defending, cards, goalkeeping, distance and more.
Player average ratings are averaged over appearances; substitutes are included
and rows are grouped by unique ID even if squad order changes.

Detailed reports are written only after all seeds succeed. A failed run may
leave a partial main CSV; output-write failures return an error.

## Convert JSON to binary: `encode`

```text
fm24ctl encode <input.json> <output.bin>
```

Encode a fixture offline; no flags. Invalid field values produce an error
with the field number. The console reports the byte count on success.
`run` already performs this conversion for JSON inputs.

Team replacement metadata (`home_team`/`away_team`) is not stored in the
binary data. Keep the JSON for later per-seed rebuilds of replaced teams;
use `keep` or `--keep-build` to play their stored binary fixture.

```powershell
.\fm24ctl.exe encode next-fixture.json next-fixture.bin
```

## Replace a team: `replace-home`, `replace-away`

```text
fm24ctl replace-home <input.json> <team ID or club name> <output.json> [--full]
fm24ctl replace-away <input.json> <team ID or club name> <output.json> [--full]
```

Have the game choose a new squad and tactic for one side, using the input's
seed. Requires the next fixture in the same game state, as with per-seed
rebuilding. Both input and output must be JSON.

Use the team's unique ID or the club's in-game short name (for example,
`"Man City"`). Ambiguous names report candidate IDs; use `list-teams` to find
an exact ID. The replaced side's edits are discarded; other edits are kept.
Both team overrides remain in the JSON for subsequent replacements and runs.

`--full`, placed last, adds the same research annotations as
`get-next-fixture --full`; it defaults to off.
See [Changing teams](FIXTURE.md#changing-teams) for details.

```powershell
.\fm24ctl.exe replace-away next-fixture.json "Luton Town" luton.json
.\fm24ctl.exe replace-away next-fixture.json 677 luton.json --full
.\fm24ctl.exe replace-home luton.json "Norwich City" norwich-v-luton.json
.\fm24ctl.exe run norwich-v-luton.json 1-100
```

## Export team IDs: `list-teams`

```text
fm24ctl list-teams <output.txt>
```

Write a UTF-8 text file of every club's first team in the loaded database,
one `uniqueId<TAB>short name` line per team. No flags.

```powershell
.\fm24ctl.exe list-teams teams.txt
```

## Export player values: `get-player`

```text
fm24ctl get-player <person unique ID> <output.json>
```

Export a player's database values as an editable `fm24-player/1` JSON file
for `inject-player`. The ID is a decimal integer from `1` to `4294967295`.
The player may belong to any club in the loaded database. Age is calculated
on the human manager's next match day, so an upcoming fixture is required.
No flags.

```powershell
.\fm24ctl.exe get-player 2000040347 torre-values.json
```

## Copy a player's values into a slot: `inject-player`

```text
fm24ctl inject-player <input.json> <home|away>.player[N]
    <person unique ID|player.json> <output.json> [--allow-duplicate]
```

Patch an existing player block without changing squad order or fixture
structure. `N` is the block's zero-based index, including substitutes; it
must exist in the fixture. A numeric ID is fetched from FM24 first; a
`get-player` JSON file works offline. Input and output must be JSON.

Copies database values such as ratings, condition, sharpness, fatigue,
personality, traits, age, height, weight and identity. It does not copy
derived ability/role ratings, caps or squad number. See
[Putting another player in a slot](FIXTURE.md#putting-another-player-in-a-slot)
for the exact scope.

By default, all three unique-ID copies are replaced and duplicate players
elsewhere in the fixture are rejected. `--allow-duplicate`, placed last,
injects the player even if they are already in the fixture, so the same
person can play twice (for example on both sides). Detailed statistics list
them once per side, under their own name. The flag defaults to off.

```powershell
.\fm24ctl.exe inject-player next-fixture.json "home.player[10]" 2000040347 torre.json
.\fm24ctl.exe run torre.json 1-100
.\fm24ctl.exe inject-player next-fixture.json "away.player[0]" torre-values.json edited.json --allow-duplicate
```

## Open the viewer

```text
fm24ctl
fm24ctl <fixture.json|fixture.bin>
```

With no arguments, export the next fixture to `temp-fixture.json` in the
Windows temporary directory and open it. A failed export prints an error
and exits without opening a window. The summary's **Click here** link saves
the temporary fixture into the current directory as
`<home-team>-v-<away-team>-<unix-timestamp>.json`, without overwriting a file.

With exactly one JSON or binary path, open that fixture without running a
match. JSON viewing is offline. Binary viewing uses FM24 to decode the
stored input, without building or playing a new fixture. Dragging a file
onto the executable in Explorer is equivalent. Neither mode accepts flags;
use the window's **Simulation** tab or `run` to simulate.

```powershell
.\fm24ctl.exe
.\fm24ctl.exe next-fixture.json
.\fm24ctl.exe next-fixture.bin
```

See the [viewer notes](../README.md#view-a-fixture) for its controls and
display limits.

## Inspect another game build: `probe`

```text
fm24ctl probe [path to fm.exe]
```

Read an executable on disk and locate the functions/data used by the harness.
If the path is omitted, discover it from the running `fm.exe`. With an
explicit path, the game need not be running. No flags.

Print a summary and write `probe.txt` beside `fm24ctl.exe`, overwriting the
previous report. The executable is read-only; nothing is injected or patched.
This is a diagnostic report, not automatic support for another game build.
If the report cannot be created, it is printed to the console and the
command returns failure.

```powershell
.\fm24ctl.exe probe
.\fm24ctl.exe probe "C:\Games\Football Manager 2024\fm.exe"
```

## Errors, logs and timeouts

| Exit code | Meaning                      |
|-----------|------------------------------|
| `0`       | Success                      |
| `1`       | Operation failed             |
| `2`       | Invalid command or arguments |

Some invalid values are detected by the game-side job parser and return
`1` rather than `2`. Check the console message as well as the exit code.

Game-connected commands inject the harness automatically when needed.
An already-loaded DLL of the same protocol is reused. Otherwise a matching
`fm24-harness-<version>.dll` beside the runner takes precedence over the
embedded copy, which is extracted into `%LOCALAPPDATA%\fm24-harness\`.
`harness.log` is beside the DLL actually loaded, normally in that directory.
If another build of the same protocol is already loaded, restart FM24 or
follow the [development rebuild instructions](DEVELOPMENT.md#building).

The DLL handles one job at a time; other runners wait. `run` waits up to
six hours, and other game jobs up to ten minutes, including queue time.
These limits are not command-line configurable. A timeout stops the runner
waiting, **not** the DLL's work; later jobs still wait for it.

For build scripts, regression-test commands and toolchain configuration,
see [Development](DEVELOPMENT.md).
