# Builds ui/img/assistant.png (dark look) and assistant-light.png from the owner's art/assistant.png.
#   python3 art/make_assistant.py      (needs Pillow, numpy, scipy)
# The art was drawn for a dark background: its smoke, dark outlines and shadows vanish on navy but look like
# dirt on white, so the light version keeps only the bright parts and turns the white word "Assistant" navy.
from PIL import Image, ImageFilter
import numpy as np
from scipy import ndimage

src = np.array(Image.open('art/assistant.png')).astype(float)
h, w, _ = src.shape
yy, xx = np.mgrid[0:h, 0:w]


def channels(im):
    r, g, b, a = [im[:, :, i] for i in range(4)]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    return r, g, b, a, mx, (mx - mn) / (mx + 1e-6)


# 1. navy backing behind the hat and microphone (their dark parts were see-through)
r, g, b, a, mx, sat = channels(src)
emblem = (xx < 330) & (a > 40)
body = ndimage.binary_closing(emblem, iterations=5)
body = ndimage.binary_fill_holes(body)
body = ndimage.binary_erosion(body, iterations=2)
back = np.array(Image.fromarray((body * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5))).astype(float)


def compose(im):
    navy = np.zeros((h, w, 4))
    navy[:, :, 0] = 10
    navy[:, :, 1] = 20
    navy[:, :, 2] = 44
    navy[:, :, 3] = back
    base = Image.fromarray(navy.astype(np.uint8), 'RGBA')
    base.alpha_composite(Image.fromarray(np.clip(im, 0, 255).astype(np.uint8), 'RGBA'))
    return base


# 2. dark look: thin out the grey smoke behind the words only (unchanged since 0.1.1)
dark = src.copy()
r, g, b, a, mx, sat = channels(dark)
smoke = (xx > 330) & (sat < 0.25) & (mx < 170)
dark[:, :, 3] = np.where(smoke, a * np.clip((mx - 60) / 110, 0, 1) ** 2 * 0.6, a)
compose(dark).save('ui/img/assistant.png', optimize=True)

# 3. light look (0.2.1): clean edges on white
light = src.copy()
r, g, b, a, mx, sat = channels(light)
words = xx > 330
# the badge's own shape, without the start of the swoosh under the microphone (its dark tail is shadow)
inside = ndimage.binary_dilation(body, iterations=1) & ~((xx > 250) & (yy > 250))
light_back = np.array(Image.fromarray((inside * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5))).astype(float)

# the white word "Assistant" becomes navy; its grey edges become see-through navy, its dark outline goes
text = words & (yy > 150) & (yy < 270) & (sat < 0.3)
cover = np.clip((mx - 110) / 90, 0, 1)
for i, c in enumerate((16, 36, 74)):
    light[:, :, i] = np.where(text, c, light[:, :, i])
alpha = np.where(text, a * cover, a)

# everywhere else around the words: keep the bright gold, fade out the dark outlines, shadows and smoke
fade = np.clip((mx - 70) / 70, 0, 1)
alpha = np.where(words & ~text, a * fade, alpha)

# around the hat and microphone: no grey haze outside the badge's own shape
haze = ~words & ~inside & (sat < 0.3)
alpha = np.where(haze, 0, alpha)
alpha = np.where(~words & ~inside, alpha * fade, alpha)

# drop specks left over (tiny islands of colour)
solid = alpha > 24
labels, n = ndimage.label(solid)
sizes = ndimage.sum(solid, labels, range(1, n + 1))
tiny = np.isin(labels, np.nonzero(sizes < 12)[0] + 1)
alpha = np.where(tiny, 0, alpha)
light[:, :, 3] = alpha
back = light_back
compose(light).save('ui/img/assistant-light.png', optimize=True)
