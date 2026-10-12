"""Fits per-player slopes from a sensitivity CSV and summarises them across players.

    python -I analyse.py <dir> <sensitivity.csv> [min_position_rating]

<dir> holds the CSV, roles.json (role id -> name) and base1.json: the same fixture's config
from `fm24ctl get-next-fixture base1.json --seed 1` (the probe's default seed), for block
layouts. Role entries are only fitted where the player's position rating is at least
min_position_rating (15 = accomplished), and points where the role rating sits at the
player's ability cap (+0x60) are dropped: role rating = min(formula, ability).
"""
import csv, json, sys, collections, statistics as st
S, csvname = sys.argv[1], sys.argv[2]
roles = {int(k): v for k, v in json.load(open(S + '/roles.json')).items()}
roles[31] = 'pressing_forward'
base = json.load(open(S + '/base1.json', encoding='utf-8'))['fields']

def val(x):
    for t in ('u8', 'u16', 'u32', 'u64'):
        if t in x:
            return x[t]

# player blocks in the baseline config: start index -> name; fields keyed by index within block
starts = [(i, x['player']) for i, x in enumerate(base) if 'player' in x]
blocks = {}
abilities = {}
for n, (i, name) in enumerate(starts):
    end = starts[n + 1][0] if n + 1 < len(starts) else len(base)
    blk = base[i:end]
    meaning, baseval = {}, {}
    sites = {k: x['site'] for k, x in enumerate(blk)}
    idbytes = set()
    for k, x in enumerate(blk):
        if x['site'].startswith('1a80fac/'):
            idbytes.update((k + 1, k + 2))
    for k, x in enumerate(blk):
        site = x['site'].split('/')[0]
        if site == '1a80fac':
            meaning[k] = ('role', roles.get(val(blk[k + 1]), 'role%d' % val(blk[k + 1])), val(blk[k + 2]))
            baseval[k] = val(x)
        elif site == '1a80a2b':
            meaning[k] = ('ability', '+0x60', 0); baseval[k] = val(x); ability_value = val(x)
        elif site == '1a80ad4':
            meaning[k] = ('leadership', '+0x70', 0); baseval[k] = val(x)
    rmap = next((x['u8map'] for x in blk if x.get('name', '').endswith('.ratings')), {})
    POS = {0: 'pos_gk', 2: 'pos_dr', 3: 'pos_dl', 4: 'pos_dc', 5: 'pos_wbr', 6: 'pos_wbl', 7: 'pos_dm',
           8: 'pos_mr', 9: 'pos_ml', 10: 'pos_mc', 11: 'pos_amr', 12: 'pos_aml', 13: 'pos_amc', 14: 'pos_st'}
    MIN = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    meaning = {k: m for k, m in meaning.items()
               if m[0] != 'role' or rmap.get(POS.get(m[2], ''), 0) >= MIN}
    abilities[name] = ability_value
    blocks[name] = (meaning, baseval, sites, idbytes)

rows = list(csv.DictReader(open(S + '/' + csvname, encoding='utf-8')))
changed = collections.defaultdict(dict); flagged = set(); misaligned = set(); frm = {}; block_of = {}; ratings = []
for r in rows:
    p = r['player']; block_of[p] = r['player_block']
    if (p, r['rating']) not in frm:
        frm[(p, r['rating'])] = int(r['rating_from'])
        if r['rating'] not in ratings: ratings.append(r['rating'])
    key = (p, r['rating'], int(r['rating_to']))
    if r['field_block'] == 'structure changed':
        flagged.add(key); continue
    if r['field_block'] == r['player_block']:
        b = blocks.get(p)
        f = int(r['field'])
        if b and (b[2].get(f) != r['site'] or f in b[3]):
            misaligned.add(key)
        if r['new'].isdigit(): changed[key][f] = int(r['new'])
values = sorted({int(r['rating_to']) for r in rows if not r['rating'].startswith('pos_')})
print('players', len(block_of), 'attribute values', values, 'misaligned builds', len(misaligned), 'structure changes', len(flagged))

def slope(p, rating, f, meaning, baseval):
    scale = 1 if rating.startswith('pos_') else 5
    pts = []
    for v in (1, 10, 20):
        raw = v * scale
        if (p, rating, raw) in flagged or (p, rating, raw) in misaligned: continue
        pts.append((v, changed.get((p, rating, raw), {}).get(f, baseval[f])))
    if (p, rating) in frm:
        b = frm[(p, rating)]
        pts.append((b / scale, baseval[f]))
    if meaning[f][0] == 'role':
        # role rating = min(formula, ability): points at the cap say nothing about the weights
        pts = [(x, y) for x, y in pts if y < abilities[p]]
    if len(pts) < 2: return None
    n = len(pts); mx = sum(x for x, _ in pts) / n; my = sum(y for _, y in pts) / n
    sxx = sum((x - mx) ** 2 for x, _ in pts)
    return sum((x - mx) * (y - my) for x, y in pts) / sxx if sxx else None

attrs = [r for r in ratings if not r.startswith('pos_')]
positions = [r for r in ratings if r.startswith('pos_')]
per_role = collections.defaultdict(lambda: collections.defaultdict(list))
poscode = collections.defaultdict(collections.Counter)
special = collections.defaultdict(lambda: collections.defaultdict(list))
for p, (meaning, baseval, _s, _i) in blocks.items():
    if p not in block_of: continue
    for f, (kind, name, code) in meaning.items():
        for r in ratings:
            s = slope(p, r, f, meaning, baseval)
            if s is None: continue
            if kind == 'role':
                if r in attrs: per_role[name][r].append(s)
                elif s > 0.5: poscode[code][r] += 1
            else:
                special[name][r].append(s)

print('\n== position codes (code: position rating that moves it, count)')
for code in sorted(poscode):
    print(code, poscode[code].most_common(3))

print()
print('== role weights: median slope per in-game point (+- IQR across player-entries); negative weights after |')
summary = {}
for role in sorted(per_role):
    stats = []
    for a, xs in per_role[role].items():
        q = st.quantiles(xs, n=4) if len(xs) > 1 else [xs[0]] * 3
        stats.append((st.median(xs), q[2] - q[0], len(xs), a))
    stats.sort(reverse=True)
    summary[role] = {a: m for m, _, _, a in stats}
    n = max(c for _, _, c, _ in stats)
    negative = ', '.join('%s %.2f' % (a, m) for m, s, c, a in sorted(stats)[:4] if m < -0.15)
    print('%-26s n=%-3d %s%s' % (role, n, ', '.join('%s %.2f+-%.2f' % (a, m, s) for m, s, c, a in stats[:9] if m > 0.1),
                                  ('  | negative: ' + negative) if negative else ''))
json.dump(summary, open(S + '/role_weights.json', 'w'), indent=1)

print()
print('== special fields: median slope across players')
for name, d in special.items():
    stats = sorted(((st.median(xs), (st.quantiles(xs, n=4)[2] - st.quantiles(xs, n=4)[0]) if len(xs) > 1 else 0, a) for a, xs in d.items()), reverse=True)
    print(name, ', '.join('%s %.2f+-%.2f' % (a, m, s) for m, s, a in stats[:10] if abs(m) > 0.05))

print('\n== overall attribute importance: sum of mean weights over all roles')
tot = collections.Counter()
for role, d in summary.items():
    for a, m in d.items():
        if m > 0: tot[a] += m
for a, v in tot.most_common():
    print('%-20s %.1f' % (a, v))
