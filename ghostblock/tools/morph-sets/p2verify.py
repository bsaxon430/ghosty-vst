import numpy as np, subprocess, os, json, sys
exec(open('p2sets.py').read())
FP='/home/claude/polt/resources/factory_patches/'; sr=48000
def render(p,key):
    os.makedirs('vr',exist_ok=True)
    out=f"vr/{p.replace('/','__')}.sxsnp.f32"; k=f'vr/{key}'
    subprocess.run(['/home/claude/render/render',FP+p+'.sxsnp'],env=dict(os.environ,OUT='vr',KEY=str(key)),capture_output=True)
    cat,f=p.split('/'); return np.fromfile(f'vr/{cat}__{f}.sxsnp.f32',dtype=np.float32).astype(float)
def loud(x):
    # loudness over the held note: the louder of the first second and the swell
    a=x[int(0.02*sr):int(1.0*sr)]; b=x[int(0.6*sr):int(1.95*sr)]
    r=lambda s: 20*np.log10(np.sqrt(np.mean(s*s))+1e-12)
    return max(r(a),r(b))
res=[]
for name,desc,orb,rate,size,cs in SETS:
    xs=[];vols=[]
    for p,o in cs:
        x=render(p,48+12*o); xs.append(x); vols.append(-loud(x))
    mv=np.mean(vols); vols=[v-mv for v in vols]; sh=min(0.0, 6-max(vols)); base=-20-0  # equal corners, then the set's level
    Ls=[loud(x) for x in xs]; tgt=np.mean(Ls)+np.mean(vols)  # (unused)
    vols=[float(np.clip(round(v+sh,1),-24,6)) for v in vols]
    # mix at a 5x5 grid of puck points
    lv=[]
    for py in np.linspace(0,1,5):
        for px in np.linspace(0,1,5):
            w=[(1-px)*(1-py),px*(1-py),(1-px)*py,px*py]
            m=sum(np.sqrt(wi)*10**(vi/20)*xi for wi,vi,xi in zip(w,vols,xs))
            lv.append(loud(m))
    cornersL=[loud(10**(v/20)*x) for v,x in zip(vols,xs)]
    print(f"{name:18s} vols {vols}  grid loud {min(lv):6.1f}..{max(lv):6.1f} (spread {max(lv)-min(lv):.1f} dB) corners {[round(c,1) for c in cornersL]}")
    res.append(dict(name=name,vols=vols))
json.dump(res,open('p2vols.json','w'))
