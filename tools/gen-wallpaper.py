#!/usr/bin/env python3
"""Generate the MiaouDesk wallpapers (night + day): a cat on a rooftop under the moon/sun.
Usage: gen-wallpaper.py OUTDIR   (requires Pillow)"""
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter

W, H, SS = 1920, 1080, 2

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def gradient(stops):
    img = Image.new('RGB', (1, H))
    px = img.load()
    for y in range(H):
        t = y / (H - 1)
        for i in range(len(stops) - 1):
            t0, c0 = stops[i]; t1, c1 = stops[i + 1]
            if t0 <= t <= t1:
                px[0, y] = lerp(c0, c1, (t - t0) / (t1 - t0)); break
    return img.resize((W, H))

def cat_silhouette(draw, ox, oy, s, col):
    """Sitting cat seen from behind, looking at the sky. Coordinates are in 'cat units' * s."""
    def P(pts): return [(ox + x * s, oy + y * s) for x, y in pts]
    # body (pear shape)
    draw.ellipse([ox - 70*s, oy - 140*s, ox + 70*s, oy + 0*s], fill=col)
    draw.ellipse([ox - 52*s, oy - 230*s, ox + 52*s, oy - 100*s], fill=col)
    # head
    draw.ellipse([ox - 48*s, oy - 290*s, ox + 48*s, oy - 200*s], fill=col)
    # ears
    draw.polygon(P([(-46, -248), (-52, -318), (-14, -278)]), fill=col)
    draw.polygon(P([(46, -248), (52, -318), (14, -278)]), fill=col)
    # tail: starts INSIDE the body (so it is attached), runs along the roof and curls up
    ctrl = [(30, -34), (52, -14), (88, -9), (124, -16), (146, -38), (148, -66), (136, -88)]
    pts = ctrl
    for _ in range(4):  # Chaikin smoothing
        nxt = [pts[0]]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            nxt += [(0.75 * x0 + 0.25 * x1, 0.75 * y0 + 0.25 * y1), (0.25 * x0 + 0.75 * x1, 0.25 * y0 + 0.75 * y1)]
        nxt.append(pts[-1]); pts = nxt
    pts = [(ox + x * s, oy + y * s) for x, y in pts]
    r = 11 * s
    for (x, y) in pts:
        draw.ellipse([x - r, y - r, x + r, y + r], fill=col)

def skyline(draw, base, col, seed, windows=None):
    rnd = random.Random(seed)
    x = 0
    while x < W * SS:
        bw = rnd.randint(60, 150) * SS
        bh = rnd.randint(60, 260) * SS
        draw.rectangle([x, base - bh, x + bw, H * SS], fill=col)
        if windows:
            for wy in range(base - bh + 18 * SS, base - 14 * SS, 26 * SS):
                for wx in range(x + 12 * SS, x + bw - 14 * SS, 22 * SS):
                    if rnd.random() < 0.28:
                        draw.rectangle([wx, wy, wx + 8 * SS, wy + 12 * SS], fill=windows)
        x += bw + rnd.randint(0, 14) * SS

def make(night):
    if night:
        img = gradient([(0, (12, 10, 40)), (0.45, (52, 30, 100)), (0.75, (150, 62, 140)), (1, (255, 140, 150))])
    else:
        img = gradient([(0, (135, 190, 255)), (0.5, (255, 214, 232)), (0.8, (255, 200, 170)), (1, (255, 170, 160))])
    img = img.convert('RGBA')
    rnd = random.Random(7)
    layer = Image.new('RGBA', (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    if night:
        for _ in range(260):
            x, y = rnd.randint(0, W * SS), rnd.randint(0, int(H * 0.6) * SS)
            r = rnd.choice([1, 1, 1, 2, 3]) * SS // 2
            a = rnd.randint(110, 255)
            d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 245, 225, a))
    # moon / sun with soft glow
    cx, cy, r = int(W * 0.72) * SS, int(H * 0.30) * SS, 120 * SS
    glow = Image.new('RGBA', layer.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gcol = (255, 236, 190, 90) if night else (255, 240, 170, 130)
    gd.ellipse([cx - r * 2, cy - r * 2, cx + r * 2, cy + r * 2], fill=gcol)
    glow = glow.filter(ImageFilter.GaussianBlur(60 * SS))
    layer = Image.alpha_composite(layer, glow)
    d = ImageDraw.Draw(layer)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 244, 214, 255) if night else (255, 226, 120, 255))
    if night:
        for (dx, dy, rr) in ((-40, -20, 26), (30, 40, 18), (45, -45, 12)):
            d.ellipse([cx + (dx - rr) * SS, cy + (dy - rr) * SS, cx + (dx + rr) * SS, cy + (dy + rr) * SS], fill=(236, 222, 190, 255))
    else:
        for i in range(7):
            x = rnd.randint(0, W) * SS; y = rnd.randint(60, 420) * SS
            for k in range(5):
                d.ellipse([x + k * 40 * SS, y - (k % 2) * 14 * SS, x + (k * 40 + 90) * SS, y + (60 - (k % 2) * 14) * SS], fill=(255, 255, 255, 210))
    far = (40, 24, 82, 255) if night else (214, 150, 190, 255)
    near = (22, 12, 48, 255) if night else (150, 96, 150, 255)
    skyline(d, int(H * 0.80) * SS, far, 3)
    skyline(d, int(H * 0.88) * SS, near, 11, (255, 214, 120, 255) if night else None)
    # rooftop
    d.rectangle([0, int(H * 0.93) * SS, W * SS, H * SS], fill=(12, 6, 26, 255) if night else (96, 58, 104, 255))
    d.rectangle([int(W * 0.50) * SS, int(H * 0.915) * SS, int(W * 0.94) * SS, int(H * 0.93) * SS + SS], fill=(12, 6, 26, 255) if night else (96, 58, 104, 255))
    cat_silhouette(d, int(W * 0.66) * SS, int(H * 0.918) * SS, 1.25 * SS, (8, 4, 18, 255) if night else (70, 40, 84, 255))
    layer = layer.resize((W, H), Image.LANCZOS)
    img = Image.alpha_composite(img, layer)
    # film grain-free subtle vignette
    vig = Image.new('L', (W, H), 0)
    vd = ImageDraw.Draw(vig)
    vd.ellipse([-W * 0.25, -H * 0.35, W * 1.25, H * 1.35], fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(120))
    dark = Image.new('RGBA', (W, H), (0, 0, 20, 80))
    img = Image.composite(img, Image.alpha_composite(img, dark), vig)
    return img.convert('RGB')

out = sys.argv[1]
make(True).save(out + '/night.jpg', quality=88, optimize=True)
make(False).save(out + '/day.jpg', quality=88, optimize=True)
