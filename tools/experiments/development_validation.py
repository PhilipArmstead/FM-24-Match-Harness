"""Check `fm24ctl develop`'s multi-month predictions against the game.

Run from bin/ on Windows, with the harness built there and a save loaded:

    python -I ../tools/experiments/development_validation.py snapshot baseline
    (play on; once every in-game week:)
    python -I ../tools/experiments/development_validation.py snapshot week02
    (at the end, with the baseline save loaded: develop counts from the game's date)
    python -I ../tools/experiments/development_validation.py predict baseline --months 4 \
        --weeks baseline week02 week03 ...
    (reload a save before playing on: develop leaves the game changed)
    python -I ../tools/experiments/development_validation.py compare baseline week06 week10 week14

snapshot writes each player's development file (fm24ctl get-development) to
results/development_validation/<label>/<uid>.json, with the week's schedule.
predict runs develop on the baseline files, one row per seed and month, to
<label>/predict-<uid>.csv. With --weeks, each player trains week k on the
schedule from the k-th snapshot listed (written to <label>/weeks-<uid>.json),
so the prediction uses the schedules the game actually trained. Without it,
develop repeats the baseline week, which is only fair for a fixed schedule.

compare counts, for each snapshot and player, the development days the game
has run since the baseline (day (row ID % 28) + 1 of each month, from the
files' comments; a day the baseline was exported on has already run). It
compares the snapshot with develop's month of the same number: CA, and every
attribute, as the share of seeds below the game's value (ties count half).
A value in the outer 5% at either end is flagged. Matching predictions put
about 10% of attributes outside, at random; more, or drift in one direction,
means develop and the game disagree.
"""

import argparse
import csv
import datetime
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "results" / "development_validation"

PLAYERS = {
    2000147523: "Omari Kellyman",  # 18, CA 99 / PA 168: fast growth
    2000128236: "Harvey Rhoades",  # 19, 53 / 116: the calibration player
    2000283797: "Sam Proctor",  # 17, goalkeeper
    29193659: "Jacob Ramsey",  # 22, first team: matches, form, injuries
    19046041: "Philippe Coutinho",  # 31: ageing
}
FLAG = 0.05  # outer share flagged at each end


def find_fm24ctl():
    for candidate in (Path("fm24ctl.exe"), Path("../bin/fm24ctl.exe")):
        if candidate.resolve().is_file():
            return candidate.resolve()
    raise FileNotFoundError("fm24ctl.exe not found in the working directory or ../bin/")


def run(fm24ctl, *arguments):
    subprocess.run([str(fm24ctl), *map(str, arguments)], cwd=fm24ctl.parent, check=True,
                   stdout=subprocess.DEVNULL)


def load(path):
    """Returns ({field: value}, export date, development day of the month)."""
    text = path.read_text(encoding="utf-8")
    exported = re.search(r"// Exported on day (\d+) of (\d+)", text)
    day = re.search(r"develops on day (\d+) of each month", text)
    if not exported or not day:
        raise ValueError(f"{path}: no export date or development day; re-export with this build")
    date = datetime.date(int(exported[2]), 1, 1) + datetime.timedelta(int(exported[1]) - 1)
    fields = json.loads(re.sub(r"^\s*//.*$", "", text, flags=re.M))["fields"]
    values = {}
    for field in fields:
        for key in ("u8", "u16", "s8", "s16", "u32", "u8map"):
            if key in field:
                values[field["name"]] = field[key]
    return values, date, int(day[1])


def developments_between(start, end, day):
    """Development days in (start, end]."""
    count = 0
    date = start + datetime.timedelta(1)
    while date <= end:
        count += date.day == day
        date += datetime.timedelta(1)
    return count


def snapshot(label, fm24ctl):
    folder = RESULTS / label
    folder.mkdir(parents=True, exist_ok=True)
    for uid in PLAYERS:
        run(fm24ctl, "get-development", uid, folder / f"{uid}.json")
    print(f"wrote {folder}")


def fields_of(path):
    return json.loads(re.sub(r"^\s*//.*$", "", path.read_text(encoding="utf-8"), flags=re.M))


