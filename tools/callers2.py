import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdis
def callers(mods, target):
    out=[]
    for name,(base,b) in mods.items():
        # ARM: bl (cond 0xEB) / blx imm (0xFA/0xFB)
        for o in range(0,len(b)-3,4):
            w=struct.unpack_from("<I",b,o)[0]
            top=w>>24
            if top in (0xEB,0xFA,0xFB):
                imm=w&0xFFFFFF
                if imm&0x800000: imm-=1<<24
                tgt=base+o+8+imm*4+(2 if top==0xFB else 0)*1
                if top==0xFB: tgt=base+o+8+imm*4+2
                if (tgt&~1)==target: out.append((name,'arm',hex(base+o)))
        # Thumb pairs
        for o in range(0,len(b)-3,2):
            h1,h2=struct.unpack_from("<HH",b,o)
            if (h1>>11)==0x1E and (h2>>11) in (0x1F,0x1D,0x1C if False else 0x1F):
                imm=((h1&0x7FF)<<11)|(h2&0x7FF)
                if imm&0x200000: imm-=1<<22
                tgt=base+o+4+imm*2
                if (h2>>11)==0x1D: tgt&=~3
                if tgt==target: out.append((name,'thumb',hex(base+o)))
    return out
if __name__=="__main__":
    mods=armdis.modules()
    for a in sys.argv[1:]:
        t=int(a,16); print(hex(t),callers(mods,t))
