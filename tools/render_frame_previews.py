import sys, glob, os, numpy as np
from PIL import Image
sys.path.insert(0,'/home/claude/nifconv')
import render as R
# render with alpha: patch background detection via zbuffer
def render_rgba(nif, texroot, picture, W, H):
    img=R.render(nif, texroot, picture, W, H)  # background (60,60,70)
    a=np.asarray(img).astype(int)
    mask=~((a[...,0]==60)&(a[...,1]==60)&(a[...,2]==70))
    hole=(a[...,0]>90)&(a[...,1]<50)&(a[...,2]>90)&mask
    alpha=(mask&~hole)*255
    rgba=np.dstack([a, alpha.astype(int)]).astype(np.uint8)
    return Image.fromarray(rgba,'RGBA')
# neutral canvas: soft dark warm gradient
cw,ch=256,192
yy,xx=np.mgrid[0:ch,0:cw]
g=np.exp(-(((xx-cw*0.5)/cw)**2+((yy-ch*0.45)/ch)**2)*3.5)
canvas=np.dstack([255+0*g,0*g,255+0*g])  # magenta: cut out afterwards
out=sys.argv[1]; files=sorted(glob.glob('/tmp/claude-0/pkg/Data/Meshes/Hearthframe/*.nif'))
only=sys.argv[2:] 
for f in files:
    stem=os.path.basename(f)[:-4]
    if only and stem not in only: continue
    W,H=(560,420) if stem.startswith('Landscape') else (440,440) if stem.startswith('Square') else (400,520)
    im=render_rgba(f,'/tmp/claude-0/pkg/Data/Textures/ax/paintings',canvas,W,H)
    bb=im.getbbox(); im=im.crop(bb)
    # hole: transparent pixels inside the frame (centre row/column scan)
    al=np.asarray(im)[...,3]; h,w=al.shape; cy,cx=h//2,w//2
    x0=cx; 
    while x0>0 and al[cy,x0-1]==0: x0-=1
    x1=cx
    while x1<w-1 and al[cy,x1+1]==0: x1+=1
    y0=cy
    while y0>0 and al[y0-1,cx]==0: y0-=1
    y1=cy
    while y1<h-1 and al[y1+1,cx]==0: y1+=1
    im.thumbnail((320,320),Image.LANCZOS); k=im.size[0]/w
    im.save(f'{out}/{stem}.png')
    with open(f'{out}/holes.txt','a') as fh: fh.write(f'{stem} {im.size[0]} {im.size[1]} {x0*k:.1f} {y0*k:.1f} {(x1+1)*k:.1f} {(y1+1)*k:.1f}\n')
    print(stem, im.size, flush=True)
