FM24 match-engine harness
===

Runs Football Manager 24's own match engine (`GAME_MATCH__Run`) headlessly,
inside the running game. You provide a match config--exportable from the
game--and run it across thousands of different seeds or the same one repeatedly.

The output is a CSV showing the results of all matches played.

Written in C17.

> NOTE: this is for Windows only.

# How does it work?

There are two artifacts produced by this build:

- **`fm24_harness_p<N>.dll`** (`src/dll/`): injected into `fm.exe`. It checks
  the game build, then runs jobs: play matches, build the next fixture,
  export configs.
- **`fm24ctl.exe`** (`src/runner/`): injects the DLL, exports fixtures in to
  editable JSON files and passes them to seeded jobs.

# How to use?

- You must be running Football Manager 24, with match engine v24.2 on Windows.
- You must have an upcoming fixture.

## Export the next fixture

If you're using Powershell, run `.\fm24ctl get-next-fixture next-fixture.json`  
If you're using Command Prompt,
run `fm24ctl.exe get-next-fixture next-fixture.json`

This will dump the internal **NEXT_FIXTURE** object in to an editable JSON
object. You can name it whatever you like and also optionally specify a seed via
the `--seed <N>` flag. The only thing the seed determines is crowd attendance.

## Run the next fixture

If you're using Powershell, run `.\fm24ctl run next-fixture.json`  
If you're using Command Prompt, run `fm24ctl.exe run next-fixture.json`

This will run the fixture using the exported settings and generated seed once.

You can run multiple times or with a new seed by specifying the seed flag:

```
.\fm24ctl run next-fixture.json 1
.\fm24ctl run next-fixture.json 1-10
.\fm24ctl run next-fixture.json 42,99,300-400
```

> Note: fixtures run on threads but they take around ~4s to execute so don't
> the range of seeds too high if you're not prepared to wait.

Results are output in a CSV file whose name matches your input file, or you can
specify your own:

```
.\fm24ctl run next-fixture.json results.csv
.\fm24ctl run next-fixture.json 1-10 results.csv
.\fm24ctl run next-fixture.json 42,99,300-400 lots-of-results.csv
```

## Experiments

There's a few things you might want to do with this feature:

1. Try many variations of your tactic played over thousands of games, measuring
   gains to find the best instructions
2. Repeatedly tweak the attributes of players to find what impacts the result
   the most significantly

I suggest knocking together a Python script
(see [/experiments for examples](./experiments)) to repeatedly run the fixture
against a few hundred seeds, tweaking it and running again, then tabulating the
results.

It should be trivial for an LLM to read the fixture JSON file--which I've taken
great pains to annotate--and modify it on a loop until it's iterated over all
the things you might care about. And it should be cheap too; it's the Python
script and my DLL doing most of the work, not your agent.

# Feature suggestions

Got an idea for how I an enhance this? Maybe more detailed outputs? Better
annotated inputs? Do you want a UI to represent what a given JSON blob looks
like? [Open an issue](PhilipArmstead/FM-24-Match-Harness/issues) and ask kindly.

# Development

See [technical details here](./docs/DEVELOPMENT.md).

# Support

This work is released for free for anybody to download or modify. I'll never
charge money for it.

That said, a lot of work went in to this so, if you think that's worth a couple
of quid, I'd be extremely grateful for a hot cup of tea or pack of biscuits.

[![Support me on Ko-fi](docs/kofi.png?v=1)](https://ko-fi.com/philarmstead)
