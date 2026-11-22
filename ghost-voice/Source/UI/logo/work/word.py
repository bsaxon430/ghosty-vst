import sys, math, numpy as np
sys.path.insert(0,'/home/claude/studio-logos/lettered/work')
from letters import *
from shapely import affinity
from shapely.ops import unary_union

NW=46   # nib width at cap height 200
C=catmull; Lz=lines
ANG=18

def G(): return 175,[ ('n',C([(150,52),(128,14),(84,4),(38,28),(14,96),(30,168),(80,198),(130,186),(154,150),(152,112)])),
                     ('n',Lz([(104,114),(166,110)])) ]
def H(): return 172,[ ('n',Lz([(30,6),(28,196)])),('n',Lz([(142,4),(144,196)])),
                     ('n',C([(26,106),(70,96),(104,104),(146,94)])) ]
def O(): 
    pts=[(96,4)]+[(90+78*math.sin(a),100-97*math.cos(a)) for a in np.linspace(0.15,2*math.pi+0.5,16)]
    return 180,[('n',C(pts))]
def S(): return 140,[('n',C([(124,40),(100,8),(58,4),(24,30),(28,74),(72,100),(114,126),(122,168),(92,197),(46,197),(12,162)]))]
def T(): return 164,[('n',C([(4,24),(30,8),(82,14),(130,10),(162,20)])),('n',Lz([(82,14),(84,196)]))]
def V(): return 176,[('n',Lz([(6,4),(88,198)])),('n',Lz([(88,198),(170,2)]))]
def I(): return 56,[('n',Lz([(28,4),(28,196)]))]
def Cc(): return 160,[('n',C([(148,48),(122,10),(78,4),(34,30),(14,100),(32,168),(80,198),(126,188),(152,156)]))]
def E(): return 146,[('n',Lz([(24,6),(24,196)])),('n',Lz([(22,8),(132,6)])),
                    ('n',Lz([(24,100),(108,100)])),('n',Lz([(22,194),(140,194)]))]
GL={'G':G,'H':H,'O':O,'S':S,'T':T,'V':V,'I':I,'C':Cc,'E':E}

def build(text="GHOST VOICE", track=10, space=70, jitter=True, arch=0.0, seed=3):
    rng=np.random.default_rng(seed)
    x=0; shapes=[]; boxes=[]
    for ch in text:
        if ch==' ': x+=space; continue
        w,strokes=GL[ch]()
        gs=[nib_stroke(P,NW,ang=ANG,minw=3) for k,P in strokes]
        g=unary_union(gs).buffer(1.2).buffer(-1.2)
        if jitter:
            g=affinity.rotate(g, rng.uniform(-3.5,3.5), origin=(w/2,100))
            g=affinity.translate(g,0,rng.uniform(-7,7))
        g=affinity.translate(g,x,0)
        shapes.append(g); boxes.append((x,w)); x+=w+track
    return shapes, x-track, boxes

def arch_warp(g, total, depth):
    # bend letters onto an arc (ouija)
    from shapely.ops import transform
    R = (total**2)/(8*depth)+depth/2 if depth>0 else None
    cx=total/2
    def f(xs,ys):
        xs=np.asarray(xs); ys=np.asarray(ys)
        th=(xs-cx)/R; r=R-(200-ys)
        return cx+ (R+0)*0 + r*np.sin(th), (R+200-R) + (R - r*np.cos(th)) - 0 + (0)
    return transform(lambda xs,ys,z=None: f(xs,ys), g)
