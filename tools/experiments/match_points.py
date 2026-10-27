"""Work out a month's match points for develop's match_points.monthN, from planned matches.

    python -I tools/experiments/match_points.py --file kellyman.json --matches 3 --minutes 90 --level 18 --average-ability 136 --opponent-reputation 7000
    python -I tools/experiments/match_points.py --played-level 1 --appearances 1 --matches 4 --minutes 90 --level 1 --average-ability 110 --opponent-reputation 2000 --friendly

The game's per-match formula (sub_144886B30; knowledge base section 20), for one kind of match
played --matches times. Run it once per kind and add the totals for a mixed month. Prints the
total and the development-file entries: match_points.monthN, and matches.monthN for the
sharpness the same matches bring. The player's played level and appearances there come from
--file (a get-development export) or --played-level/--appearances; neither changes in-season.

--level is the competition's reputation / 10 (Premier League 18, Serie A 17); friendlies are 1.
--average-ability is the minutes-weighted mean CA of both matchday squads. U21 and reserve games
appear not to be competitive: give --friendly for them (unconfirmed).
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # python -I leaves the script's folder out
from development_validation import fields_of  # noqa: E402


def match_points(minutes, level, average_ability, opponent_reputation, played_level, appearances,
                 competitive=True, result="draw", player_of_the_match=False):
    """Points one match adds to the player's counter (+0x1AE)."""
    minutes = min(minutes, 90)
    if minutes <= 0:
        return 0
    steps = max(1, minutes // 5)
    if minutes >= 45:
        steps += {"win": 1, "draw": 0, "loss": -1}[result] + player_of_the_match
    weight = (average_ability // 10 + level + opponent_reputation // 500) / 40 + 0.5
    if competitive:
        weight = max(weight, 1.0)
    ratio = level / played_level if played_level else 1.0
    if level and level >= played_level and appearances <= 59:  # new-level bonus
        ratio *= 1.5 - 0.5 * appearances / 60
    ratio = min(max(ratio, 1.0 if competitive else 0.85), 2.0)
    # shortcut: doubles where the game uses floats; a product within ~1e-6 of a whole number may
    # truncate one point differently.
    return int(weight * ratio * steps)


def self_check():
    # Measured (knowledge base section 20): an AI friendly (average CA 109, opponent 5100, a win,
    # 90 minutes) gave a senior +16 and a youth with played level 1 and 1 appearance +29 in
    # another; Kellyman (played level 1, 1 appearance) +29 for 90 minutes and a 6-2 loss in a
    # first team vs U21 friendly (opponent 7000, average CA 110-129).
    assert match_points(90, 1, 109, 5100, 12, 100, competitive=False, result="win") == 16
    assert match_points(90, 1, 109, 5100, 1, 1, competitive=False, result="win") == 29
    assert match_points(90, 1, 120, 7000, 1, 1, competitive=False, result="loss") == 29
    # The worked example: average 150, Premier League, opponent 9000 gives a weight of 1.775.
    assert match_points(90, 18, 150, 9000, 18, 120) == int(1.775 * 18)


def main():
    self_check()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--file", type=Path, help="a development file to take the played level from")
    parser.add_argument("--played-level", type=int, help="played_level in the file")
    parser.add_argument("--appearances", type=int, help="played_level.appearances in the file")
    parser.add_argument("--matches", type=int, required=True)
    parser.add_argument("--minutes", type=int, required=True, help="per match")
    parser.add_argument("--level", type=int, required=True, help="competition reputation / 10")
    parser.add_argument("--average-ability", type=int, required=True)
    parser.add_argument("--opponent-reputation", type=int, required=True, help="0-10000")
    parser.add_argument("--friendly", action="store_true", help="not competitive (also U21 games)")
    parser.add_argument("--result", choices=["win", "draw", "loss"], default="draw")
    parser.add_argument("--player-of-the-match", action="store_true")
    parser.add_argument("--month", type=int, default=1, help="for the printed entry")
    args = parser.parse_args()

    played_level, appearances = args.played_level, args.appearances
    if args.file:
        values = {f["name"]: f.get("u8") for f in fields_of(args.file)["fields"]}
        played_level = values["played_level"] if played_level is None else played_level
        appearances = values["played_level.appearances"] if appearances is None else appearances
    if played_level is None or appearances is None:
        parser.error("give --file, or --played-level and --appearances")

    each = match_points(args.minutes, args.level, args.average_ability, args.opponent_reputation,
                        played_level, appearances, not args.friendly, args.result,
                        args.player_of_the_match)
    total = min(each * args.matches, 65535)
    print(f"{args.matches} x {each} = {total} points")
    print(f'{{"name": "match_points.month{args.month}", "u16": {total}}},')
    # The same matches for sharpness (develop: matches.monthN, knowledge base section 21).
    print(f'{{"name": "matches.month{args.month}.count", "u16": {args.matches}}},')
    print(f'{{"name": "matches.month{args.month}.minutes", "u16": {min(args.minutes, 120)}}}')


if __name__ == "__main__":
    main()
