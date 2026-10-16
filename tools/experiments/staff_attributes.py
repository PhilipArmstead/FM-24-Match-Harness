"""Max out each home staff member's attributes in turn over seeds 1-50.

Run from bin/ on Windows, with the harness built there
and FM24 idle after "Simulate Match", before "Continue":

    python -I ../tools/experiments/staff_attributes.py
    python -I ../tools/experiments/staff_attributes.py --seeds 1-100
    python -I ../tools/experiments/staff_attributes.py --help

The script looks for fm24ctl.exe in the working directory, then in ../bin/.
Commands run from the executable's directory.

The next fixture is exported with seed 42. Every home block with staff
attributes (the manager, each backroom entry and team_extra: the
<block>.attributes.* fields) gets one variant in which every known attribute
is maxed: 20 for the 1-20 scale (the first 27), 100 for the 1-100 scale.
Attributes whose meaning is unknown (unknown_*) are left as they are. A final
variant maxes everyone at once. home.staff[0..2] have no attributes and are
not changed.

The unmodified fixture runs first. Seeds are paired, so a variant that plays
any seed differently from the baseline (a different score or a different final
random-number state) shows that the engine read something that was changed.

JSON fixtures and all CSV outputs are saved in the repository's
results/staff_attributes/, with a summary in summary.csv. Later invocations
overwrite the files they use. Errors stop the experiment.
"""

import argparse
import csv
import json
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ATTRIBUTE_COUNT = 52
SCALE_20_COUNT = 27  # the first 27 are on the 1-20 scale, the rest on 1-100
ATTRIBUTE = re.compile(r"(home\.[^.]+)\.attributes\.(\w+)")


def find_staff(fields):
    """Return {block: {"attributes": [(index, name), ...52], "name": person}}."""
    staff = {}
    for i, field in enumerate(fields):
        match = ATTRIBUTE.fullmatch(field.get("name", ""))
        if match:
            staff.setdefault(match.group(1), {"attributes": [], "name": match.group(1)})
            staff[match.group(1)]["attributes"].append((i, match.group(2)))
    for block, info in staff.items():
        if len(info["attributes"]) != ATTRIBUTE_COUNT:
            raise ValueError(f"{block}: expected {ATTRIBUTE_COUNT} attributes")
        for i, field in enumerate(fields):
            if field.get("name") == block + ".person_uid":
                # The full name is the first string with a space after person_uid.
                for after in fields[i : i + 8]:
                    if "str" in after and " " in after["str"]:
                        info["name"] = after["str"]
                        break
                break
    return staff


def maximum(position):
    return 20 if position < SCALE_20_COUNT else 100


def write_variant(original, label, attributes):
    """Writes a copy of `original` with each (index, name, value) in `attributes` set."""
    with original.open(encoding="utf-8") as f:
        fixture = json.load(f)
    fields = fixture["fields"]
    for index, name, value in attributes:
        field = fields[index]
        if not field.get("name", "").endswith(".attributes." + name) or type(field.get("u8")) is not int:
            raise ValueError(f"field {index}: expected the u8 attribute {name}")
        field["u8"] = value
    variant = original.with_name(original.stem + "-" + label + ".json")
    with variant.open("w", encoding="utf-8") as f:
        json.dump(fixture, f, ensure_ascii=False)
        f.write("\n")
    return variant


def run_fixture(fixture, seeds, count, fm24ctl):
    output = fixture.with_suffix(".csv")
    subprocess.run(
        [str(fm24ctl), "run", str(fixture), seeds, str(output)],
        cwd=fm24ctl.parent,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    results = {}
    with output.open(encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if row["finished"] != "1":
                raise ValueError(f"{output}: seed {row['seed']} did not finish")
            results[int(row["seed"])] = (
                int(row["home_goals"]),
                int(row["away_goals"]),
                row["rng_final"],
            )
    if len(results) != count:
        raise ValueError(f"{output}: expected {count} results, got {len(results)}")
    return results


def summarise(label, person, results, baseline):
    wins = sum(h > a for h, a, _ in results.values())
    draws = sum(h == a for h, a, _ in results.values())
    losses = len(results) - wins - draws
    goals_for = sum(h for h, _, _ in results.values()) / len(results)
    goals_against = sum(a for _, a, _ in results.values()) / len(results)
    scores = sum(results[s][:2] != baseline[s][:2] for s in results) if baseline else 0
    rngs = sum(results[s][2] != baseline[s][2] for s in results) if baseline else 0
    return {
        "variant": label,
        "person": person,
        "wins": wins,
        "draws": draws,
        "losses": losses,
        "goals_for": f"{goals_for:.2f}",
        "goals_against": f"{goals_against:.2f}",
        "seeds_score_changed": scores,
        "seeds_rng_changed": rngs,
    }


def seed_count(seeds):
    count = 0
    for part in seeds.split(","):
        low, _, high = part.partition("-")
        count += int(high or low) - int(low) + 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds", default="1-50", help="seed pattern (default 1-50)")
    args = parser.parse_args()
    started = time.perf_counter()
    fm24ctl = Path("fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        fm24ctl = Path("../bin/fm24ctl.exe").resolve()
    if not fm24ctl.is_file():
        raise FileNotFoundError("fm24ctl.exe not found in the working directory or ../bin/")
    count = seed_count(args.seeds)
    directory = ROOT / "results" / "staff_attributes"
    directory.mkdir(parents=True, exist_ok=True)
    original = directory / "next-fixture.json"
    subprocess.run(
        [str(fm24ctl), "get-next-fixture", str(original), "--seed", "42"],
        cwd=fm24ctl.parent,
        check=True,
    )
    with original.open(encoding="utf-8") as f:
        staff = find_staff(json.load(f)["fields"])
    if not staff:
        raise ValueError("The exported fixture has no labelled home staff attributes")
    print(f"{len(staff)} home staff blocks with attributes", flush=True)

    baseline = run_fixture(original, args.seeds, count, fm24ctl)
    rows = [summarise("baseline", "", baseline, None)]
    print("variant              person                     W  D  L   GF   GA  score rng", flush=True)

    def report(row):
        rows.append(row)
        print(
            f"{row['variant']:<20} {row['person'][:25]:<25} {row['wins']:>3}{row['draws']:>3}"
            f"{row['losses']:>3} {row['goals_for']:>5}{row['goals_against']:>5}"
            f" {row['seeds_score_changed']:>5} {row['seeds_rng_changed']:>3}",
            flush=True,
        )

    report(rows.pop())
    def maxed(info):
        return [
            (index, name, maximum(position))
            for position, (index, name) in enumerate(info["attributes"])
            if not name.startswith("unknown_")
        ]

    variants = [(block, info["name"], maxed(info)) for block, info in staff.items()]
    variants.append(("all-staff", "everyone", [a for info in staff.values() for a in maxed(info)]))
    for block, person, attributes in variants:
        label = block.removeprefix("home.").replace("[", "-").replace("]", "")
        results = run_fixture(write_variant(original, label, attributes), args.seeds, count, fm24ctl)
        report(summarise(label, person, results, baseline))

    with (directory / "summary.csv").open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Experiment took {time.perf_counter() - started:.2f} seconds", flush=True)


if __name__ == "__main__":
    main()
