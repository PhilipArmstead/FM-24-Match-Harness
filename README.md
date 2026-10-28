FM24 match-engine harness
===

Run Football Manager 24's own match engine headlessly inside the running
game. Export a fixture, edit it, and simulate it across many random seeds
to compare tactics, players and match conditions.

The app includes a Windows UI and a command-line interface. Results are
saved as CSV files, with optional detailed player and team statistics.
Written in C17.

## Features

- Export the human manager's next fixture as editable JSON or binary data.
- Inspect squads, stored tactics, player condition and venue details.
- Maximise selected players' morale, condition and sharpness, and clear
  their fatigue.
- Replace players or teams and edit pitch, stadium and attendance values.
- Run a single seed, a range or repeated seeds, with parallel simulation.
- Automate experiments through the CLI and annotated fixture JSON.

## Requirements

- 64-bit Windows 10 or newer.
- A supported Steam or Epic Games build of FM24, using match engine 24.2.
  Compatibility is build-specific, not guaranteed for every release.
  Older Steam FM 24.2.4 support is experimental.
- For exporting and simulating: FM24 running with a save loaded, an upcoming
  human-managed fixture, and the game **idle**, not processing.

Existing JSON fixtures can be viewed and edited offline. Simulation, team
replacement and fetching players from the game require FM24.

## Download and quick start

1. Download `fm24ctl.exe` from the
   [latest release](https://github.com/PhilipArmstead/FM-24-Match-Harness/releases/latest).
2. Start FM24, load your save and leave the game idle before an upcoming
   fixture. Avoid interacting with FM24 while the harness is working.
3. Open `fm24ctl.exe`. It exports the next fixture to a temporary JSON file
   and opens the UI.
4. Save the fixture, make any edits, then use **Match > Simulation** to
   choose seeds and run matches. Results are saved beside the fixture.

For comparisons with the real match, export after submitting your team and
before continuing to the match. Keep the game at that point while running
your experiments.

### View a fixture

Drag a `.json` or `.bin` fixture onto `fm24ctl.exe`, or use **Open** in the
UI. JSON opens without the game; binary fixtures need FM24 running and idle.
Opening a file does not run a simulation or overwrite it.

The UI shows stored fixture data, not a live match or tactical editor.
Use the [fixture editing guide](docs/FIXTURE.md) for manual JSON edits;
change values without adding, removing or reordering fields.

### Command line

From PowerShell in the executable's folder:

```powershell
.\fm24ctl.exe get-next-fixture next-fixture.json
.\fm24ctl.exe run next-fixture.json 1-100 results.csv --detailed
```

This runs 100 matches and writes `results.csv`, `results-players.csv` and
`results-teams.csv`. By default, the AI handles in-match changes for both
teams.

Explicit numeric seeds rebuild the fixture in the current game state and
reapply your edits. Keep the exported fixture as the manager's upcoming
match. For older fixtures, `--keep-build` skips rebuilding, but leaves
pre-match values such as attendance and weather unchanged.

See the [CLI reference](docs/CLI.md) for commands, flags, output formats and
more examples.

## Troubleshooting

Keep FM24 idle and avoid clicking around in the game during simulations.
Restart FM24 after updating the harness if it reports that an older DLL
cannot be replaced.

For failures, check the console and `harness.log`, normally at
`%LOCALAPPDATA%\fm24-harness\harness.log`. Unsupported game builds are
refused.

[Report a bug or suggest a feature](https://github.com/PhilipArmstead/FM-24-Match-Harness/issues).
For bugs, include the harness version, game version and storefront,
reproduction steps, and relevant console or log output. For crashes,
include a crash dump if available.

## Development and contributions

See the [development guide](docs/DEVELOPMENT.md) for prerequisites, cloning,
building, tests and code-style guidelines. Keep contributions focused and
add regression tests for changed behaviour.

## License

The source code is licensed under
[GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html), as indicated
by its SPDX headers.

## Support

This work is free to download or modify. I'll never charge money for it.
If you've found it useful, I'd be grateful for a hot cup of tea or a pack
of biscuits.

[![Support me on Ko-fi](docs/kofi.png?v=1)](https://ko-fi.com/philarmstead)
