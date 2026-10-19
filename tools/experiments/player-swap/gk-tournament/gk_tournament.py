"""Run the goalkeeper league and knockout tournament on Windows with FM24 idle.

From the repository root:
    python -I tools/experiments/player-swap/gk-tournament/gk_tournament.py

Paths are independent of the working directory. The runner is bin/fm24ctl.exe;
goalkeepers.txt and the read-only villas-souped.json live beside this script.
Outputs default to results/gk-tournament/; --output-dir changes that directory.

Each group's four keepers meet twice, swapping home/away (96 fixtures), over
seeds 1-500 with --detailed. Player values are fetched once per keeper per invocation, with
condition and both sharpness values set to 10000, fatigue to 0 and morale to 20.
--keep-build preserves this Villa-vs-Villa snapshot without per-seed rebuilding.

The top two in each group advance by total seed wins, breaking ties by their
original goalkeepers.txt order. Group winners receive seeds 1-8 and runners-up
9-16, each sorted by wins with the same tie break. The fixed standard bracket
keeps seeds 1 and 2 apart until the final; it is not reseeded between rounds.
Knockouts also have two legs, decided by combined wins. Ties use extra seeds
from 501, each played home and away, until the combined win counts differ.

Fixtures, results, detailed player/team reports and command logs share a stem.
An existing results CSV with the correct row count skips fixture creation and
execution; its scores restore the win counts even without the fixture or log.
Wrong row counts (including old 1000-seed results) restart that leg, whether
found on disk or just generated. Other invalid results stop the tournament.
New runs use the runner's "runs: home" summary, checked against the CSV.
league-standings.csv records qualification/seeds; knockout-results.csv records
combined wins for the named first-leg home/away keepers and tie-break seed counts.
Other errors stop execution; rerun to resume.
"""

import argparse
import csv
import json
import re
import subprocess
from dataclasses import dataclass
from itertools import combinations
from pathlib import Path
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BASE = HERE / "villas-souped.json"
RUNNER = ROOT / "bin" / "fm24ctl.exe"
LAST_SEED = 500
WELLBEING = {
    "condition": ("u16", 10000),
    "sharpness[0]": ("u16", 10000),
    "sharpness[1]": ("u16", 10000),
    "fatigue": ("u16", 0),
    "morale": ("u8", 20),
}


@dataclass(frozen=True)
class Keeper:
    group: str
    name: str
    uid: int


class ResultRowCountError(ValueError):
    pass


def read_groups(path):
    groups = {}
    group = None
    uids = set()
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line == "# League phase":
            continue
        heading = re.fullmatch(r"## Group ([A-H])", line)
        player = re.fullmatch(r"- (.+) \(([1-9][0-9]*)\)", line)
        if heading and heading[1] not in groups:
            group = heading[1]
            groups[group] = []
        elif player and group is not None:
            uid = int(player[2])
            if uid in uids or uid > 0xFFFFFFFF:
                raise ValueError(f"{path}:{number}: duplicate or invalid player UID {uid}")
            uids.add(uid)
            groups[group].append(Keeper(group, player[1], uid))
        else:
            raise ValueError(f"{path}:{number}: invalid group or player line: {line}")
    if list(groups) != list("ABCDEFGH") or any(len(players) != 4 for players in groups.values()):
        raise ValueError(f"{path}: expected groups A-H in order, with four keepers each")
    return groups


def read_results(path, first=1, last=LAST_SEED):
    home = draws = away = 0
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        if len(rows) != last - first + 1:
            raise ResultRowCountError(
                f"{path}: expected {last - first + 1} result rows for seeds {first}-{last}, "
                f"got {len(rows)}"
            )
        if not {"seed", "home_goals", "away_goals", "finished"} <= set(reader.fieldnames or ()):
            raise ValueError(f"{path}: missing result columns")
        for seed, row in enumerate(rows, first):
            if row["seed"] != str(seed) or row["finished"] != "1":
                raise ValueError(f"{path}: expected a finished result for seed {seed}")
            hg, ag = int(row["home_goals"]), int(row["away_goals"])
            if hg < 0 or ag < 0:
                raise ValueError(f"{path}: negative goals for seed {seed}")
            home += hg > ag
            draws += hg == ag
            away += hg < ag
    return home, draws, away


