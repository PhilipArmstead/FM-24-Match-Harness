import json,sys
d,factor,out=sys.argv[1],float(sys.argv[2]),sys.argv[3]
cfg=json.load(open(d+'/A.json',encoding='utf-8'))
for f in cfg['fields']:
    if f.get('name','').startswith('home.player[') and f['name'].endswith('.ratings'):
        m=f['u8map']
        for k in list(m):
            if not k.startswith('pos_'): m[k]=max(1,round(m[k]*factor))
json.dump(cfg,open(d+'/'+out,'w',encoding='utf-8'),ensure_ascii=False,indent=2)
