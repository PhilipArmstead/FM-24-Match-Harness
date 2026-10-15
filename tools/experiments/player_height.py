"""Compare all home players at 150 cm and 210 cm over seeds 1-300.

Run from bin/ on Windows, with the harness built there and FM24 idle after
"Simulate Match", before "Continue":

    python -I ../tools/experiments/player_height.py
    python -I ../tools/experiments/player_height.py --baseline

The script looks for fm24ctl.exe in the working directory, then ../bin/.
It exports the next fixture with seed 42, then runs 300 matches at 150 cm,
followed by 300 at 210 cm, including substitutes. Only home-player heights
change: each .height u32 holds the bits of a float in metres, not centimetres.
Ratings (including jumping reach), weights and away players stay unchanged.

Each run uses --detailed and prints the home team's win/draw/loss record.
--baseline also runs the unmodified fixture before the two height variants.
JSON fixtures and CSVs go in the repository's results/player-height/.
Later invocations overwrite their outputs. Errors stop the experiment.
"""

import argparse
import csv
import json
import re
import struct
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def write_variant(original, label, height):
    with original.open(encoding="utf-8") as f:
        fixture = json.load(f)
    fields = {field["name"]: field for field in fixture["fields"] if "name" in field}
    players = [name for name in fields if re.fullmatch(r"home\.player\[\d+\]", name)]
    if not players:
        raise ValueError("The exported fixture has no labelled home players")
    height_bits = struct.unpack("<I", struct.pack("<f", height / 100))[0]
    for player in players:
        name = player + ".height"
        if name not in fields:
            raise ValueError(f"{name}: missing height label; rebuild the harness")
        field = fields[name]
        if type(field.get("u32")) is not int or not 0 <= field["u32"] <= 0xFFFFFFFF:
            raise ValueError(f"{name}: expected a u32 containing float metres")
        field["u32"] = height_bits
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
    directory = ROOT / "results" / "player-height"
    directory.mkdir(parents=True, exist_ok=True)
    original = directory / "next-fixture.json"
    subprocess.run(
        [str(fm24ctl), "get-next-fixture", str(original), "--seed", "42"],
        cwd=fm24ctl.parent,
        check=True,
    )
    if args.baseline:
        run_fixture(original, "baseline", fm24ctl)
    for label, height in (("150cm", 150), ("210cm", 210)):
        run_fixture(write_variant(original, label, height), label, fm24ctl)
    print(f"Experiment took {time.perf_counter() - started:.2f} seconds", flush=True)


if __name__ == "__main__":
    main()
