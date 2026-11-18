import struct, sys, json, base64
class R:
    def __init__(s,d): s.d=d; s.p=0
    def byte(s): b=s.d[s.p]; s.p+=1; return b
    def cint(s):
        n=s.byte(); neg=n&0x80; n&=0x7f
        v=int.from_bytes(s.d[s.p:s.p+n],'little'); s.p+=n
        return -v if neg else v
    def string(s):
        e=s.d.index(b'\0',s.p); v=s.d[s.p:e].decode('utf-8','replace'); s.p=e+1; return v
    def var(s):
        size=s.cint(); end=s.p+size
        if size==0: return None
        m=s.byte()
        if m==1: v=struct.unpack('<i',s.d[s.p:s.p+4])[0]
        elif m==2: v=True
        elif m==3: v=False
        elif m==4: v=struct.unpack('<d',s.d[s.p:s.p+8])[0]
        elif m==5: v=s.d[s.p:end-1].decode('utf-8','replace')
        elif m==6: v=struct.unpack('<q',s.d[s.p:s.p+8])[0]
        elif m==7:
            s2=R(s.d); s2.p=s.p; n=s2.cint(); v=[s2.var() for _ in range(n)]
        elif m==8: v=bytes(s.d[s.p:end])
        else: v=None
        s.p=end; return v
    def tree(s):
        t={'type':s.string(),'props':{},'children':[]}
        for _ in range(s.cint()):
            k=s.string(); t['props'][k]=s.var()
        for _ in range(s.cint()): t['children'].append(s.tree())
        return t
def load(path): return R(open(path,'rb').read()).tree()
def show(t,ind=0):
    pr={k:(v if not isinstance(v,(bytes,str)) or len(v)<80 else f'<{type(v).__name__} {len(v)}>') for k,v in t['props'].items()}
    if t['type']!='PARAM': print(' '*ind+t['type'],pr)
    for c in t['children']: show(c,ind+2)
if __name__=='__main__': show(load(sys.argv[1]))
