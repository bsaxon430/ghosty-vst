import numpy as np, json, itertools, sys
C=json.load(open('corr.json')); R={tuple(k.split('|')):v for k,v in C['R'].items()}
cp=[(0.13,0.20),(0.87,0.20),(0.13,0.80),(0.87,0.80)]
W=[]
for py in np.linspace(0.2,0.8,7):
    for px in np.linspace(0.13,0.87,7):
        w=[]
        for cx,cy in cp:
            d=np.hypot(px-cx,py-cy); e=max(0,d-0.06); w.append(1e8 if e<1e-4 else 1/(e*e))
        s=sum(w); W.append([x/s for x in w])
S=np.sqrt(np.array(W))
def spread(q):
    M=np.array([[R[(a,b)] for b in q] for a in q]); p=np.einsum('gi,ij,gj->g',S,M,S); db=10*np.log10(np.maximum(p,1e-6)); return db.min(),db.max()
def best(cands, must=()):
    res=[]
    for q in itertools.combinations(cands,4):
        if not all(m in q for m in must): continue
        bq=min(itertools.permutations(q), key=lambda o: max(-spread(o)[0], spread(o)[1]))
        lo,hi=spread(bq); res.append((max(-lo,hi),bq,lo,hi))
    res.sort(); return res[:5]
groups=json.loads(sys.argv[1])
for name,(cands,must) in groups.items():
    print(name)
    for sc,q,lo,hi in best(cands,must): print("  ",round(sc,2),q,round(lo,1),round(hi,1))
