"""Response curves of one player's role ratings from a `sensitivity --values 1-20` CSV.

    python -I curves.py <dir> <sweep.csv> "<player name prefix>" role1,role2,...

<dir> holds the CSV, roles.json and base1.json (see analyse.py). Prints every rating that moves a
listed role entry by 3 or more, as its value at in-game 1..20 (X = build skipped: structure changed
or misaligned).
"""
import csv,json,sys,collections
S,csvname,who=sys.argv[1],sys.argv[2],sys.argv[3]; want=sys.argv[4].split(',')
roles={int(k):v for k,v in json.load(open(S+'/roles.json')).items()}; roles[31]='pressing_forward'
POS={0:'gk',2:'dr',3:'dl',4:'dc',5:'wbr',6:'wbl',7:'dm',8:'mr',9:'ml',10:'mc',11:'amr',12:'aml',13:'amc',14:'st'}
base=json.load(open(S+'/base1.json',encoding='utf-8'))['fields']
st=[i for i,x in enumerate(base) if 'player' in x]+[len(base)]
for a,b in zip(st,st[1:]):
    if base[a]['player'].startswith(who): blk=base[a:b]; break
ab=next(y['u16'] for y in blk if y['site'].startswith('1a80a2b/'))
sites={k:x['site'] for k,x in enumerate(blk)}
ent={}
for k in range(len(blk)-2):
    if blk[k]['site'].startswith('1a80fac/'):
        n='%s@%s'%(roles.get(blk[k+1]['u8']),POS.get(blk[k+2]['u8']))
        if n.split('@')[0] in want: ent[k]=(n,blk[k]['u16'])
idb={k+1 for k in ent}|{k+2 for k in ent}
rows=list(csv.DictReader(open(S+'/'+csvname,encoding='utf-8')))
ch=collections.defaultdict(dict); bad=set(); frm={}
for r in rows:
    key=(r['rating'],int(r['rating_to'])); frm[r['rating']]=int(r['rating_from'])
    if r['field_block']=='structure changed': bad.add(key); continue
    if r['field_block']!=r['player_block']: continue
    f=int(r['field'])
    if sites.get(f)!=r['site']: bad.add(key)
    if r['new'].isdigit(): ch[key][f]=int(r['new'])
print(who,'ability',ab)
for f,(n,bv) in ent.items():
    print('\n%s  baseline %d'%(n,bv))
    for rating in frm:
        sc=1 if rating.startswith('pos_') else 5
        ys=[]
        for v in range(1,21):
            raw=v*sc
            if (rating,raw) in bad: ys.append('  X')
            elif raw==frm[rating]: ys.append('%3d*'%bv if False else '%3d'%bv)
            else: ys.append('%3d'%ch.get((rating,raw),{}).get(f,bv))
        nums=[int(y) for y in ys if y.strip()!='X']
        if max(nums)-min(nums)>=3: print('  %-16s %s'%(rating,' '.join(ys)))
