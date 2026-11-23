# Builds ui/img/header-light.png (the light look's logo, sound waves and title in one) from the owner's
# art/header-light.webp: trimmed to what's visible, faint specks around it removed, sized for sharp
# screens (2x the size it's shown at).
#   python3 art/make_header_light.py      (needs Pillow, numpy, scipy)
from PIL import Image
import numpy as np
from scipy import ndimage

SHOWN_HEIGHT = 132  # px on the 1672 x 941 stage (see .header-art in ui/css/app.css)

im = np.array(Image.open('art/header-light.webp').convert('RGBA')).astype(float)
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
height = SHOWN_HEIGHT * 2
width = round(out.width * height / out.height)
out = out.resize((width, height), Image.LANCZOS)
out.save('ui/img/header-light.png', optimize=True)
print('cropped to', box, '->', out.size, '(shown at', round(width / 2), 'x', SHOWN_HEIGHT, ')')
