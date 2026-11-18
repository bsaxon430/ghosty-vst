# Packs export.py's .gbcurves (16384 points, zlib) into assets/curves/*.gbc (8193 points, raw) + index.json.
# usage: python3 pack.py <export dir> <assets/curves>
import os,sys,zlib,struct,json
import numpy as np
src,dst=sys.argv[1],sys.argv[2]; os.makedirs(dst,exist_ok=True)
idx=json.load(open(os.path.join(src,'index.json'))); out=[]
for e in idx:
    b=zlib.decompress(open(os.path.join(src,e['file']),'rb').read())
    assert b[:4]==b'GBCV'; ver,lanes,N=struct.unpack('<III',b[4:16])
    a=np.frombuffer(b[16:],'<f4').reshape(lanes,N)
    r=np.concatenate([a[:,::2], a[:,-1:]],axis=1).astype('<f4')   # every other point, both ends kept
    fn=e['file'].replace('.gbcurves','.gbc')
    open(os.path.join(dst,fn),'wb').write(b'GBCV'+struct.pack('<III',1,lanes,r.shape[1])+r.tobytes())
    out.append({'name':e['name'],'file':fn,'lanes':lanes,'drives':e['drives']})
json.dump(out,open(os.path.join(dst,'index.json'),'w'),indent=1)
print(len(out),'sets',sum(o['lanes'] for o in out),'curves')
