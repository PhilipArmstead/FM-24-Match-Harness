"""Team instruction bit experiment.

    python -I team_instructions.py make <config.json> <dir> <bits>
    python -I team_instructions.py rank <dir>

make: writes <dir>/<variant>.json and <dir>/list.txt from a labelled config (protocol 15+):
    base               the config unchanged
    home_clear_W_B     one set bit of the home plan[0]'s team instructions cleared
    home_clear_all     all of them cleared
    home_set_W_B       a bit from <bits> that is clear at home, set
    away_clear_W_B/away_set_W_B   the same for the away plan[0]
<bits> lists the bits known to be valid choices (seen set in a game-made tactic), "W:B,W:B,...".
For home only the <bits> also listed in HOME_SET are set (others may clash with home's choices).

rank: compares each <dir>/<variant>.csv with <dir>/base.csv on the seeds both played (home
perspective; paired effect +- SE). Variants added to list.txt by hand are ranked too.
"""
import csv
import json
import math
import os
import statistics
import sys

HOME_SET = {(0, 11), (2, 5)}  # run at defence, get stuck in: set alongside home's bits in tac0


def instruction_fields(fields, side):
    prefix = '%s.manager.tactic.plan[0].team_instructions[' % side
    found = {int(f['name'][len(prefix)]): f for f in fields if f.get('name', '').startswith(prefix)}
    assert sorted(found) == [0, 1, 2], side
    return found


def make(config_path, out, bits_text):
    config = json.load(open(config_path, encoding='utf-8'))
    known = sorted({tuple(int(v) for v in item.split(':')) for item in bits_text.split(',')})
    os.makedirs(out, exist_ok=True)
    variants = {'base': []}
    for side in ('home', 'away'):
        words = {w: f['u32'] for w, f in instruction_fields(config['fields'], side).items()}
        set_bits = [(w, b) for w in range(3) for b in range(32) if words[w] >> b & 1]
        for w, b in set_bits:
            variants['%s_clear_%d_%d' % (side, w, b)] = [(side, w, b, 0)]
        if side == 'home' and set_bits:
            variants['home_clear_all'] = [(side, w, b, 0) for w, b in set_bits]
        for w, b in known:
            if (w, b) not in set_bits and (side == 'away' or (w, b) in HOME_SET):
                variants['%s_set_%d_%d' % (side, w, b)] = [(side, w, b, 1)]
    for name, edits in variants.items():
        edited = json.loads(json.dumps(config))
        for side, w, b, value in edits:
            field = instruction_fields(edited['fields'], side)[w]
            field['u32'] = field['u32'] | 1 << b if value else field['u32'] & ~(1 << b)
        json.dump(edited, open(os.path.join(out, name + '.json'), 'w', encoding='utf-8'))
    open(os.path.join(out, 'list.txt'), 'w').write('\n'.join(variants) + '\n')
    print('%d variants in %s' % (len(variants), out))


def load(path):
    """Seed -> (home goals, away goals, final RNG state)."""
    return {int(r['seed']): (int(r['home_goals']), int(r['away_goals']), r['rng_final'])
            for r in csv.DictReader(open(path))}


def measures(home, away):
    return {'home': home, 'away': away, 'diff': home - away,
            'points': 3 if home > away else 1 if home == away else 0}


def paired(variant, base):
    """Effect (mean, SE) of each measure over the seeds both runs played; SE of the paired differences."""
    seeds = sorted(set(variant) & set(base))
    effect = {}
    for k in ('home', 'away', 'diff', 'points'):
        deltas = [measures(*variant[s][:2])[k] - measures(*base[s][:2])[k] for s in seeds]
        effect[k] = (statistics.mean(deltas), statistics.stdev(deltas) / math.sqrt(len(deltas)))
    identical = all(variant[s] == base[s] for s in seeds)
    return len(seeds), effect, identical


def rank(out):
    base = load(os.path.join(out, 'base.csv'))
    base_means = {k: statistics.mean(measures(*v[:2])[k] for v in base.values())
                  for k in ('home', 'away', 'diff', 'points')}
    print('base: n=%d  home %.2f  away %.2f  GD %+.2f  points %.2f' % (
        len(base), *(base_means[k] for k in ('home', 'away', 'diff', 'points'))))
    print('effects vs base on the same seeds (paired); "same" = every match identical to base')
    print('%-18s %4s  %-15s %-15s %-15s %-15s %s' % (
        'variant', 'n', 'goal diff', 'home goals', 'away goals', 'home points', 'z(GD)'))
    rows = []
    for name in open(os.path.join(out, 'list.txt')).read().split():
        path = os.path.join(out, name + '.csv')
        if name == 'base' or not os.path.exists(path):
            continue
        rows.append((name, *paired(load(path), base)))
    rows.sort(key=lambda row: (row[3], row[2]['diff'][0]))
    with open(os.path.join(out, 'ranking.csv'), 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['variant', 'n', 'gd_effect', 'gd_se', 'home_goals_effect', 'away_goals_effect',
                         'points_effect', 'identical'])
        for name, n, e, identical in rows:
            if identical:
                print('%-18s %4d  same' % (name, n))
            else:
                print('%-18s %4d  %+.2f +- %.2f   %+.2f +- %.2f   %+.2f +- %.2f   %+.2f +- %.2f   %+.1f' % (
                    name, n, *e['diff'], *e['home'], *e['away'], *e['points'],
                    e['diff'][0] / e['diff'][1]))
            writer.writerow([name, n] + ['%.3f' % v for v in (
                e['diff'][0], e['diff'][1], e['home'][0], e['away'][0], e['points'][0])] + [int(identical)])


if len(sys.argv) == 5 and sys.argv[1] == 'make':
    make(*sys.argv[2:])
elif len(sys.argv) == 3 and sys.argv[1] == 'rank':
    rank(sys.argv[2])
else:
    sys.exit(__doc__)
