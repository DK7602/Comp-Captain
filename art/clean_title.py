# Removes a stray dark dash just left of the "C" in ui/img/title.png (piece of the dark-background art that
# shows as a speck on the light look). Only that one piece is touched; letters and stars stay as drawn.
#   python3 art/clean_title.py      (needs Pillow, numpy, scipy; safe to run again)
from PIL import Image
import numpy as np
from scipy import ndimage

im = np.array(Image.open('ui/img/title.png'))
a = im[:, :, 3]
labels, n = ndimage.label(a > 30)
for i in range(1, n + 1):
    ys, xs = np.nonzero(labels == i)
    if xs.max() < 60 and 150 < ys.min() and ys.max() < 210 and len(xs) < 400:
        grow = ndimage.binary_dilation(labels == i, iterations=3)
        im[:, :, 3] = np.where(grow, 0, im[:, :, 3])
        print('removed the speck at x', xs.min(), '-', xs.max(), 'y', ys.min(), '-', ys.max())
Image.fromarray(im, 'RGBA').save('ui/img/title.png', optimize=True)
