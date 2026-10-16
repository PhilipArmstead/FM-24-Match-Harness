"""Set each of a manager's attributes in turn to its maximum or to 1, over seeds 1-20.

Run from bin/ on Windows, with the harness built there
and FM24 idle after "Simulate Match", before "Continue":

    python -I ../tools/experiments/manager_attributes.py
    python -I ../tools/experiments/manager_attributes.py --seeds 1-100
    python -I ../tools/experiments/manager_attributes.py --minimum
    python -I ../tools/experiments/manager_attributes.py --only motivating,man_management
    python -I ../tools/experiments/manager_attributes.py --opponent "Aston Villa" --side both --both
    python -I ../tools/experiments/manager_attributes.py --help

The script looks for fm24ctl.exe in the working directory, then in ../bin/.

The next fixture is exported with seed 42; with --opponent, the game then
builds it against that team (fm24ctl replace-away), which may be your own team
playing itself. For each known attribute of the manager on --side (home by
default; away, or both) one variant sets that attribute alone to its maximum:
20 on the 1-20 scale, 100 on the 1-100 scale (docs/FIXTURE.md#staff-attributes).
--minimum sets it to 1 instead, and --both makes one variant of each.
Attributes already at that value, and unknown_* ones, are skipped; --only
limits the run to the named attributes. The unmodified fixture runs first; a
variant that plays any seed differently from it (a different score or final
random-number state) shows that the engine read that attribute. Results are
always the home side's.

With --opponent the matches are played with run --keep-build: from the one
build in the file, changing only the seed. The game rebuilds a team playing
itself differently every time (the filler players it adds to the away bench
get new positions), so run's per-seed rebuild can't place the edits there.

JSON fixtures and all CSV outputs are saved in the repository's
results/manager_attributes[-minimum][-vs-<opponent>]/, with a summary in
summary.csv. Later invocations overwrite the files they use. Errors stop the
experiment.
"""

import argparse
import csv
import json
import re
import subprocess
import sys
import time
from pathlib import Path

# python -I doesn't put the script's directory on the path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import staff_attributes as staff  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds", default="1-20", help="seed pattern (default 1-20)")
    values = parser.add_mutually_exclusive_group()
    values.add_argument(
        "--minimum", action="store_true", help="set each attribute to 1 instead of its maximum"
    )
    values.add_argument("--both", action="store_true", help="test both the maximum and 1")
    parser.add_argument("--only", help="comma-separated attribute names to test (default: all)")
    parser.add_argument("--opponent", help="build the fixture against this team (name or ID) first")
    parser.add_argument(
        "--side", choices=("home", "away", "both"), default="home", help="whose manager to edit"
    )
    args = parser.parse_args()
    started = time.perf_counter()
    fm24ctl = Path("fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        fm24ctl = Path("../bin/fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        raise FileNotFoundError("fm24ctl.exe not found in the working directory or ../bin/")
    count = staff.seed_count(args.seeds)
    name = "manager_attributes" + ("-minimum" if args.minimum else "")
    if args.opponent:
        name += "-vs-" + re.sub(r"[^a-z0-9]+", "-", args.opponent.lower()).strip("-")
    directory = staff.ROOT / "results" / name
    directory.mkdir(parents=True, exist_ok=True)
    original = directory / "next-fixture.json"
    exported = directory / "exported.json" if args.opponent else original
    subprocess.run(
        [str(fm24ctl), "get-next-fixture", str(exported), "--seed", "42"],
        cwd=fm24ctl.parent,
        check=True,
    )
    if args.opponent:
        subprocess.run(
            [str(fm24ctl), "replace-away", str(exported), args.opponent, str(original)],
            cwd=fm24ctl.parent,
            check=True,
        )
    with original.open(encoding="utf-8") as f:
        fields = json.load(f)["fields"]
    found = staff.find_staff(fields)
    sides = ("home", "away") if args.side == "both" else (args.side,)
    managers = {side: found.get(side + ".manager") for side in sides}
    for side, manager in managers.items():
        if not manager:
            raise ValueError(f"The fixture has no labelled {side} manager attributes")
    names = {name for _, name in managers[sides[0]]["attributes"]}
    only = set(args.only.split(",")) if args.only else None
    if only and not only <= names:
        raise ValueError(f"Unknown attributes: {sorted(only - names)}")
    extra = ("--keep-build",) if args.opponent else ()
    # Labels keep the old file names for a plain run of the home side at one value.
    plain = sides == ("home",) and not args.both

    baseline = staff.run_fixture(original, args.seeds, count, fm24ctl, extra)
    rows = [staff.summarise("baseline", "", baseline, None)]
    print("variant                                   from  to   W  D  L   GF   GA  score rng", flush=True)
    for side in sides:
        for position, (index, attribute) in enumerate(managers[side]["attributes"]):
            if only and attribute not in only:
                continue
            value = fields[index]["u8"]
            tops = [staff.maximum(position), 1] if args.both else [1 if args.minimum else staff.maximum(position)]
            for top in tops:
                label = attribute if plain else f"{side}-{attribute}-{top}"
                if attribute.startswith("unknown_") or value == top:
                    print(f"{label:<41} {value:>4}      skipped", flush=True)
                    continue
                variant = staff.write_variant(original, label, [(index, attribute, top)])
                results = staff.run_fixture(variant, args.seeds, count, fm24ctl, extra)
                row = staff.summarise(label, side, results, baseline)
                row["from"], row["to"] = value, top
                rows.append(row)
                print(
                    f"{label:<41} {value:>4} {top:>3} {row['wins']:>3}{row['draws']:>3}"
                    f"{row['losses']:>3} {row['goals_for']:>5}{row['goals_against']:>5}"
                    f" {row['seeds_score_changed']:>5} {row['seeds_rng_changed']:>3}",
                    flush=True,
                )

    with (directory / "summary.csv").open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[-1]), restval="")
        writer.writeheader()
        writer.writerows(rows)
    print(f"Experiment took {time.perf_counter() - started:.2f} seconds", flush=True)


if __name__ == "__main__":
    main()