def parse_summary(stdout, count):
    summaries = re.findall(
        r"^(\d+) runs: home (\d+) / draw (\d+) / away (\d+),", stdout, re.MULTILINE
    )
    if len(summaries) != 1:
        raise ValueError("Runner stdout must contain exactly one 'runs: home' summary")
    runs, home, draws, away = map(int, summaries[0])
    if runs != count or home + draws + away != count:
        raise ValueError(f"Runner summary does not account for {count} seeds")
    return home, draws, away


def bracket_order(size):
    order = [1, 2]
    while len(order) < size:
        opposite = 2 * len(order) + 1
        order = [value for seed in order for value in (seed, opposite - seed)]
    return order


def write_table(path, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


class Tournament:
    def __init__(self, directory, work):
        self.directory = directory
        self.work = work
        with BASE.open(encoding="utf-8") as stream:
            fixture = json.load(stream)
        if fixture["format"] != "fm24-match-config/1":
            raise ValueError(f"{BASE}: expected an fm24-match-config/1 fixture")
        fields = {field["name"]: field for field in fixture["fields"] if "name" in field}
        self.original_uids = {
            side: fields[f"{side}.player[0].person_uid"]["u32"] for side in ("home", "away")
        }
        for side in ("home", "away"):
            for suffix, (kind, _) in WELLBEING.items():
                if kind not in fields[f"{side}.player[0].{suffix}"]:
                    raise ValueError(f"{BASE}: invalid {side} goalkeeper field {suffix}")

    def command(self, log, *args):
        if not RUNNER.is_file():
            raise FileNotFoundError(f"{RUNNER} not found; build the harness on Windows first")
        command = [str(RUNNER), *map(str, args)]
        log.write("$ " + subprocess.list2cmdline(command) + "\n")
        log.flush()
        result = subprocess.run(
            command, cwd=RUNNER.parent, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", check=False,
        )
        log.write(result.stdout)
        log.flush()
        print(result.stdout, end="", flush=True)
        result.check_returncode()
        return result.stdout

    def player_file(self, keeper, log):
        path = self.work / f"player-{keeper.uid}.json"
        if not path.exists():
            self.command(log, "get-player", keeper.uid, path)
            with path.open(encoding="utf-8") as stream:
                player = json.load(stream)
            fields = {field["name"]: field for field in player["fields"]}
            if player["format"] != "fm24-player/1" or fields["person_uid"]["u32"] != keeper.uid:
                raise ValueError(f"{path}: wrong player export for {keeper.uid}")
            for suffix, (kind, value) in WELLBEING.items():
                if kind not in fields[suffix]:
                    raise ValueError(f"{path}: invalid player field {suffix}")
                fields[suffix][kind] = value
            with path.open("w", encoding="utf-8") as stream:
                json.dump(player, stream, ensure_ascii=False, separators=(",", ":"))
                stream.write("\n")
        return path

    def play(self, label, home, away, first=1, last=LAST_SEED):
        fixture = self.directory / f"{label}.json"
        output = fixture.with_suffix(".csv")
        if output.exists():
            try:
                wins = read_results(output, first, last)
            except ResultRowCountError as error:
                print(f"Restarting {label}: {error}", flush=True)
            else:
                print(f"Skipping {label}: results already exist", flush=True)
                return wins
        while True:
            print(f"Playing {label}: {home.name} vs {away.name}, seeds {first}-{last}", flush=True)
            with fixture.with_suffix(".log").open("w", encoding="utf-8") as log:
                intermediate = self.work / "home.json"
                for side, keeper, source, target in (
                    ("home", home, BASE, intermediate),
                    ("away", away, intermediate, fixture),
                ):
                    # A keeper already in this slot may also occupy the opposite baseline slot.
                    extra = ("--keep-uid",) if keeper.uid == self.original_uids[side] else ()
                    self.command(
                        log, "inject-player", source, f"{side}.player[0]",
                        self.player_file(keeper, log), target, *extra,
                    )
                seeds = str(first) if first == last else f"{first}-{last}"
                stdout = self.command(
                    log, "run", fixture, seeds, output, "--keep-build", "--detailed"
                )
            try:
                results = read_results(output, first, last)
            except ResultRowCountError as error:
                print(f"Restarting {label}: {error}", flush=True)
                continue
            wins = parse_summary(stdout, last - first + 1)
            if wins != results:
                raise ValueError(f"{output}: results disagree with the runner's win summary")
            return wins

    def play_pair(self, label, home, away, first=1, last=LAST_SEED):
        hw, draws, aw = self.play(f"{label}-{home.uid}-{away.uid}", home, away, first, last)
        rhw, rdraws, raw = self.play(f"{label}-{away.uid}-{home.uid}", away, home, first, last)
        return hw + raw, draws + rdraws, aw + rhw


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--output-dir", type=Path, default=ROOT / "results" / "gk-tournament",
        help="fixture, report and log directory (default: results/gk-tournament)",
    )
    args = parser.parse_args(argv)
    groups = read_groups(HERE / "goalkeepers.txt")
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix=".players-", dir=directory) as work:
        tournament = Tournament(directory, Path(work))
        totals = {keeper.uid: 0 for players in groups.values() for keeper in players}
        standings = {}
        for group, players in groups.items():
            for home, away in combinations(players, 2):
                hw, _, aw = tournament.play_pair(f"league-{group.lower()}", home, away)
                totals[home.uid] += hw
                totals[away.uid] += aw
            standings[group] = sorted(players, key=lambda keeper: -totals[keeper.uid])
            print(f"\nGroup {group}", flush=True)
            for position, keeper in enumerate(standings[group], 1):
                print(f"  {position}. {keeper.name} ({keeper.uid}): {totals[keeper.uid]} wins", flush=True)

        qualifiers = []
        for position in (0, 1):
            qualifiers.extend(sorted(
                (players[position] for players in standings.values()),
                key=lambda keeper: -totals[keeper.uid],
            ))
        seeds = {keeper.uid: seed for seed, keeper in enumerate(qualifiers, 1)}
        write_table(directory / "league-standings.csv", [
            {
                "group": group, "position": position, "uid": keeper.uid, "name": keeper.name,
                "wins": totals[keeper.uid], "seed": seeds.get(keeper.uid, ""),
            }
            for group, players in standings.items()
            for position, keeper in enumerate(players, 1)
        ])
        print("\nKnockout seeds", flush=True)
        for keeper in qualifiers:
            print(f"  {seeds[keeper.uid]}. {keeper.name}: {totals[keeper.uid]} wins", flush=True)

        active = [qualifiers[seed - 1] for seed in bracket_order(16)]
        results = []
        for round_name in ("round-of-16", "quarter-final", "semi-final", "final"):
            winners = []
            for home, away in zip(active[::2], active[1::2]):
                label = f"knockout-{round_name}"
                hw, draws, aw = tournament.play_pair(label, home, away)
                extra_seed = LAST_SEED
                while hw == aw:
                    extra_seed += 1
                    if extra_seed > 0xFFFFFFFF:
                        raise ValueError(f"{label}: exhausted the runner's seed range without a winner")
                    h, d, a = tournament.play_pair(
                        f"{label}-tiebreak-{extra_seed}", home, away, extra_seed, extra_seed
                    )
                    hw += h
                    draws += d
                    aw += a
                winner = home if hw > aw else away
                winners.append(winner)
                results.append({
                    "round": round_name,
                    "home_uid": home.uid, "home_name": home.name, "home_seed": seeds[home.uid],
                    "away_uid": away.uid, "away_name": away.name, "away_seed": seeds[away.uid],
                    "home_wins": hw, "draws": draws, "away_wins": aw,
                    "tiebreak_seeds": extra_seed - LAST_SEED, "winner_uid": winner.uid,
                })
                write_table(directory / "knockout-results.csv", results)
                print(f"{round_name}: {winner.name} advances ({hw}-{draws}-{aw})", flush=True)
            active = winners
        print(f"\nChampion: {active[0].name} ({active[0].uid})", flush=True)


if __name__ == "__main__":
    main()
