"""Play a fixture with every home manager match focus over a seed range.

Run from a directory holding fm24ctl.exe and the input fixture, with the harness
built there and FM24 idle after "Simulate Match", before "Continue":

    python -I match_focus.py villas-souped.json 1-1000

home.manager.match_focus.code[0] takes 0-2 and code[1] and code[2] take each
pair of different values from 3-8, in ascending order (3-4, never 4-3): 45
variants. Each variant is written as <input>-fixture-{0}-{1}-{2}.json and played
with fm24ctl run into <input>-output-{0}-{1}-{2}.csv, both in the current
directory. After each run, the line "{0}-{1}-{2}, <avg home goals>, <avg away
goals>, <wins>, <draws>, <losses>" from fm24ctl's summary replaces or is added to
match_focus_results.csv. A variant whose output CSV and results line both exist
is skipped. Errors stop the experiment.
"""

import itertools
import json
import re
import subprocess
import sys
import time
from pathlib import Path

CODES = [f"home.manager.match_focus.code[{i}]" for i in range(3)]
USAGE = "usage: python match_focus.py <input.json> <seed-range>"
SUMMARY = re.compile(r"^\d+ runs: home (\d+) / draw (\d+) / away (\d+), avg ([\d.]+)-([\d.]+)$", re.MULTILINE)


def read_results(path):
    if not path.is_file():
        return {}
    with path.open(encoding="utf-8") as f:
        return {line.split(",", 1)[0]: line.rstrip("\n") for line in f if line.strip()}


def write_results(path, results):
    with path.open("w", encoding="utf-8") as f:
        f.writelines(line + "\n" for line in results.values())


def run(fm24ctl, variant, seeds, output):
    """Run fm24ctl, echoing its stdout, and return the summary's (home goals, away goals, wins, draws, losses)."""
    lines = []
    with subprocess.Popen(
        [str(fm24ctl), "run", str(variant), seeds, str(output), "--keep-build"],
        cwd=fm24ctl.parent,
        stdout=subprocess.PIPE,
        text=True,
    ) as process:
        for line in process.stdout:
            print(line, end="", flush=True)
            lines.append(line)
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, process.args)
    summaries = SUMMARY.findall("".join(lines))
    if len(summaries) != 1:
        raise ValueError(f"{output.name}: fm24ctl stdout must contain exactly one 'runs: home' summary")
    wins, draws, losses, home_goals, away_goals = summaries[0]
    return home_goals, away_goals, wins, draws, losses


def main():
    if len(sys.argv) != 3:
        print(USAGE, file=sys.stderr)
        sys.exit(2)
    started = time.perf_counter()
    directory = Path.cwd()
    source = directory / sys.argv[1]
    seeds = sys.argv[2]
    fm24ctl = directory / "fm24ctl.exe"
    if not fm24ctl.is_file():
        raise FileNotFoundError(f"fm24ctl.exe not found in {directory}")
    with source.open(encoding="utf-8") as f:
        fixture = json.load(f)
    fields = {field.get("name"): field for field in fixture["fields"]}
    if fields.get("home.manager.match_focus.count", {}).get("u16") != 3:
        raise ValueError(f"{source}: expected a home manager with 3 match focuses")
    for name in CODES:
        if type(fields.get(name, {}).get("u8")) is not int:
            raise ValueError(f"{source}: expected the u8 field {name}")
    results_path = directory / "match_focus_results.csv"
    results = read_results(results_path)

    focuses = [(a, b, c) for a in range(3) for b, c in itertools.combinations(range(3, 9), 2)]
    for focus in focuses:
        label = "-".join(map(str, focus))
        output = directory / f"{source.stem}-output-{label}.csv"
        if output.is_file() and label in results:
            print(f"{label}: {output.name} and its results line exist, skipping", flush=True)
            continue
        for name, value in zip(CODES, focus):
            fields[name]["u8"] = value
        variant = directory / f"{source.stem}-fixture-{label}.json"
        with variant.open("w", encoding="utf-8") as f:
            json.dump(fixture, f, ensure_ascii=False)
            f.write("\n")
        print(label, flush=True)
        results[label] = ", ".join((label, *run(fm24ctl, variant, seeds, output)))
        write_results(results_path, results)
    print(f"Experiment took {time.perf_counter() - started:.2f} seconds", flush=True)


if __name__ == "__main__":
    main()
