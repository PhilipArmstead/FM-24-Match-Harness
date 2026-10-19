FM24 match-engine harness
===

Runs Football Manager 24's own match engine (`GAME_MATCH__Run`) headlessly,
inside the running game. You provide a match config--exportable from the
game--and run it across thousands of different seeds or the same one repeatedly.

The output is a CSV showing the results of all matches played.

Written in C17.

> NOTE: this is for Windows 10 or newer only. Steam and Epic Games compatible.

 # How to use?

- You must be running Football Manager 24, with match engine v24.2 on Windows.
- You must have an upcoming fixture.

## UI

- Opening the UI creates a temporary file based on your next fixture.
  - Opening the app by dragging a fixture file on to it will open the file.
  - You can save this or open an existing fixture file.
- You can modify the condition and morale of players in the fixture.
- You can swap players and teams in the fixture.
- You can modify venue/attendnace details in the fixture.
- You can run the fixture with a single seed or a range of seeds and save
   detailed output.

## CLI

See the [CLI reference](docs/CLI.md) for every console command, flag,
configuration default and usage example. This is useful if you're interacting
with the harness using Python or if you're more comfortable in the command line
than the UI.

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

# Troubleshooting

I've had reports of FM24 crashing while matches are simulating. The game must
be in an idle state and, ideally, you won't be clicking around in it. If you
encounter errors, send me a crash dump and I will investigate.

If you run in to other problems, please
[open an issue](https://github.com/PhilipArmstead/FM-24-Match-Harness/issues).

# Development

See [technical details here](./docs/DEVELOPMENT.md).

# Support

This work is released for free for anybody to download or modify. I'll never
charge money for it.

That said, a lot of work went in to this so, if you think that's worth a couple
of quid, I'd be extremely grateful for a hot cup of tea or pack of biscuits.

[![Support me on Ko-fi](docs/kofi.png?v=1)](https://ko-fi.com/philarmstead)
