from lib import *
import sys, json, copy
exec(open('sets.py').read())
import importlib
RANGES={}
import re
src=open('/home/claude/fbot-backup/src/common/synth_parameters.cpp').read()
def rng(name):
    # strip prefixes like osc_1_ / filter_1_ / env_1_
    base=re.sub(r'^(osc|filter|env|lfo|random)_\d+_','',name)
    m=re.search(r'\{ "'+re.escape(base)+r'", 0x[0-9a-f]+, ([^,]+), ([^,]+),',src)
    if not m: m=re.search(r'\{ "'+re.escape(name)+r'", 0x[0-9a-f]+, ([^,]+), ([^,]+),',src)
    ev=lambda t: float(eval(t.replace('Distortion::kMinDrive','-30').replace('Distortion::kMaxDrive','30').replace('kNumOscillatorWaveFrames','257').replace('f','') if 'kNum' not in t else t.replace('kNumOscillatorWaveFrames','257')))
    return ev(m.group(1)), ev(m.group(2))
def apply(settings, ops):
    s=settings
    for op in ops:
        k,n,v=op[0],op[1],op[2]
        lo,hi=rng(n)
        if k=='add': s[n]=s[n]+v
        elif k=='set': s[n]=v
        elif k=='mul': s[n]=s[n]*v
        s[n]=min(hi,max(lo,s[n]))
out=[]; tag=0
for S in SETS:
    base=load(S['base']); L0=loud(render(base,tag='b')); sp0=spec(render(base,tag='b'))
    gl=[]
    for gname,ops in S['ghosts']:
        d=copy.deepcopy(base); apply(d['settings'],ops)
        # level: match the base (secant on volume)
        v=d['settings']['volume']; L=loud(render(d,tag='g'))
        for it in range(4):
            if abs(L-L0)<0.3: break
            v2=min(7399.44,max(0,v*10**((L0-L)/86)))
            d['settings']['volume']=v2; L2=loud(render(d,tag='g'))
            v,L=v2,L2
        sp=spec(render(d,tag='g')); diff=float(np.mean(np.abs(sp-sp0)))
        changed={op[1]:d['settings'][op[1]] for op in ops}; changed['volume']=d['settings']['volume']
        gl.append(dict(name=gname,values=changed,loud=L,diff=diff,settings=d['settings']))
    # midpoints: centre of the board (all four ghosts equally) in the knob domain
    mid=copy.deepcopy(base)
    keys=set(k for g in gl for k in g['values'])
    for k in keys: mid['settings'][k]=float(np.mean([g['settings'][k] for g in gl]))
    Lm=loud(render(mid,tag='m'))
    print(f"{S['name']:16s} base {L0:6.1f} ghosts "+" ".join(f"{g['name']}:{g['loud']:.1f}/d{g['diff']:.1f}" for g in gl)+f"  centre {Lm:.1f}")
    out.append(dict(S, ghosts=[dict(name=g['name'],values=g['values']) for g in gl]))
json.dump(out,open('gfinal.json','w'),indent=1)
