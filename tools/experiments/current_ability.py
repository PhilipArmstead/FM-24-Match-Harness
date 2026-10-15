"""Compare home-player Current Ability at 1 and 200 over seeds 1-300.

Run from bin/ on Windows, with the harness built there
and FM24 idle after "Simulate Match", before "Continue":

    python -I ../tools/experiments/current_ability.py
    python -I ../tools/experiments/current_ability.py --baseline
    python -I ../tools/experiments/current_ability.py --help

The script first looks for fm24ctl.exe in the working directory, then in
../bin/ relative to that directory. If neither file exists, it raises an
error before exporting or creating results. Commands run from the selected
executable's directory.

Options:
    --baseline  Also run the unmodified fixture before the two CA variants.
    -h, --help  Show command-line help and exit.

Every experiment exports the next fixture with seed 42. Without --baseline,
only min-CA (1) and max-CA (200) are played. Each fixture uses seeds 1-300
with --detailed, and prints the home team's win/draw/loss record.
The total elapsed time is printed when the experiment finishes.

JSON fixtures and all CSV outputs are saved in results/current-ability/.
Later invocations overwrite the files they use. Errors stop the experiment.
"""

import argparse
import csv
import json
import re
import subprocess
import time
from pathlib import Path

THIS_DIR = Path(__file__).resolve()


def write_variant(original, label, ability):
    with original.open(encoding="utf-8") as f:
        fixture = json.load(f)
    fields = {field["name"]: field for field in fixture["fields"] if "name" in field}
    players = [name for name in fields if re.fullmatch(r"home\.player\[\d+\]", name)]
    if not players:
        raise ValueError("The exported fixture has no labelled home players")
    for player in players:
        name = player + ".ability"
        field = fields[name]
        keys = [key for key in field if key in ("u8", "u16", "u32", "u64")]
        if len(keys) != 1 or type(field[keys[0]]) is not int:
            raise ValueError(f"{name}: expected one unsigned integer value")
        field[keys[0]] = ability
    variant = original.with_name(original.stem + "-" + label + ".json")
    with variant.open("w", encoding="utf-8") as f:
        json.dump(fixture, f, ensure_ascii=False)
        f.write("\n")
    return variant


def run_fixture(fixture, label, fm24ctl):
    output = fixture.with_suffix(".csv")
    subprocess.run(
        [str(fm24ctl), "run", str(fixture), "1-300", str(output), "--detailed"],
        cwd=fm24ctl.parent,
        check=True,
    )
    wins = draws = losses = 0
    with output.open(encoding="utf-8", newline="") as f:
        for seed, row in enumerate(csv.DictReader(f), 1):
            if int(row["seed"]) != seed or row["finished"] != "1":
                raise ValueError(f"{output}: expected a finished match for seed {seed}")
            home, away = int(row["home_goals"]), int(row["away_goals"])
            if home < 0 or away < 0:
                raise ValueError(f"{output}: negative goals for seed {seed}")
            wins += home > away
            draws += home == away
            losses += home < away
    if wins + draws + losses != 300:
        raise ValueError(f"{output}: expected exactly 300 results for seeds 1-300")
    print(
        f"{label} fixture: home team {wins} wins / {draws} draws / {losses} losses",
        flush=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--baseline", action="store_true", help="also run the unmodified fixture"
    )
    args = parser.parse_args()
    started = time.perf_counter()
    fm24ctl = Path("fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        fm24ctl = Path("../bin/fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        raise FileNotFoundError(
            "fm24ctl.exe not found in the working directory or ../bin/"
        )
    directory = THIS_DIR / "results" / "current-ability"
    directory.mkdir(parents=True, exist_ok=True)
    original = directory / "next-fixture.json"
    subprocess.run(
        [str(fm24ctl), "get-next-fixture", str(original), "--seed", "42"],
        cwd=fm24ctl.parent,
        check=True,
    )
    if args.baseline:
        run_fixture(original, "baseline", fm24ctl)
    for label, ability in (("min-CA", 1), ("max-CA", 200)):
        run_fixture(write_variant(original, label, ability), label, fm24ctl)
    print(f"Experiment took {time.perf_counter() - started:.2f} seconds", flush=True)


if __name__ == "__main__":
    main()