def with_weeks(folder, uid, weeks):
    """The baseline file with schedule.week1..N from the snapshots in `weeks` (each one's week1)."""
    config = fields_of(folder / f"{uid}.json")
    fields = [f for f in config["fields"] if not f["name"].startswith("schedule.week")]
    dates = []
    for number, label in enumerate(weeks, 1):
        path = RESULTS / label / f"{uid}.json"
        week = next(f for f in fields_of(path)["fields"] if f["name"] == "schedule.week1")
        fields.append({"name": f"schedule.week{number}", "sessions": week["sessions"]})
        dates.append(load(path)[1])
    for before, after in zip(dates, dates[1:]):
        if not 4 <= (after - before).days <= 10:
            print(f"warning: {uid}: snapshots {before} and {after} are not about a week apart")
    out = folder / f"weeks-{uid}.json"
    out.write_text(json.dumps(dict(config, fields=fields), indent=1), encoding="utf-8")
    return out


def predict(label, months, seeds, weeks, fm24ctl):
    folder = RESULTS / label
    # develop counts from the game's date, so the game must be at the snapshot's (load its save).
    uid = next(iter(PLAYERS))
    probe = folder / "date-check.json"
    run(fm24ctl, "get-development", uid, probe)
    now, wanted = load(probe)[1], load(folder / f"{uid}.json")[1]
    probe.unlink()
    if now != wanted:
        raise SystemExit(f"the game is at {now}, the {label} snapshot at {wanted}: load the {label} save")
    for uid, name in PLAYERS.items():
        print(f"{name}...", flush=True)
        source = with_weeks(folder, uid, weeks) if weeks else folder / f"{uid}.json"
        run(fm24ctl, "develop", source, months, seeds, folder / f"predict-{uid}.csv",
            "--per-seed-breakdown")
    print(f"wrote {folder}")


def share_below(values, actual):
    below = sum(v < actual for v in values)
    equal = sum(v == actual for v in values)
    return (below + equal / 2) / len(values)


def compare(baseline, labels):
    out = RESULTS / f"compare-{'-'.join(labels)}.csv"
    with out.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(["snapshot", "player", "developments", "value", "start", "game",
                         "predicted_mean", "p5", "p95", "share_below", "flag"])
        for uid, name in PLAYERS.items():
            start, start_date, day = load(RESULTS / baseline / f"{uid}.json")
            with (RESULTS / baseline / f"predict-{uid}.csv").open(encoding="utf-8") as source:
                rows = list(csv.DictReader(source))
            for label in labels:
                game, date, _ = load(RESULTS / label / f"{uid}.json")
                month = developments_between(start_date, date, day)
                predicted = [r for r in rows if int(r["month"]) == month]
                if not predicted:
                    print(f"{label} {name}: {month} developments, beyond the prediction")
                    continue
                values = {"current_ability": (start["current_ability"], game["current_ability"])}
                for attribute, value in game["ratings"].items():
                    if not attribute.startswith("pos_"):
                        values[attribute] = (start["ratings"][attribute], value)
                flagged = []
                for value, (before, actual) in values.items():
                    sample = sorted(float(r[value]) for r in predicted)
                    share = share_below(sample, actual)
                    flag = "low" if share < FLAG else "high" if share > 1 - FLAG else ""
                    if flag:
                        flagged.append(f"{value} {flag}")
                    writer.writerow([label, name, month, value, before, actual,
                                     round(sum(sample) / len(sample), 2), sample[len(sample) // 20],
                                     sample[-1 - len(sample) // 20], round(share, 3), flag])
                ca = sorted(float(r["current_ability"]) for r in predicted)
                print(f"{label} {name:18} {month} dev: CA {start['current_ability']} -> "
                      f"{game['current_ability']} (predicted {sum(ca) / len(ca):.1f}, "
                      f"90% {ca[len(ca) // 20]:.0f}-{ca[-1 - len(ca) // 20]:.0f}); "
                      f"{len(flagged)}/{len(values)} flagged" + (f": {', '.join(flagged)}" if flagged else ""))
    print(f"wrote {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("snapshot").add_argument("label")
    command = commands.add_parser("predict")
    command.add_argument("label")
    command.add_argument("--months", type=int, default=3)
    command.add_argument("--seeds", default="1-200")
    command.add_argument("--weeks", nargs="+", help="weekly snapshots, in order: their schedules")
    command = commands.add_parser("compare")
    command.add_argument("baseline")
    command.add_argument("snapshots", nargs="+")
    args = parser.parse_args()
    if args.command == "snapshot":
        snapshot(args.label, find_fm24ctl())
    elif args.command == "predict":
        predict(args.label, args.months, args.seeds, args.weeks, find_fm24ctl())
    else:
        compare(args.baseline, args.snapshots)


if __name__ == "__main__":
    main()
