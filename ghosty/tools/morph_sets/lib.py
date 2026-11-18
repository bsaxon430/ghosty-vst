import json, subprocess, numpy as np, os, wave, copy
H='/home/claude/gbuild/UpstreamHeadless_artefacts/Release/UpstreamHeadless'
P='/home/claude/fbot-backup/ghosty/presets/'
def load(name):
    import glob
    f=[x for x in glob.glob(P+'*.vital') if x.split('/')[-1][4:-6]==name][0]
    return json.load(open(f))
def render(d, note=48, secs=2.0, tag='x'):
    fn=f'/tmp/gp_{tag}.vital'; json.dump(d,open(fn,'w'))
    out=f'/tmp/gp_{tag}.wav'
    if os.path.exists(out): os.remove(out)
    subprocess.run([H,'--headless','-o',out,'-l',str(secs),'-m',str(note),fn],capture_output=True,cwd='/tmp')
    w=wave.open(out); n=w.getnframes(); x=np.frombuffer(w.readframes(n),dtype=np.int16).reshape(-1,2).astype(float)/32768
    return x
def loud(x, sr=44100):
    m=x.mean(1); a=m[int(0.02*sr):int(1.0*sr)]; b=m[int(0.6*sr):int(1.95*sr)]
    r=lambda s: 20*np.log10(np.sqrt(np.mean(s*s))+1e-12)
    return max(r(a),r(b))
def spec(x,sr=44100):
    m=x.mean(1)[int(0.1*sr):int(1.9*sr)]
    sp=np.abs(np.fft.rfft(m*np.hanning(len(m))))**2; fr=np.fft.rfftfreq(len(m),1/sr)
    e=np.geomspace(40,16000,25); b=np.array([sp[(fr>=a)&(fr<c)].sum() for a,c in zip(e[:-1],e[1:])])+1e-20
    return 10*np.log10(b/b.sum())
