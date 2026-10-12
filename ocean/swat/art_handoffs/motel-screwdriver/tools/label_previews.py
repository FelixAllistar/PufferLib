"""Python 3 + Pillow. Adds labels outside rendered image; does not retouch asset."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
fontpath='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
try:f=ImageFont.truetype(fontpath,27);small=ImageFont.truetype(fontpath,19)
except OSError:f=small=ImageFont.load_default()
labels={'01_front':'FRONT | MAIN mesh only','02_back':'BACK / opposite surface | MAIN only','03_tip':'FLATHEAD TIP | source geometry','04_collar':'COLLAR | uniform source mapping preserved'}
thumbs=[]
for name,label in labels.items():
 p=root/'previews'/f'{name}.png';im=Image.open(p).convert('RGB');out=Image.new('RGB',(im.width,im.height+85),(25,29,35));out.paste(im,(0,85));d=ImageDraw.Draw(out);d.text((22,12),label,font=f,fill='white');d.text((22,49),'250 mm editorial length. Rendered from runtime GLB. No verified physical scale.',font=small,fill=(195,203,213));out.save(root/'previews'/f'{name}_labelled.png');out.thumbnail((600,651));thumbs.append(out)
sheet=Image.new('RGB',(1200,1302),(25,29,35))
for i,im in enumerate(thumbs):sheet.paste(im,((i%2)*600,(i//2)*651))
sheet.save(root/'previews/CONTACT_SHEET.png')
