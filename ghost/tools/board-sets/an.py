import numpy as np, glob, os, json
sr=48000; F={}
r=lambda s: 20*np.log10(np.sqrt(np.mean(s*s))+1e-12)
for f in sorted(glob.glob('*.f32')):
    x=np.fromfile(f,dtype=np.float32).astype(float); n=f[:-4]
    early=r(x[int(.02*sr):int(.3*sr)]); late=r(x[int(1.5*sr):int(1.95*sr)]); sus=r(x[int(.1*sr):int(1.95*sr)]); rel=r(x[int(2.2*sr):int(2.6*sr)])
    loud=max(r(x[int(.02*sr):int(1.0*sr)]), r(x[int(.6*sr):int(1.95*sr)]))
    W=960; env=np.sqrt(np.convolve(x*x,np.ones(W)/W,'valid')[::W]); att=np.argmax(env>=0.5*env.max())*0.02
    seg=x[int(0.05*sr):int(0.85*sr)]; sp=np.abs(np.fft.rfft(seg*np.hanning(len(seg)),4*len(seg))); fr=np.fft.rfftfreq(4*len(seg),1/sr)
    m=(fr>15)&(fr<3000); s=sp[m]; ff=fr[m]; pk=[i for i in range(1,len(s)-1) if s[i]>s[i-1] and s[i]>=s[i+1] and s[i]>s.max()*10**(-20/20)]
    low=ff[pk[0]] if pk else 0
    psp=sp**2; cen=float((psp*fr).sum()/psp.sum())
    F[n]=dict(loud=loud,early=early,late=late,rel=rel,att=att,low=low,cen=cen)
    print(f"{n:24s} loud {loud:6.1f} dec {late-early:6.1f} rel {rel-sus:7.1f} att {att:4.2f} low {low:6.1f} cen {cen:6.0f}")
json.dump(F,open('feat.json','w'))
