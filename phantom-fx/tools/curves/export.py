# Exports every .bddpreset to a GhostBlock curve set: "GBCV", u32 version=1, u32 lanes, u32 size, then lanes*size float32 (LE),
# lanes ordered by drive ([+N] in the source name; base = 0). The file is zlib-compressed as a whole (.gbcurves).
import sys,glob,os,re,zlib,struct,json; sys.path.insert(0,os.path.dirname(__file__)); import bdd, numpy as np
src,dst=sys.argv[1],sys.argv[2]; os.makedirs(dst,exist_ok=True); index=[]
N=16384
def drive(p):
    m=re.search(r'\[\+([0-9.]+)',os.path.basename(p.get('presetSourcePath',''))); return float(m.group(1)) if m else 0.0
for f in sorted(glob.glob(os.path.join(src,'*.bddpreset'))):
    t=bdd.load(f); lm=[c for c in t['children'] if c['type']=='LaneMixer'][0]
    name=os.path.basename(f)[:-10].replace('__',' (').replace('_',' ').strip()
    if '(' in name and not name.endswith(')'): name+=')'
    lanes=[]
    for L in lm['children']:
        p=L['props']
        if 'curveDataF32' in p:
            y=np.frombuffer(zlib.decompress(p['curveDataF32']),'<f4').astype(np.float32); lanes.append((drive(p),y))
        elif 'equationText' in p:
            e=p['equationText']; x=np.linspace(-1,1,N)
            for i,m in enumerate(np.linspace(0,1,9)):
                y=eval(e,{'abs':np.abs,'max':np.maximum,'min':np.minimum,'x':x,'m':m}); y=np.asarray(y*np.ones_like(x),np.float32)
                lanes.append((float(i),y))
    lanes.sort(key=lambda l:l[0])
    seen=[];uniq=[]
    for d,y in lanes:
        if any(np.array_equal(y,u) for u in seen): continue
        seen.append(y); uniq.append((d,y))
    assert all(len(y)==N for _,y in uniq) and all(np.isfinite(y).all() for _,y in uniq), name
    body=b'GBCV'+struct.pack('<III',1,len(uniq),N)+b''.join(y.tobytes() for _,y in uniq)
    fn=re.sub(r'[^A-Za-z0-9]+','_',name).strip('_')+'.gbcurves'
    open(os.path.join(dst,fn),'wb').write(zlib.compress(body,9))
    index.append({'name':name,'file':fn,'lanes':len(uniq),'drives':[d for d,_ in uniq],'equation':any('equationText' in L['props'] for L in lm['children'])})
json.dump(index,open(os.path.join(dst,'index.json'),'w'),indent=1)
print(len(index),'sets',sum(i['lanes'] for i in index),'curves')
