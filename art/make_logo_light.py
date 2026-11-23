# Builds ui/img/logo-light.png (the light look's logo) from ui/img/logo.png.
#   python3 art/make_logo_light.py      (needs Pillow, numpy, scipy)
# The logo has see-through gaps inside it (between the hat brim, the wreath and the microphone). On the dark
# look the navy background shows through and the badge looks solid; on white the gaps look like missing
# pieces. This puts a navy backing inside the logo's outline, and drops soft dark specks outside it.
from PIL import Image, ImageFilter
import numpy as np
from scipy import ndimage

src = np.array(Image.open('ui/img/logo.png')).astype(float)
h, w, _ = src.shape
a = src[:, :, 3]

shape = a > 60
# The gaps beside the microphone open out at the bottom, between the wreaths' tips and the stand.
# A short lid across the bottom (from one wreath tip to the other) closes them, so they can be filled.
yy, xx = np.mgrid[0:h, 0:w]
lid = (np.abs(yy - 437) <= 3) & (xx > 190) & (xx < 420)
closed = ndimage.binary_closing(shape | lid, iterations=6)
body = ndimage.binary_fill_holes(closed)
body &= ~(lid & ~shape) | (yy < 434)          # the lid itself isn't part of the badge
body = ndimage.binary_erosion(body, iterations=3)
back = np.array(Image.fromarray((body * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))).astype(float)

# soft dark specks outside the outline (glow left-overs) go
mx = src[:, :, :3].max(axis=2)
outside = ~ndimage.binary_dilation(body, iterations=2)
src[:, :, 3] = np.where(outside & (mx < 90), 0, a)

navy = np.zeros((h, w, 4))
navy[:, :, 0] = 10
navy[:, :, 1] = 20
navy[:, :, 2] = 44
navy[:, :, 3] = back
out = Image.fromarray(navy.astype(np.uint8), 'RGBA')
out.alpha_composite(Image.fromarray(np.clip(src, 0, 255).astype(np.uint8), 'RGBA'))
out.save('ui/img/logo-light.png', optimize=True)
