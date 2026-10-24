# Predict TRAINING_SESSION per-tier category weights (session+0x44/+0x70/+0x9C), mirroring
# sub_14390C590 (weight fix-up) + sub_1442D7DC0 + sub_142906E20 (attribute -> category).
import json, sys

ATTR = {'Acceleration': 49, 'Aerial Reach': 27, 'Aggression': 60, 'Agility': 61, 'Anticipation': 32, 'Balance': 57,
        'Bravery': 58, 'Command Of Area': 28, 'Communication': 29, 'Composure': 67, 'Concentration': 68, 'Corners': 42,
        'Crossing': 15, 'Decisions': 33, 'Dribbling': 16, 'Eccentricity': 46, 'Finishing': 17, 'First Touch': 37,
        'Flair': 41, 'Free Kick Taking': 50, 'Handling': 26, 'Heading': 18, 'Injury Proneness': 63, 'Jumping Reach': 54,
        'Kicking': 30, 'Long Shots': 19, 'Long Throws': 45, 'Marking': 20, 'Off The Ball': 21, 'Natural Fitness': 65,
        'One On Ones': 34, 'Pace': 53, 'Passing': 22, 'Penalty Taking': 23, 'Positioning': 35, 'Reflexes': 36,
        'Rushing Out (Tendency)': 47, 'Stamina': 52, 'Strength': 51, 'Tackling': 24, 'Teamwork': 43, 'Technique': 38,
        'Throwing': 31, 'Vision': 25, 'Work Rate': 44}
CATS = [[54, 65, 52, 51, 44], [49, 61, 57, 53], [36, 34, 67, 68, 35, 47, 32], [27, 26, 28, 29, 33, 30, 31, 25, 37, 38, 22],
        [60, 68, 35], [20, 24], [32, 33, 25, 43], [16, 37, 22, 18, 38], [67, 41, 21], [15, 17, 19], [23, 42, 45, 50]]
CAT_NAMES = ['Strength', 'Quickness', 'GK ShotStop', 'GK Handl/Dist', 'Defending', 'Tackling', 'Tactics',
             'Ball Control', 'Attacking', 'Shooting', 'Set Pieces']
SIZE = [5, 4, 7, 11, 3, 2, 4, 5, 3, 3, 4]
GK2 = {32, 34, 35, 36, 47, 67, 68}        # 0x180000801D bits, +32
GK3 = {22, 25, 26, 27, 28, 29, 30, 31, 33, 37, 38}  # 0x18BF9 bits, +22
PATH1 = {22, 25, 30, 33, 37, 38}           # 100361 bits, +22
PATH2 = {32, 35, 67, 68}                   # 0x600000002400 bits, +22


def category(a, gk):
    """sub_142906E20(a, gk): first category containing a; outfield (gk=0) reroutes some GK lists."""
    of = not gk
    for c in range(11):
        if of and c == 2:
            if a in PATH2:
                continue
            if a in GK2 and a not in PATH1:
                return 2
            continue
        if of and c == 3:
            if a in PATH1 or a in PATH2:
                continue
            if a in GK3:
                return 3
            continue
        if a in CATS[c]:
            return c
    return -1


def tier_weights(s, tier, gk):
    w = {3: s['primary_weight'], 2: s['secondary_weight'], 1: s['tertiary_weight']}[tier]
    key = {3: 'primary', 2: 'secondary', 1: 'tertiary'}[tier]
    attrs = [ATTR[a] for a in s[key] if a in ATTR]
    out = [0.0] * 11
    if not attrs:
        return out
    dil = max(0.05, 1 - 0.05 * (len(attrs) - 1))
    tw = w / 100 if (not gk or w >= 35) else 0.35
    for a in attrs:
        c = category(a, gk)
        if 0 <= c <= 10:
            out[c] += 20 / SIZE[c] * dil * tw
    return out


def fix_weights(s):
    # sub_14390C590: sum != 100 -> 60/20/20
    # shortcut: the flag-based rules (no secondary/tertiary flags -> 100/0/0, no tertiary -> 50/50/0) are not
    # applied; add them if a session's file weights don't sum to 100 and its live weights disagree with this output
    p, se, t = s['primary_weight'], s['secondary_weight'], s['tertiary_weight']
    if p + se + t != 100:
        p, se, t = 60, 20, 20
    s = dict(s, primary_weight=p, secondary_weight=se, tertiary_weight=t)
    return s


if __name__ == '__main__':
    # live value of Team Tactical (session 5) tier-3 Tactics weight, read 2026-10-22
    tactical = {'primary': ['Anticipation', 'Composure', 'Concentration', 'Decisions', 'Off The Ball',
                            'Positioning', 'Teamwork', 'Vision'],
                'secondary': [], 'tertiary': [], 'primary_weight': 100, 'secondary_weight': 0, 'tertiary_weight': 0}
    assert abs(tier_weights(tactical, 3, 0)[6] - 13.0) < 1e-6
    d = json.load(open(sys.argv[1]))['data_array']
    for s in d:
        s = fix_weights(s)
        for tier in (3, 2, 1):
            for gk in (0, 1):
                w = tier_weights(s, tier, gk)
                if any(w):
                    print(f"{s['name']:36} tier{tier} gk{gk} " + ' '.join(f'{x:5.2f}' for x in w))
