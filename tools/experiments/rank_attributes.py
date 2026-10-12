"""Ranks attributes by their effect on match results (per-attribute experiment)."""
import csv, math, os, statistics as st, sys
d, base = sys.argv[1], sys.argv[2]

def load(path):
    rows = list(csv.DictReader(open(path)))
    h = [int(r['home_goals']) for r in rows]; a = [int(r['away_goals']) for r in rows]
    gd = [x - y for x, y in zip(h, a)]
    pts = [3 if x > y else 1 if x == y else 0 for x, y in zip(h, a)]
    return dict(n=len(rows), h=h, a=a, gd=gd, pts=pts)

def ms(x): return st.mean(x), st.pstdev(x) / math.sqrt(len(x))

b = load(base)
bm = {k: ms(b[k]) for k in ('h', 'a', 'gd', 'pts')}
out = []
for name in open(os.path.join(d, 'list.txt')).read().split():
    p = os.path.join(d, name + '.csv')
    if not os.path.exists(p): continue
    r = load(p)
    if r['n'] < 50: continue
    e = {}
    for k in ('h', 'a', 'gd', 'pts'):
        m, s = ms(r[k]); e[k] = (m - bm[k][0], math.hypot(s, bm[k][1]))
    out.append((e['gd'][0], name, r['n'], e))
out.sort()
print('baseline: n=%d  Villa %.2f  Bristol %.2f  GD %+.2f  pts %.2f' % (b['n'], bm['h'][0], bm['a'][0], bm['gd'][0], bm['pts'][0]))
print('effect of -5 (in-game) on every Villa player, vs baseline; z = effect / SE')
print('%-18s %4s  %-14s %-14s %-14s %-12s %s' % ('attribute', 'n', 'goal diff', 'Villa goals', 'Bristol goals', 'points', 'z(GD)'))
with open(os.path.join(d, 'ranking.csv'), 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['attribute', 'n', 'gd_effect', 'gd_se', 'villa_goals_effect', 'bristol_goals_effect', 'points_effect'])
    for gd, name, n, e in out:
        z = e['gd'][0] / e['gd'][1]
        print('%-18s %4d  %+.2f +- %.2f   %+.2f +- %.2f   %+.2f +- %.2f   %+.2f +- %.2f  %+.1f' % (
            name, n, *e['gd'], *e['h'], *e['a'], *e['pts'], z))
        w.writerow([name, n, '%.3f' % e['gd'][0], '%.3f' % e['gd'][1], '%.3f' % e['h'][0], '%.3f' % e['a'][0], '%.3f' % e['pts'][0]])
