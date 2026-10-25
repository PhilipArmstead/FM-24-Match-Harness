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
| `develop`                      | Yes; **changes the game**                   | Simulate a player's training and growth    |
| `get-development`              | Yes                                         | Export a player's development inputs       |
| `seed-development`             | Yes; patches game code until it fires       | Fix the game's next development seed       |
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

Before match day the game hasn't yet scheduled you to attend the match, so it
builds your side as it would an AI side: the AI picks your line-up and tactic.
The export still succeeds, but the result line ends with a `warning:` saying so.
Export again on match day to get your own line-up and tactic.

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

## Simulate a player's development: `develop`

```text
fm24ctl develop <person unique ID|development.json> <months> <seeds> <output.csv>
    [--per-seed-breakdown]
```

Runs the game's own monthly training and development routine on a player,
month after month, with each month starting from the previous month's result.
It uses the player's current training and club. While each month runs, the
game's date moves on to that month's development day (and back afterwards),
so the player ages and trains on the weeks being predicted, with the club's
rest days and season dates for those weeks. Month 1 ends on the player's next
development day (day `id % 28 + 1` of a month), with the whole weeks of
training before it; later months have 4 or 5 weeks (52 a year). `months` is
`1` to `600`. Seeds take the same forms as `run` (`42`, `1-100`,
`1,5,10-20`, not `keep`); each seed is a separate simulation from the
player's current values.

| Flag                   | Default | Meaning                                                              |
|------------------------|---------|----------------------------------------------------------------------|
| `--per-seed-breakdown` | Off     | Write one row per seed and month instead of the average of the seeds |

The flag may appear anywhere after `develop`.

Given a development file (see `get-development`) instead of an ID, it first
sets the player's values to the file's, simulates from them, and afterwards
puts the player's own values back.

