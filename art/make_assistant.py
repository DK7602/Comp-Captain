# Builds ui/img/assistant.png (the Assistant badge, both looks) from the owner's art/assistant.webp:
# trimmed to what's visible, faint specks removed, sized for sharp screens (2x the size it's shown at).
#   python3 art/make_assistant.py      (needs Pillow, numpy, scipy)
# (The first badge, art/assistant.png, was drawn for a dark background; the owner replaced it on 2026-11-23.)
from PIL import Image
import numpy as np
from scipy import ndimage

SHOWN_WIDTH = 272  # px on the 1672 x 941 stage (see .assistant in ui/css/app.css)

im = np.array(Image.open('art/assistant.webp').convert('RGBA')).astype(float)
a = im[:, :, 3]

# faint specks: barely-there pixels, and small islands away from the artwork
a = np.where(a < 12, 0, a)
solid = a > 40
labels, n = ndimage.label(ndimage.binary_dilation(solid, iterations=2))
sizes = ndimage.sum(solid, labels, range(1, n + 1))
keep = np.isin(labels, np.nonzero(sizes >= 60)[0] + 1)
a = np.where(keep, a, 0)
im[:, :, 3] = a

ys, xs = np.nonzero(a > 8)
pad = 6
box = (max(0, xs.min() - pad), max(0, ys.min() - pad), min(im.shape[1], xs.max() + pad + 1), min(im.shape[0], ys.max() + pad + 1))
out = Image.fromarray(np.clip(im, 0, 255).astype(np.uint8), 'RGBA').crop(box)
width = SHOWN_WIDTH * 2
height = round(out.height * width / out.width)
out = out.resize((width, height), Image.LANCZOS)
out.save('ui/img/assistant.png', optimize=True)
print('cropped to', box, '->', out.size, '(shown at', SHOWN_WIDTH, 'x', round(height / 2), ')')
