import math, numpy as np
from shapely.geometry import Polygon, MultiPolygon
from shapely.ops import unary_union
from shapely import affinity

def catmull(pts, n=24):
    pts=[np.array(p,float) for p in pts]
    P=[pts[0]]+pts+[pts[-1]]
    out=[]
    for i in range(1,len(P)-2):
        p0,p1,p2,p3=P[i-1],P[i],P[i+1],P[i+2]
        for k in range(n):
            t=k/n; t2=t*t; t3=t2*t
            out.append(0.5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t2+(-p0+3*p1-3*p2+p3)*t3))
    out.append(P[-2]); return np.array(out)

def lines(pts, n=30):
    out=[]
    for a,b in zip(pts[:-1],pts[1:]):
        a=np.array(a,float); b=np.array(b,float)
        for k in range(n): out.append(a+(b-a)*k/n)
    out.append(np.array(pts[-1],float)); return np.array(out)

def resample(P, step=1.5):
    d=np.r_[0,np.cumsum(np.linalg.norm(np.diff(P,axis=0),axis=1))]
    L=d[-1]; m=max(int(L/step),2)
    s=np.linspace(0,L,m)
    return np.c_[np.interp(s,d,P[:,0]),np.interp(s,d,P[:,1])]

def nib_stroke(P, W, ang=25, taper=(0.12,0.12), minw=2.5, wob=0.0):
    """broad-edged nib swept along P"""
    P=resample(P); n=len(P); a=math.radians(ang)
    polys=[]
    for i in range(n-1):
        t=i/(n-1)
        f=1.0
        if taper[0]>0 and t<taper[0]: f=0.45+0.55*math.sin(t/taper[0]*math.pi/2)
        if taper[1]>0 and t>1-taper[1]: f=0.45+0.55*math.sin((1-t)/taper[1]*math.pi/2)
        w=W*f*(1+wob*math.sin(i*0.21))/2
        v=np.array([math.cos(a),-math.sin(a)])*w
        p0,p1=P[i],P[i+1]
        q=Polygon([p0+v,p1+v,p1-v,p0-v])
        if q.is_valid and q.area>0: polys.append(q)
    g=unary_union(polys)
    return g.buffer(minw/2, quad_segs=6)

def wisp_stroke(P, r0, r1, curve=lambda t: t, step=1.2):
    """pointed-pen tapering wisp: radius from r0 to r1"""
    P=resample(P,step); n=len(P)
    polys=[]
    for i in range(n-1):
        t=i/(n-1); t2=(i+1)/(n-1)
        ra=r0+(r1-r0)*curve(t); rb=r0+(r1-r0)*curve(t2)
        p0,p1=P[i],P[i+1]; d=p1-p0; L=np.linalg.norm(d)
        if L==0: continue
        nrm=np.array([-d[1],d[0]])/L
        polys.append(Polygon([p0+nrm*ra,p1+nrm*rb,p1-nrm*rb,p0-nrm*ra]))
    return unary_union(polys)

def to_path(g, prec=1):
    if g.is_empty: return ""
    geoms = g.geoms if hasattr(g,'geoms') else [g]
    out=[]
    for poly in geoms:
        for ring in [poly.exterior]+list(poly.interiors):
            c=list(ring.coords)
            out.append("M"+" L".join(f"{x:.{prec}f} {y:.{prec}f}" for x,y in c[:-1])+"Z")
    return " ".join(out)
