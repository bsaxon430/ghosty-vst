from lib import *
import json, copy, itertools
exec(open('build.py').read().split('out=[]; tag=0')[0])
def ghost(base, ops, L0, scale):
    sops=[]
    for op in ops:
        k,n,v=op
        if k=='add': sops.append((k,n,v*scale))
        elif k=='mul': sops.append((k,n,1+(v-1)*scale))
        else: sops.append(('set',n,base['settings'][n]+(v-base['settings'][n])*scale))
    d=copy.deepcopy(base); apply(d['settings'],sops)
    raw=loud(render(d,tag='g')); v=d['settings']['volume']; L=raw
    for it in range(5):
        if abs(L-L0)<0.25: break
        v=min(7399.44,max(0,v*10**((L0-L)/86))); d['settings']['volume']=v; L=loud(render(d,tag='g'))
    return d, raw-L0, L
import sys
prev={x['name']:x for x in json.load(open('gfinal.json'))}
out=[]
for S in SETS:
    if sys.argv[1:] and S['name'] not in sys.argv[1:]:
        out.append(prev[S['name']]); continue
    base=load(S['base']); L0=loud(render(base,tag='b')); sp0=spec(render(base,tag='b'))
    scales=[1.0]*4; 
    for rnd in range(5):
        full=[]; comp=[]
        for gi,(gname,ops) in enumerate(S['ghosts']):
            d,c,L=ghost(base,ops,L0,scales[gi]); full.append(d['settings']); comp.append(c)
        keys=set(op[1] for _,ops in S['ghosts'] for op in ops)|{'volume'}
        worst={}
        for i,j in itertools.combinations(range(4),2):
            d=copy.deepcopy(base)
            for k in keys: d['settings'][k]=(full[i][k]+full[j][k])/2
            dl=loud(render(d,tag='m'))-L0
            for g in (i,j): worst[g]=max(worst.get(g,0),abs(dl))
        # also the centre
        d=copy.deepcopy(base)
        for k in keys: d['settings'][k]=float(np.mean([f[k] for f in full]))
        dc=loud(render(d,tag='m'))-L0
        bad=[g for g in range(1,4) if worst.get(g,0)>1.5]
        if not bad and abs(dc)<1.5: break
        if not bad: bad=[max(range(1,4),key=lambda g:worst.get(g,0))]
        for g in bad: scales[g]*=0.7
    diffs=[float(np.mean(np.abs(spec(render(dict(base,settings=f),tag='g'))-sp0))) for f in full]
    print(f"{S['name']:16s} scales {[round(x,2) for x in scales]} comp {[round(c,1) for c in comp]} worst-pair {max(worst.values()):.1f} centre {dc:+.1f} diff {[round(x,1) for x in diffs]}", flush=True)
    gh=[]
    for gi,(gname,ops) in enumerate(S['ghosts']):
        vals={op[1]:full[gi][op[1]] for op in ops}; vals['volume']=full[gi]['volume']
        gh.append(dict(name=gname,values=vals))
    out.append(dict(S,ghosts=gh))
json.dump(out,open('gfinal.json','w'),indent=1)
