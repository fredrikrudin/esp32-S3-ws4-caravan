# esp32-S3-ws4-caravan v1.0
import sys, glob, os, numpy as np
from PIL import Image
d = sys.argv[1]
for f in sorted(glob.glob(d + "/*.raw")):
    a = np.fromfile(f, dtype="<u2").reshape(480, 480).astype(np.uint32)
    r = ((a >> 11) & 31) * 255 // 31; g = ((a >> 5) & 63) * 255 // 63; b = (a & 31) * 255 // 31
    Image.fromarray(np.dstack([r, g, b]).astype(np.uint8)).save(f[:-4] + ".png")
names = [os.path.basename(f)[:-4] for f in sorted(glob.glob(d + "/*.png")) if "sheet" not in f]
ims = [Image.open(f"{d}/{n}.png") for n in names]
cols = 4; rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 490, rows * 490), "white")
for i, im in enumerate(ims): sheet.paste(im, ((i % cols) * 490, (i // cols) * 490))
sheet.save(d + "/sheet.png")
print(names)
