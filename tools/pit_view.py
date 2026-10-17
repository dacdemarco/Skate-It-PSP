import sys, os, glob
sys.path.insert(0,'tools')
import numpy as np, oct as octm, pvs_tex, dst, render_level
from PIL import Image
level="Amphitheatre"; lvl="extracted/unpacked/DATA/LOCATIONS/%s/%s/"%(level,level)
table=pvs_tex.resolve_textures(level); nverts=len(octm.load_cmn(lvl+"verts.cmn"))
tex=[]
for p in table:
    if p is None: tex.append(None); continue
    bb=open(p,"rb").read(); tex.append(None if len(bb)<18 else np.asarray(dst.to_image(dst.decode(bb)).convert("RGBA")))
def tris_for(layers, box):
    out=[]
    for f in sorted(glob.glob(lvl+"*.oct")):
        if os.path.basename(f)[0] not in layers: continue
        b=open(f,"rb").read(); S,O=octm.scale(b),octm.origin(b)
        for lf in octm.leaves(b,nverts):
            for tid,ts in octm.batches(b,lf):
                for t in ts:
                    P=[tuple(O[a]+v[0][a]*S for a in range(3)) for v in t]
                    c=np.mean(P,axis=0)
                    if box[0]<=c[0]<=box[1] and box[2]<=c[2]<=box[3]: out.append((P,[v[1] for v in t],tid))
    return out
box=(-80,-35,30,70)
for name,layers in (("layer1","1"),("layer1+4","14")):
    T=tris_for(layers,box)
    render_level.render(tex,T,"pit_%s.png"%name,yaw=200,pitch=38,zoom=16,W=640,H=420,center=(-59,-66,54))
    print(name,len(T),"triangles")
a=Image.open("pit_layer1.png"); b=Image.open("pit_layer1+4.png")
s=Image.new("RGB",(1280,420)); s.paste(a,(0,0)); s.paste(b,(640,0)); s.save("pit_compare.png")