**Save the game first, and reload it afterwards without saving.** The routine
works on the game's real player and passes them to other parts of the game
(history, news, contracts). After each seed and at the end, the tool puts
back the player's ratings, current and potential ability, training progress,
height, weight, reputation, personality and age, so every seed starts from
them. Everything else the game changed stays, including state carried from one
seed to the next (such as the player's development history). For example, a
youth player sends one training report to the inbox for every simulated month
of every seed, so 500 seeds mean 500 inbox items. If the game misbehaves after
reloading, restart FM24.
The game pauses while the simulation runs (about a second for 500 seeds of
one month).

```powershell
.\fm24ctl.exe develop 2000147523 24 1-100 kellyman.csv
.\fm24ctl.exe develop 2000147523 24 1-100 kellyman-seeds.csv --per-seed-breakdown
```

By default the CSV has one row per month, starting with month `0` (the
starting state), with every value averaged over the seeds to three decimal
places: `month`, `seeds` (how many seeds each row averages), then the player
values below. With `--per-seed-breakdown` it has one row per seed and month
instead, with whole values: `seed`, `month`, then the same player values.

| Columns | Meaning |
|---|---|
| `current_ability`, `potential_ability` | CA and PA |
| `weight`, `height` | kg, cm |
| `progress_strength` … `progress_set_pieces` (11) | training progress, `-100` to `50` |
| the 69 ratings by name | the game's 1–100 scale (divide by 5 for the 1–20 scale shown in the game); positions 1–20 |

Players who are also staff, and players the game does not develop, are
refused.

## Export a player's development inputs: `get-development`

```text
fm24ctl get-development <person unique ID> [output.json]
```

Writes the values the development routine reads from a player as an
editable `fm24-development/1` file, in the same field syntax as a fixture
(change values only; don't add, remove or reorder fields or ratings). Remove
a field to leave the player's own value alone. Nothing in the game is changed.

The header comments give the game date of the export (as a day of the year)
and the day of the month the player develops on.
A `//` comment above each field gives its range or meaning (the table below
has them all). Add your own `//` comments anywhere between values: `develop`
skips them, as do all of fm24ctl's JSON inputs. Fields the game computes as it
plays (training progress, condition, morale, the development counters and
flags) come last, under `// here be dragons`: leave them alone to simulate
training.

Without an output file, it writes to the current directory, named after the
player's common name, or else forename and surname, in lowercase ASCII
letters joined by hyphens: accents are dropped (`Álex Moreno` →
`alex-moreno.json`), as are apostrophes (`Tommi O'Reilly` →
`tommi-oreilly.json`). A name with no Latin letters gives
`player-<unique ID>.json`. An existing file is overwritten.

```powershell
.\fm24ctl.exe get-development 2000128236          # harvey-rhoades.json
# edit harvey-rhoades.json, then
.\fm24ctl.exe develop harvey-rhoades.json 12 1-200 rhoades-edited.csv
```

| Field | Values |
|---|---|
| `person_uid` | the player (don't change) |
| `current_ability`, `potential_ability` | 1–200 |
| `ratings` | the 69 ratings by name: positions 1–20, attributes 1–100 |
| `training_progress.<category>` (11) | `s8`, −100 to 50 |
| `training_intensity` | 0 or 1 normal, 2 double, 254 half, 255 the team's workload |
| `training_focus` | the additional focus by ID (below), 255 none |
| `condition`, `sharpness` | 0–10000 |
| `fatigue` | `s16`; 0 is fresh |
| `morale` | 0–255 |
| `height`, `weight` | cm, kg |
| `personality.<trait>` (8) | 1–20 |
| `date_of_birth.day`, `date_of_birth.year` | day of the year from 1, and the year |
| `training_happiness` | 0–100 (only if the player has a training happiness record) |
| `training_happiness.unknown_08` | changes at the weekly training review; meaning unknown |
| `last_development.gains`, `last_development.other_gains` | `s16`; rating points from the last development |
| `weak_foot_progress` | 0–65535; the weaker foot gains 1 above 10 × its rating + 500 |
| `development_period.*` (4) | a total and count moved to `last_*` each period; meaning unknown |
| `rating_progress.important_matches`, `rating_progress.determination` | 0–65535; the rating gains 1 at a threshold |
| `personality_progress.pressure`, `personality_progress.professionalism` | 0–65535; the trait gains 1 at a threshold |
| `played_level`, `played_level.appearances` | best club reputation ÷ 10 with 11+ appearances in a season; appearances there, 0–120 |
| `development_flags` | `0x40` late developer, `0x200` late growth due, `0x100` late growth had |
| `training_rating` | 0–20 |
| `ability_carry` | `hex`: the 4 bytes of a float, the CA growth carried to the next development |
| `coaching` | the team's coaching rating per category (11), 0–100: best coach + training facilities − 10 |
| `coach_workload` | the team's coach workload per category (11), 0–100; progress builds `(100 − workload)%` as fast |
| `training_facilities` | the club's training facilities, 1–20 |
| `schedule.week1` … `schedule.weekN` | `sessions`: 21 training sessions, `day1.slot1` to `day7.slot3`; up to 52 weeks |

`get-development` writes the schedule the player's team trains on this week
as `schedule.week1`. `develop` trains every simulated week on the file's
weeks in turn (week 1, 2, … N, then 1 again, restarting for each seed),
instead of the club's own schedules. Remove every `schedule.week` field to
keep the club's schedules. Add `schedule.week2` and so on (numbered in order)
for a rotation. Sessions are named as in the game's `training_sessions.jsb`
(case doesn't matter), e.g. `"Team Physical"`, `"Team Rest"`,
`"Match Prep Match Practice"`, `"Unit Technical Chance Conversion"`. The
game's short aliases work too (`"Match Practice"`, `"Team Bonding"`). An
unknown name is an error. A number is taken as a session ID.

```json
{"name": "schedule.week1", "sessions": {
  "day1.slot1": "Team Physical", "day1.slot2": "Team Outfield", "day1.slot3": "Team Rest",
  ...
  "day7.slot1": "Team Rest", "day7.slot2": "Team Rest", "day7.slot3": "Team Rest"
}}
```

`training_focus` is the player's additional focus. Right after loading a
save the game hasn't set it up yet (it does at the first training day), so
`get-development` leaves the field out; add it yourself and `develop` gives
the player the focus for the run. Names marked * were confirmed in the
game; the others are inferred from the attributes.

| ID | Focus | Attributes |
|---|---|---|
| 0–4 | injury recovery (Quickness, Agility and Balance, Strength, Stamina, Physical) | only offered after an injury; no effect in `develop` |
| 5, 6, 7 | Free Kicks, Corners, Penalty Taking | each + Technique |
| 8 | Long Throws | Long Throws |
| 9 | Quickness* | Pace, Acceleration |
| 10 | Agility and Balance | Agility, Balance |
| 11 | Strength | Strength, Jumping Reach (+ Aerial Reach for goalkeepers) |
| 12 | Endurance* | Stamina, Work Rate |
| 13 | Defensive Positioning | Marking, Positioning, Decisions |
| 14 | Attacking Movement | Off The Ball, Anticipation, Decisions |
| 15 | Shooting* | Finishing, Long Shots, Technique |
| 16 | Passing | Vision, Passing, Technique |
| 17 | Decisions | Composure, Decisions |
| 18 | Crossing | Crossing, Technique |
| 19 | Ball Control | First Touch, Dribbling, Technique |
| 20 | Heading | Heading, Bravery |
| 21–25 | Goalkeeping | Shot Stopping, Positioning, Handling, One on Ones, Distribution |
| 26 | Playmaking | First Touch, Passing, Vision |

`coaching`, `coach_workload` and `training_facilities` belong to the
player's team and club, not the player: `develop` sets them for the run and
puts them back afterwards. The game works `coaching` out from the coaches'
attributes and assignments, the training facilities and the squad's
schedules (see the knowledge base, section 18); `develop` doesn't, so
`coaching` already includes the facilities at export. If you change
`training_facilities`, change `coaching` by the same amount to match. Teams
without a coaching record (it is made when the game first rates the team's
coaches) don't export these two, and a file with them is refused.

Not in the file yet: rest (the club's rest list) and position/role/duty
training.

## Seed the game's own development: `seed-development`

```text
fm24ctl seed-development <person unique ID> <seed> [<seeds> <output.csv>]
```

Makes the game's own next development of that player draw its random numbers
from `seed`, exactly as `develop` does for that seed. Advance past the
player's development day, then compare their values (`get-player`) with
`develop`'s month `1` row for the same seed, run from the same save. It
checks how closely `develop` reproduces the game.

```powershell
.\fm24ctl.exe develop 2000128236 1 7 predicted.csv --per-seed-breakdown   # from a save; then reload it
.\fm24ctl.exe seed-development 2000128236 7                              # arm, then advance in the game
.\fm24ctl.exe get-player 2000128236 actual.json
```

With `<seeds>` and `<output.csv>`, the hook first runs `develop`'s simulation
of that very development, one month for each of those seeds, on the game's
own thread at the moment the game develops the player, and writes `develop`'s
CSV with one row per seed (as `--per-seed-breakdown`, so each seed can be
compared). It then puts the player's simulated values back and lets the game's call
run. This rules out differences of date, state or thread between `develop`
and the game; the player's other side effects (see `develop`) still apply.

It changes game code: the daily scheduler's two calls of the development
routine are redirected through a small hook until that player develops, then
put back. Other players develop normally. Running it again before then
replaces the player and seed. Reloading a save keeps the hook armed; it lasts
until the player develops or FM24 exits.

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
