FM24 match-engine harness
===

Runs Football Manager 24's own match engine (`GAME_MATCH__Run`) headlessly,
inside the running game. You provide a match config--exportable from the
game--and run it across thousands of different seeds or the same one repeatedly.

The output is a CSV showing the results of all matches played.

Written in C17.

> NOTE: this is for Windows 10 or newer only. Steam and Epic Games compatible.

# How does it work?

There are two files shipped in this bundle:

- **`fm24-harness-<version>.dll`** (`src/dll/`): injected into `fm.exe`. It checks
  the game build, then runs jobs: play matches, build the next fixture,
  export configs.
- **`fm24ctl.exe`** (`src/runner/`, `src/ui/`): exports fixtures, passes them
  to seeded jobs and opens fixtures in a native Windows viewer.

# How to use?

- You must be running Football Manager 24, with match engine v24.2 on Windows.
- You must have an upcoming fixture.

## Export the next fixture

If you're using Powershell, run `.\fm24ctl get-next-fixture next-fixture.json`  
If you're using Command Prompt,
run `fm24ctl.exe get-next-fixture next-fixture.json`

This will dump the internal **NEXT_FIXTURE** object in to an editable JSON
object. You can name it whatever you like and also optionally specify a seed via
the `--seed <N>` flag. Besides the match itself, the seed decides some pre-match
details: crowd attendance, the officials, the weather and a random value per
player.

If you're trying to replicate a match that the game itself is about to play, 
you need to export after clicking "Simulate Match"/"Submit Team" but before
clicking "Continue"/"Go to Match". If you don't care about the exact result of
the game's real match, you can export the fixture whenever.

## Run the next fixture

If you're using Powershell, run `.\fm24ctl run next-fixture.json`  
If you're using Command Prompt, run `fm24ctl.exe run next-fixture.json`

This will run the fixture using the exported settings and generated seed once.

You can run multiple times or with a new seed by specifying the seed flag.
A given seed plays exactly as the game would with that seed, including the
result and the statistics:

```
.\fm24ctl run next-fixture.json 1
.\fm24ctl run next-fixture.json 1-10
.\fm24ctl run next-fixture.json 42,99,300-400
```

> Note: fixtures run on threads but they take around ~4s to execute so don't
> set the range of seeds too high if you're not prepared to wait.

Results are output in a CSV file whose name matches your input file, or you can
specify your own:

```
.\fm24ctl run next-fixture.json results.csv
.\fm24ctl run next-fixture.json 1-10 results.csv
.\fm24ctl run next-fixture.json 42,99,300-400 lots-of-results.csv
```

If you want to see more detailed outputs, add the `--detailed` flag to the end
of a `run` command and check the extra CSVs generated: one for players and one
for teams, with each statistic averaged over all the seeds. They match the
game's own match statistics screens.

## Replace either team

Have the game rebuild either side of your next fixture, keeping the edits
you made to the rest of the file:

```
.m24ctl replace-away next-fixture.json "Luton Town" luton.json
.m24ctl replace-away next-fixture.json 677 luton.json
.m24ctl run luton.json 1-100
```

Use `fm24ctl replace-home next-fixture.json "Norwich City" norwich.json` to
replace the home team instead. Name the team by its club's short name as the
game shows it ("Man City") or by its unique ID. The game picks the new side's
squad and tactic; the other side and its edits are kept. Both replacements
are retained when you replace the other side or run another seed. See
[docs/FIXTURE.md](docs/FIXTURE.md#changing-teams) for the details.

## View a fixture

Launch `fm24ctl.exe` without arguments to export the next fixture from the
running game to `temp-fixture.json` in the operating system's temporary
directory and open it in the viewer. If the export fails, the reason is
printed to the console and the application exits without opening a window.

The summary warns that this temporary fixture is not saved. Select **Click
here** (or focus the summary and press Enter) to copy the JSON into the
current working directory as `<home-team>-v-<away-team>-<unix-timestamp>.json`.
Team names are lower case, spaces become hyphens, and non-printable,
non-ASCII and Windows-invalid filename characters are removed. Existing
files are never overwritten. After export the summary shows the saved
filename. If the temporary file is gone, it instead says **Fixture cannot
be found; restart the app**.

Pass the fixture path as the first and only argument, without `run`:

```powershell
.\fm24ctl.exe next-fixture.json
.\fm24ctl.exe next-fixture.bin
```

> Note: you can also accomplish this by dragging the file on to the
> application in Explorer.

This opens a fixture window, with both teams' pitch diagrams, stored tactic
names, formation shapes, positions, roles and duties. Player tables include
morale, condition, sharpness and signed fatigue. The **Squads**
tab lists substitutes beneath the known starters, with `Sub` in the Position
column and blank Slot, Role and Duty cells.

The **Simulation** tab runs the viewed fixture using the existing match job:
choose a thread count, enter a seed pattern such as `1`, `42`, `1-1000`,
`1,3,5` or `1,4,7-70`, optionally select **Generate detailed reports?**, then
click **Simulate fixture**. Threads default to the logical CPU count minus
one, with a minimum of 1 and the existing harness cap of 16; that default is
also the field's maximum. The seed pattern defaults to `keep` (the fixture's
own seed). The window remains responsive while running, but another simulation
cannot be started from this window and it cannot be closed until the run finishes.
Completion or failure appears in the tab, with results and errors also
printed to the console.

Results use the fixture's path with a `.csv` extension; detailed reports
add `-players.csv` and `-teams.csv`. Running again replaces the previous result files.

JSON viewing does not need FM24. Binary viewing requires the supported game
running and idle: the harness decodes and re-exports the input, without
building a new fixture, preparing the match or playing it. The viewer does
not modify the input file unless you explicitly save over it.

**Data limits:** the human-controlled side's starting XI maps to its first
11 players. AI squad order is different, so its 11 tactic rows show
`<Unknown>` players, followed by its full squad with blank Slot, Position,
Role and Duty cells. Weather and the
league/cup type codes are not mapped yet; the competition name is displayed,
and `Friendly` is recognised. Date/time is shown from the JSON export's
advisory source annotation, converted from its one-based day of year to
`YYYY-MM-DD HH:MM` with Gregorian leap years; binary dates without that
annotation are unknown. Only the referee is shown, not the other officials.

## Experiments

There's a few things you might want to do with this feature:

1. Try many variations of your tactic played over thousands of games, measuring
   gains to find the best instructions
2. Repeatedly tweak the attributes of players to find what impacts the result
   the most significantly

I suggest knocking together a Python script repeatedly run the fixture against a
few hundred seeds, tweaking it and running again, then tabulating the results.

It should be trivial for an LLM to read the fixture JSON file--which I've taken
great pains to annotate--and modify it on a loop until it's iterated over all
the things you might care about. And it should be cheap too; it's the Python
script and my DLL doing most of the work, not your agent.

# Feature suggestions

Got an idea for how I can enhance this? Maybe even more detailed outputs? Better
annotated inputs or additional viewer features?
[Open an issue](https://github.com/PhilipArmstead/FM-24-Match-Harness/issues)
and ask kindly.

# Development

See [technical details here](./docs/DEVELOPMENT.md).

# Support

This work is released for free for anybody to download or modify. I'll never
charge money for it.

That said, a lot of work went in to this so, if you think that's worth a couple
of quid, I'd be extremely grateful for a hot cup of tea or pack of biscuits.

[![Support me on Ko-fi](docs/kofi.png?v=1)](https://ko-fi.com/philarmstead)
