"""Max out each of the home manager's attributes in turn over seeds 1-20.

Run from bin/ on Windows, with the harness built there
and FM24 idle after "Simulate Match", before "Continue":

    python -I ../tools/experiments/manager_attributes.py
    python -I ../tools/experiments/manager_attributes.py --seeds 1-100
    python -I ../tools/experiments/manager_attributes.py --help

The script looks for fm24ctl.exe in the working directory, then in ../bin/.

The next fixture is exported with seed 42. For each known home.manager
attribute (docs/FIXTURE.md#staff-attributes) one variant sets that attribute
alone to its maximum: 20 on the 1-20 scale, 100 on the 1-100 scale.
Attributes already at their maximum, and unknown_* ones, are skipped. The
unmodified fixture runs first; a variant that plays any seed differently from
it (a different score or final random-number state) shows that the engine
read that attribute.

JSON fixtures and all CSV outputs are saved in the repository's
results/manager_attributes/, with a summary in summary.csv. Later invocations
overwrite the files they use. Errors stop the experiment.
"""

import argparse
import csv
import json
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
    args = parser.parse_args()
    started = time.perf_counter()
    fm24ctl = Path("fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        fm24ctl = Path("../bin/fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        raise FileNotFoundError("fm24ctl.exe not found in the working directory or ../bin/")
    count = staff.seed_count(args.seeds)
    directory = staff.ROOT / "results" / "manager_attributes"
    directory.mkdir(parents=True, exist_ok=True)
    original = directory / "next-fixture.json"
    subprocess.run(
        [str(fm24ctl), "get-next-fixture", str(original), "--seed", "42"],
        cwd=fm24ctl.parent,
        check=True,
    )
    with original.open(encoding="utf-8") as f:
        fields = json.load(f)["fields"]
    manager = staff.find_staff(fields).get("home.manager")
    if not manager:
        raise ValueError("The exported fixture has no labelled home manager attributes")

    baseline = staff.run_fixture(original, args.seeds, count, fm24ctl)
    rows = [staff.summarise("baseline", "", baseline, None)]
    print("attribute                            from  to   W  D  L   GF   GA  score rng", flush=True)
    for position, (index, name) in enumerate(manager["attributes"]):
        top = staff.maximum(position)
        value = fields[index]["u8"]
        if name.startswith("unknown_") or value >= top:
            print(f"{name:<36} {value:>4}      skipped", flush=True)
            continue
        variant = staff.write_variant(original, name, [(index, name, top)])
        row = staff.summarise(name, "", staff.run_fixture(variant, args.seeds, count, fm24ctl), baseline)
        row["from"], row["to"] = value, top
        rows.append(row)
        print(
            f"{name:<36} {value:>4} {top:>3} {row['wins']:>3}{row['draws']:>3}{row['losses']:>3}"
            f" {row['goals_for']:>5}{row['goals_against']:>5}"
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
