# Builds ui/img/assistant.png (dark look) and assistant-light.png from the owner's art/assistant.png.
from PIL import Image, ImageFilter
import numpy as np
from scipy import ndimage
src = np.array(Image.open('art/assistant.png')).astype(float)
h, w, _ = src.shape
yy, xx = np.mgrid[0:h, 0:w]

def channels(im):
    r, g, b, a = [im[:, :, i] for i in range(4)]
    mx = np.maximum(np.maximum(r, g), b); mn = np.minimum(np.minimum(r, g), b)
    return r, g, b, a, mx, (mx - mn) / (mx + 1e-6)

# 1. navy backing behind the hat and microphone (their dark parts were see-through)
r, g, b, a, mx, sat = channels(src)
emblem = (xx < 330) & (a > 40)
body = ndimage.binary_closing(emblem, iterations=5)
body = ndimage.binary_fill_holes(body)
body = ndimage.binary_erosion(body, iterations=2)
back = np.array(Image.fromarray((body * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5))).astype(float)

def compose(im):
    navy = np.zeros((h, w, 4)); navy[:, :, 0] = 10; navy[:, :, 1] = 20; navy[:, :, 2] = 44; navy[:, :, 3] = back
    base = Image.fromarray(navy.astype(np.uint8), 'RGBA')
    base.alpha_composite(Image.fromarray(np.clip(im, 0, 255).astype(np.uint8), 'RGBA'))
    return base

# 2. dark look: thin out the grey smoke behind the words only
dark = src.copy()
r, g, b, a, mx, sat = channels(dark)
smoke = (xx > 330) & (sat < 0.25) & (mx < 170)
dark[:, :, 3] = np.where(smoke, a * np.clip((mx - 60) / 110, 0, 1) ** 2 * 0.6, a)
compose(dark).save('ui/img/assistant.png', optimize=True)

# 3. light look: same, plus the white word "Assistant" turned navy so it reads on white
light = dark.copy()
r, g, b, a, mx, sat = channels(light)
txt = (xx > 330) & (yy > 150) & (yy < 270) & (sat < 0.3) & (mx > 120)
k = np.clip((mx - 120) / 100, 0, 1)
for i, c in enumerate((16, 36, 74)):
    light[:, :, i] = np.where(txt, light[:, :, i] * (1 - k) + c * k, light[:, :, i])
light[:, :, 3] = np.where(txt, np.maximum(a, 200 * k), a)
compose(light).save('ui/img/assistant-light.png', optimize=True)
