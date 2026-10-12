import csv,sys,statistics as st,math
d=sys.argv[1]
data={}
for c in 'ACDR':
    rows=list(csv.DictReader(open('%s/%s.csv'%(d,c))))
    h=[int(r['home_goals']) for r in rows]; a=[int(r['away_goals']) for r in rows]
    gd=[x-y for x,y in zip(h,a)]
    pts=[3 if x>y else 1 if x==y else 0 for x,y in zip(h,a)]
    data[c]=dict(n=len(rows),h=h,a=a,gd=gd,pts=pts)
def m(x): return st.mean(x)
def se(x): return st.pstdev(x)/math.sqrt(len(x))
print('config  raw      derived   n    Villa goals   Bristol goals  goal diff     Villa pts/game  W-D-L')
lab={'A':('base','base'),'C':('EDITED','base'),'D':('base','EDITED'),'R':('EDITED','EDITED')}
for c,v in data.items():
    w=sum(p==3 for p in v['pts']); dr=sum(p==1 for p in v['pts']); l=v['n']-w-dr
    print('%-7s %-8s %-9s %-4d %.2f+-%.2f     %.2f+-%.2f     %+.2f+-%.2f    %.2f+-%.2f       %d-%d-%d'%(c,*lab[c],v['n'],m(v['h']),se(v['h']),m(v['a']),se(v['a']),m(v['gd']),se(v['gd']),m(v['pts']),se(v['pts']),w,dr,l))
print('\neffects on goal difference (2x2):')
g={c:m(data[c]['gd']) for c in data}; s={c:se(data[c]['gd']) for c in data}
raw=((g['C']+g['R'])-(g['A']+g['D']))/2; der=((g['D']+g['R'])-(g['A']+g['C']))/2; inter=(g['R']-g['D'])-(g['C']-g['A'])
sm=math.sqrt(sum(x*x for x in s.values()))/2
print('  raw attributes %+.2f +- %.2f'%(raw,sm)); print('  derived values %+.2f +- %.2f'%(der,sm)); print('  interaction    %+.2f +- %.2f'%(inter,2*sm))
