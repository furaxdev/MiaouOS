#!/usr/bin/env python3
"""Generate the MiaouDesk wallpapers (night + day) at 2880x1800: a cat on a rooftop above a
detailed city, under a procedural cratered moon / a golden-hour sun, rendered in linear HDR with
bloom, halos, god rays, rim light, window lights, lens ghosts, vignette, film grain and a soft
tone-map.  Everything is procedural (no external art).
Usage: gen-wallpaper.py OUTDIR [width]     (requires numpy, scipy, Pillow)"""
import math, os, sys
import numpy as np
from scipy.ndimage import gaussian_filter, affine_transform
from PIL import Image, ImageDraw

W = int(sys.argv[2]) if len(sys.argv) > 2 else 2880
H = W * 5 // 8
SC = W / 2880.0
SCM = max(SC, 0.6)
F32 = np.float32


def px(v): return max(1, int(round(v * SC)))
def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
def col(r, g, b): return np.array([r, g, b], F32)


def fbm(h, w, beta, rng, aniso=(1.0, 1.0)):
    """Fractal noise with a 1/f^beta amplitude spectrum, unit std."""
    n = rng.standard_normal((h, w)).astype(F32)
    f = np.fft.rfft2(n)
    fy = np.fft.fftfreq(h)[:, None] * aniso[0]
    fx = np.fft.rfftfreq(w)[None, :] * aniso[1]
    r = np.sqrt(fx * fx + fy * fy); r[0, 0] = 1
    f *= (r ** -beta).astype(F32); f[0, 0] = 0
    o = np.fft.irfft2(f, s=(h, w)).astype(F32)
    return o / (o.std() + 1e-9)


def resize_f(a, w, h):
    return np.asarray(Image.fromarray(np.ascontiguousarray(a, dtype=F32)).resize((w, h), Image.BILINEAR))


def gb(a, s):
    """Gaussian blur of (H,W) or (H,W,3); large sigmas run on a downsampled copy."""
    if s <= 8:
        return gaussian_filter(a, (s, s) + ((0,) if a.ndim == 3 else ()), mode='nearest')
    f = max(2, int(s / 4)); h, w = a.shape[:2]
    pad = [(0, (-h) % f), (0, (-w) % f)] + [(0, 0)] * (a.ndim - 2)
    p = np.pad(a, pad, mode='edge'); hh, ww = p.shape[0] // f, p.shape[1] // f
    small = p.reshape(hh, f, ww, f, *a.shape[2:]).mean((1, 3))
    small = gaussian_filter(small, (s / f, s / f) + ((0,) if a.ndim == 3 else ()), mode='nearest')
    if a.ndim == 2: return resize_f(small, ww * f, hh * f)[:h, :w]
    return np.stack([resize_f(small[..., i], ww * f, hh * f)[:h, :w] for i in range(a.shape[2])], -1)


def vgrad(stops, y1=1.0):
    t = np.clip(np.linspace(0, 1, H) / y1, 0, 1)
    ts = [s[0] for s in stops]
    return np.stack([np.interp(t, ts, [s[1][i] for s in stops]) for i in range(3)], -1)[:, None, :].astype(F32)


def splat_points(layer, xs, ys, vals):
    x0 = np.floor(xs).astype(int); y0 = np.floor(ys).astype(int); fx = (xs - x0)[:, None]; fy = (ys - y0)[:, None]
    for dx, dy, w in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        xi = np.clip(x0 + dx, 0, W - 1); yi = np.clip(y0 + dy, 0, H - 1)
        for c in range(3): np.add.at(layer[..., c], (yi, xi), vals[:, c] * w[:, 0])


def disc(cx, cy, r, soft=1.0):
    y, x = np.mgrid[0:H, 0:W].astype(F32)
    return np.clip((r - np.hypot(x - cx, y - cy)) / soft + 0.5, 0, 1)


# ---------------------------------------------------------------- celestial bodies
def make_moon(rng, cx, cy, R, light):
    """Shaded, cratered, gibbous moon patch: (rgb HDR, alpha, x0, y0)."""
    m = int(R * 1.3); x0, y0 = int(cx - m), int(cy - m); n = 2 * m + 1
    y, x = np.mgrid[0:n, 0:n].astype(F32); u = (x - m) / R; v = (y - m) / R
    r2 = u * u + v * v; rr = np.sqrt(r2)
    alpha = np.clip((1 - rr) * R / 1.2 + 0.5, 0, 1)
    nz = np.sqrt(np.clip(1 - r2, 0, 1))
    L = np.array(light, F32); L /= np.linalg.norm(L)
    ndl = u * L[0] + v * L[1] + nz * L[2]
    lit = smooth(-0.04, 0.30, ndl) * np.clip(ndl, 0, 1) ** 0.35
    big = fbm(n, n, 2.2, rng); fine = fbm(n, n, 1.1, rng)
    alb = 0.78 + 0.05 * fine
    alb *= 1 - 0.38 * smooth(0.25, 0.95, big) * (rr < 0.93)          # maria
    for _ in range(900):                                                # craters, power-law sizes
        a = rng.random() * 2 * math.pi; d = math.sqrt(rng.random()) * 0.97
        ccx, ccy = d * math.cos(a), d * math.sin(a)
        rc = 0.006 + 0.075 * rng.random() ** 3.2
        xa, xb = int(m + (ccx - rc * 1.6) * R), int(m + (ccx + rc * 1.6) * R) + 1
        ya, yb = int(m + (ccy - rc * 1.6) * R), int(m + (ccy + rc * 1.6) * R) + 1
        xa, ya, xb, yb = max(xa, 0), max(ya, 0), min(xb, n), min(yb, n)
        if xb - xa < 2 or yb - ya < 2: continue
        sx = (u[ya:yb, xa:xb] - ccx) / rc; sy = (v[ya:yb, xa:xb] - ccy) / rc
        dd = np.hypot(sx, sy); toward = -(sx * L[0] + sy * L[1]) / np.maximum(dd, 1e-3)
        rim = np.exp(-((dd - 1.0) / 0.16) ** 2)
        floor = (dd < 1) * (1 - 0.35 * smooth(0.2, 1.0, dd))
        wall = np.exp(-((dd - 0.82) / 0.2) ** 2) * toward
        alb[ya:yb, xa:xb] *= 1 + 0.30 * rim - 0.20 * floor * (1 + 0.5 * toward) + 0.22 * wall * (dd < 1)
        if rc > 0.05:                                                   # ejecta rays
            alb[ya:yb, xa:xb] *= 1 + 0.12 * np.exp(-((dd - 1.8) / 1.0) ** 2) * (np.cos(np.arctan2(sy, sx) * 7) > 0.6)
    alb = np.clip(alb, 0.12, 1.2)
    limb = 0.65 + 0.35 * nz ** 0.5
    inten = (lit * alb * limb + 0.012 * alb) * 1.5
    return (inten[..., None] * col(1.0, 0.94, 0.82)).astype(F32), alpha.astype(F32), x0, y0


def stars(rng, vis):
    n = 30000
    xs = rng.random(n) * (W - 1); ys = rng.random(n) * (H - 1)
    b = 0.010 + 1.0 * rng.random(n) ** 10
    k = rng.random(n)[:, None]
    tint = 0.55 * ((1 - k) * col(1.0, 0.82, 0.62) + k * col(0.68, 0.80, 1.0)) + 0.45
    layer = np.zeros((H, W, 3), F32); splat_points(layer, xs, ys, tint * b[:, None])
    layer = gb(layer, 0.7 * SCM) * 2.4
    spikes = np.zeros((H, W, 3), F32)
    for i in np.argsort(-b)[:34]:                                       # diffraction spikes
        x, y = int(xs[i]), int(ys[i]); L = px(10 + 60 * b[i])
        if not (L < x < W - L and L < y < H - L): continue
        prof = np.exp(-np.abs(np.arange(-L, L + 1)) / (L * 0.35)) * 0.12 * b[i]
        spikes[y, x - L:x + L + 1] += prof[:, None] * tint[i]
        spikes[y - L:y + L + 1, x] += prof[:, None] * tint[i]
    return (layer + gb(spikes, 0.7)) * vis[..., None]


# ---------------------------------------------------------------- city
def city_layer(rng, top_lo, top_hi, wmin, wmax, depth, pitch, p_lit):
    """One skyline layer: (mask HxW, per-column top, lit windows RGB HDR)."""
    top = np.full(W, H, int); wins = np.zeros((H, W, 3), F32)
    x = -int(rng.integers(0, px(40))); blds = []
    while x < W:
        bw = int(rng.integers(px(wmin), px(wmax)))
        ht = int(H * (top_lo + (top_hi - top_lo) * rng.random() ** 1.4))
        kind = rng.random(); x0, x1 = max(x, 0), min(x + bw, W)
        if x1 > x0:
            top[x0:x1] = np.minimum(top[x0:x1], ht); blds.append([x0, x1, ht])
            if kind < 0.30 and x1 - x0 > px(24):                        # stepped tower
                iw = (x1 - x0) // 4; ht2 = ht - int(H * (0.01 + 0.035 * rng.random()))
                top[x0 + iw:x1 - iw] = np.minimum(top[x0 + iw:x1 - iw], ht2); blds.append([x0 + iw, x1 - iw, ht2])
            elif kind < 0.45 and x1 - x0 > px(14):                      # spire
                sx = (x0 + x1) // 2; sh = int(H * (0.012 + 0.05 * rng.random())); sw = max(1, px(2 if depth < 0.5 else 1.2))
                top[sx:sx + sw] = np.minimum(top[sx:sx + sw], ht - sh)
            elif kind < 0.60:                                           # roof box / tank
                bx = x0 + int((x1 - x0) * 0.2); bw2 = int((x1 - x0) * 0.35); bh = px(10 + 10 * rng.random())
                top[bx:bx + bw2] = np.minimum(top[bx:bx + bw2], ht - bh)
        x += bw + int(rng.integers(0, px(8)))
    mask = (np.arange(H)[:, None] >= top[None, :]).astype(F32)
    pw, ph = px(pitch), px(pitch * 1.55)
    tile = np.zeros((ph, pw), F32); ww_, wh_ = max(1, int(pw * 0.52)), max(1, int(ph * 0.52))
    tile[(ph - wh_) // 2:(ph - wh_) // 2 + wh_, (pw - ww_) // 2:(pw - ww_) // 2 + ww_] = 1
    warm = col(1.0, 0.70, 0.36); soft = col(0.98, 0.86, 0.64); pale = col(0.70, 0.84, 1.0)
    for x0, x1, ht in blds:
        if x1 - x0 < pw * 2: continue
        nx = (x1 - x0 - pw) // pw; y_top = ht + ph
        ny = min((H - int(H * 0.30) - y_top) // ph, (int(H * 0.86) - y_top) // ph)
        if nx < 1 or ny < 1: continue
        g = rng.random((ny, nx)) < p_lit * (0.35 + 1.1 * rng.random())
        g &= rng.random((ny, 1)) < 0.85
        ints = (0.45 + 1.2 * rng.random((ny, nx))) * g
        pal = rng.random((ny, nx, 1))
        cc = np.where(pal < 0.55, warm, np.where(pal < 0.88, soft, pale)).astype(F32)
        patch = np.kron(ints, np.ones((ph, pw), F32))[..., None] * np.tile(tile, (ny, nx))[..., None] * np.kron(cc, np.ones((ph, pw, 1), F32))
        xa = x0 + (x1 - x0 - nx * pw) // 2; hh, ww2 = patch.shape[:2]
        ya2, xa2 = min(y_top + hh, H), min(xa + ww2, W)
        wins[y_top:ya2, xa:xa2] += patch[:ya2 - y_top, :xa2 - xa]
    return mask, top, wins


def edge_light(mask, top, light_x, rim_col, strength):
    """Rim light on roofs and on the walls facing the light."""
    up = np.roll(mask, 1, 0); up[0] = 0
    left = np.roll(mask, 1, 1); right = np.roll(mask, -1, 1)
    facing = (np.arange(W) < light_x)[None, :]
    side = np.where(facing, mask * (1 - right), mask * (1 - left))
    below = np.clip(np.arange(H)[:, None] - top[None, :], 0, None)
    side = side * np.exp(-below / px(36))                                 # walls only catch light near the roof
    e = gb(np.clip(mask * (1 - up) + 0.7 * side, 0, 1).astype(F32), 0.8 * SCM)
    return e[..., None] * rim_col * strength


# ---------------------------------------------------------------- cat
def cat_mask(rng, cx, by, hgt):
    """Sitting cat seen from behind: anatomy built at 4x, then individual hairs drawn along the contour."""
    s = hgt / 318.0; pad = int(hgt * 0.75); S = 4
    x0, y0 = int(cx - pad), int(by - hgt * 1.25); x1, y1 = int(cx + pad), int(by + hgt * 0.15)
    im = Image.new('L', ((x1 - x0) * S, (y1 - y0) * S), 0); d = ImageDraw.Draw(im)
    ox, oy = (cx - x0) * S, (by - y0) * S; k = s * S
    def P(pts): return [(ox + x * k, oy + y * k) for x, y in pts]
    def E(x0_, y0_, x1_, y1_): d.ellipse([ox + x0_ * k, oy + y0_ * k, ox + x1_ * k, oy + y1_ * k], fill=255)
    # one continuous body outline (like a real cat: the head melts into the shoulders, no neck pinch)
    prof = [(2, 66), (-14, 76), (-40, 82), (-75, 78), (-110, 68), (-150, 58), (-185, 52), (-215, 50), (-240, 58),
            (-262, 58), (-282, 48), (-296, 30), (-300, 12), (-301, 0)]
    right = [(w_, y_) for y_, w_ in prof]; left = [(-w_, y_) for y_, w_ in reversed(prof)]
    out = right + left
    for _ in range(3):                                   # Chaikin smoothing of the contour
        nxt = []
        for i in range(len(out)):
            (xa, ya), (xb, yb) = out[i], out[(i + 1) % len(out)]
            nxt += [(0.75 * xa + 0.25 * xb, 0.75 * ya + 0.25 * yb), (0.25 * xa + 0.75 * xb, 0.25 * ya + 0.75 * yb)]
        out = nxt
    d.polygon(P(out), fill=255)
    E(-84, -50, -34, 2); E(34, -50, 84, 2)               # thighs bulging at the base
    E(-64, -24, -22, 5); E(22, -24, 64, 5)               # hind paws
    d.polygon(P([(-54, -262), (-60, -322), (-14, -282)]), fill=255)   # ears: wide set, slightly rounded by the fur pass
    d.polygon(P([(54, -262), (60, -322), (14, -282)]), fill=255)
    pts = [(34, -34), (56, -14), (92, -9), (128, -16), (150, -40), (152, -70), (140, -94), (122, -104)]  # tail, starts inside the body
    for _ in range(4):
        nxt = [pts[0]]
        for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
            nxt += [(0.75 * xa + 0.25 * xb, 0.75 * ya + 0.25 * yb), (0.25 * xa + 0.75 * xb, 0.25 * ya + 0.75 * yb)]
        nxt.append(pts[-1]); pts = nxt
    for i, (xx, yy) in enumerate(pts):
        r = (12.5 - 5.5 * (i / len(pts)) ** 1.5) * k
        d.ellipse([ox + xx * k - r, oy + yy * k - r, ox + xx * k + r, oy + yy * k + r], fill=255)
    # hairs: short strokes leaving the contour, longer on cheeks / ears / tail / ruff
    a = np.asarray(im, F32) / 255.0
    g = gaussian_filter(a, 2.0 * S / 3)
    gy, gx = np.gradient(g); edge = (a > 0.5) & (gaussian_filter(a, 1.0) < 0.97)
    ey, ex = np.nonzero(edge); sel = rng.random(len(ey)) < 0.40
    for yy, xx in zip(ey[sel], ex[sel]):
        nx_, ny_ = -gx[yy, xx], -gy[yy, xx]; nn = math.hypot(nx_, ny_)
        if nn < 1e-6: continue
        nx_, ny_ = nx_ / nn, ny_ / nn
        ang = math.atan2(ny_, nx_) + rng.normal(0, 0.45); L = (3 + 9 * rng.random() ** 1.6) * S * SCM
        xa, ya = xx - nx_ * 2, yy - ny_ * 2
        # slight curve: two segments
        xm, ym = xa + math.cos(ang) * L * 0.6, ya + math.sin(ang) * L * 0.6
        xe, ye = xm + math.cos(ang + 0.35) * L * 0.4, ym + math.sin(ang + 0.35) * L * 0.4
        d.line([(xa, ya), (xm, ym), (xe, ye)], fill=int(150 + 105 * rng.random()), width=max(1, int(S * 0.55 * SCM)))
    m = np.asarray(im.resize((x1 - x0, y1 - y0), Image.LANCZOS), F32) / 255.0
    return np.clip(m, 0, 1).astype(F32), x0, y0


def foreground(img, lights, rng, night, light_pos, rim_col, cat_x=0.31):
    """Rooftop ledge, vents, antenna and the cat, with rim lighting."""
    y0 = int(H * 0.862); ph = px(26); shape_top = y0 - ph
    base = col(0.0025, 0.0032, 0.0070) if night else col(0.012, 0.007, 0.008)
    tex = 1 + 0.11 * fbm(H, W, 0.9, rng) + 0.09 * fbm(H, W, 1.8, rng)      # gravel + stains
    mask = np.zeros((H, W), F32); mask[shape_top:] = 1
    for fx, fw, fh in ((0.60, 0.050, 0.030), (0.69, 0.034, 0.055), (0.86, 0.060, 0.038)):
        xa, xb = int(W * fx), int(W * (fx + fw)); ya = int(shape_top - H * fh)
        mask[ya:shape_top, xa:xb] = 1; mask[ya - px(4):ya, xa - px(4):xb + px(4)] = 1
    ax = int(W * 0.94); mask[int(H * 0.60):shape_top, ax:ax + px(3)] = 1
    for k in range(5):
        yk = int(H * (0.64 + 0.045 * k)); mask[yk:yk + px(3), ax - px(30):ax + px(33)] = 1
    m2 = gb(mask, 0.7 * SCM)
    up = np.roll(mask, 1, 0); up[0] = 0
    edge = gb(mask * (1 - up), 0.9 * SCM)
    body = base * tex[..., None]
    slab = col(0.012, 0.018, 0.034) if night else col(0.09, 0.04, 0.02)
    body[shape_top:y0] = base * 1.8 + slab
    rim = edge[..., None] * rim_col * (0.55 if night else 0.9)
    img[:] = img * (1 - m2[..., None]) + (body + rim) * m2[..., None]
    lights *= (1 - m2)[..., None]                                          # the ledge hides lights behind it
    lights += rim * 0.7
    # aircraft beacon on the mast (tiny, dim)
    bx, by = ax + px(1), int(H * 0.60)
    lights[by - px(3):by + px(3), bx - px(3):bx + px(3)] += col(1.0, 0.12, 0.08) * (1.5 if night else 0.5)
    # cat
    cmask, cx0, cy0 = cat_mask(rng, W * cat_x, shape_top + px(3), H * 0.205)
    hh, ww = cmask.shape
    catc = col(0.0018, 0.0022, 0.0050) if night else col(0.006, 0.004, 0.005)
    sub = img[cy0:cy0 + hh, cx0:cx0 + ww]
    fibre = fbm(hh, ww, 0.7, rng, (0.25, 1.0)); fine = fbm(hh, ww, 0.5, rng)
    fur_tex = np.clip(1 + 0.50 * fibre + 0.22 * fine, 0.3, 2.2)         # fur fibres (texture only, no reflection)
    sub[:] = sub * (1 - cmask[..., None]) + (catc * fur_tex[..., None]) * cmask[..., None]
    lights[cy0:cy0 + hh, cx0:cx0 + ww] *= (1 - cmask)[..., None]
    

# ---------------------------------------------------------------- post-processing
def god_rays(src, cx, cy, length, steps, decay):
    out = np.zeros_like(src)
    for i in range(steps):
        s = 1 - length * (i + 1) / steps; off = (cy - cy * s, cx - cx * s)
        for c in range(3):
            out[..., c] += affine_transform(src[..., c], (s, s), offset=off, order=1, mode='constant') * (decay ** i)
    return out / steps


def post(hdr, lights, rng, expo, vig, grain, rays, ghosts, aber=0.0013):
    h, w = hdr.shape[:2]
    bl = (0.55 * gb(lights, px(3)) + 0.40 * gb(lights, px(11)) + 0.35 * gb(lights, px(38)) +
          0.30 * gb(lights, px(120)) + 0.25 * gb(lights, px(330)))             # multi-radius bloom
    img = hdr + lights * 0.55 + bl
    q = lights[::4, ::4]                                                        # anamorphic streak
    st = gaussian_filter(q, (0.6, px(90) / 4, 0), mode='nearest')
    img += resize_f(st[..., 0], w, h)[..., None] * col(0.8, 0.9, 1.0) * 0.18
    cx, cy, length, gain, tint = rays                                           # god rays
    src = np.clip(lights[::4, ::4] - 1.0, 0, 20) * 0.08
    r = god_rays(src, cx / 4, cy / 4, length, 28, 0.985)
    img += np.stack([resize_f(r[..., c], w, h) for c in range(3)], -1) * gain * tint
    gx, gy, items = ghosts                                                      # lens ghosts
    g = np.zeros((h, w, 3), F32)
    for t, rad, c, a in items:
        x = gx + (w / 2 - gx) * t; y = gy + (h / 2 - gy) * t; rr = px(rad)
        y0, y1, x0, x1 = int(y - rr * 1.5), int(y + rr * 1.5), int(x - rr * 1.5), int(x + rr * 1.5)
        if y0 < 0 or x0 < 0 or y1 >= h or x1 >= w: continue
        yy, xx = np.mgrid[y0:y1, x0:x1].astype(F32); d = np.hypot(xx - x, yy - y) / rr
        ring = np.exp(-((d - 0.85) / 0.14) ** 2) * 0.7 + np.clip(1 - d, 0, 1) * 0.35
        g[y0:y1, x0:x1] += ring[..., None] * np.array(c, F32) * a
    img += gb(g, 1.5)
    yy = np.linspace(-1, 1, h, dtype=F32)[:, None]; xx = np.linspace(-1, 1, w, dtype=F32)[None, :] * (w / h)
    img *= (1 - vig * np.clip((xx * xx + yy * yy) / 2.6, 0, 1) ** 1.2)[..., None]   # vignette
    ldr = 1 - np.exp(-expo * img)                                               # exponential shoulder
    for ch, s in ((0, 1 + aber), (2, 1 - aber)):                                # chromatic aberration
        ldr[..., ch] = affine_transform(ldr[..., ch], (s, s), offset=(h / 2 * (1 - s), w / 2 * (1 - s)), order=1, mode='nearest')
    ldr = np.clip(ldr, 0, 1) ** (1 / 2.2)
    lum = ldr.mean(-1, keepdims=True)                                           # film grain
    gr = gaussian_filter(rng.standard_normal((h, w, 3)).astype(F32), (0.55, 0.55, 0), mode='nearest') * 1.7
    gr = 0.75 * gr.mean(-1, keepdims=True) + 0.25 * gr
    return np.clip(ldr + gr * grain * (1.15 - 0.7 * lum), 0, 1)


# ---------------------------------------------------------------- scenes
def night_scene(seed=7):
    rng = np.random.default_rng(seed); hor = 0.70
    Y = np.linspace(0, 1, H, dtype=F32)[:, None]
    sky = vgrad([(0.0, (0.0016, 0.0030, 0.0100)), (0.35, (0.0050, 0.0120, 0.0320)), (0.62, (0.0150, 0.0320, 0.0620)),
                 (0.78, (0.0420, 0.0700, 0.1000)), (1.0, (0.05, 0.07, 0.10))], 0.80) * np.ones((1, W, 1), F32)
    drift = resize_f(gb(fbm(H // 8, W // 8, 2.5, rng), 3), W, H)
    sky *= (1 + 0.12 * drift)[..., None]
    moon_x, moon_y, R = 0.745 * W, 0.235 * H, 0.074 * H
    hdr = sky.copy(); lights = np.zeros((H, W, 3), F32)
    yy, xx = np.mgrid[0:H, 0:W].astype(F32); a = W / H
    # Milky Way with dust lanes
    ang = math.radians(-32); dx, dy = math.cos(ang), math.sin(ang)
    d = (xx / W * a - 0.35) * dy - (yy / H - 0.30) * dx
    band = np.exp(-(d / 0.115) ** 2)
    dens = np.clip(band * np.clip(0.75 + 0.35 * fbm(H, W, 1.7, rng), 0, 2) * (1 - 0.8 * smooth(0.0, 1.2, fbm(H, W, 1.9, rng)) * band), 0, 2)
    vis = (1 - smooth(0.50, 0.74, Y[:, 0]))[:, None] * np.ones((1, W), F32)
    fine = gb((rng.random((H, W)) < dens * 0.06).astype(F32) * rng.random((H, W)).astype(F32) ** 2.5 * 0.35, 0.6)[..., None] * col(1.0, 0.95, 0.88)
    hdr += (dens[..., None] * col(0.052, 0.058, 0.080) * 0.9 + fine) * vis[..., None]
    st = stars(rng, vis)
    # thin clouds lit by the moon
    cl = fbm(H, W, 2.0, rng, (1.0, 3.2)); cl2 = fbm(H, W, 1.3, rng, (1.0, 2.0))
    cband = np.exp(-((Y - 0.34) / 0.20) ** 2) * 0.85 + np.exp(-((Y - 0.12) / 0.10) ** 2) * 0.5
    shape = np.clip(smooth(0.35, 1.7, cl * 0.8 + cl2 * 0.25 - 0.1) * cband, 0, 1) * (1 - smooth(0.60, 0.78, Y))
    dm = np.hypot(xx - moon_x, yy - moon_y) / H
    grad = np.clip((np.roll(cl, -px(6), 1) - cl) * 0.5 + 0.5, 0, 1)
    ccol = col(0.30, 0.40, 0.62) * ((np.exp(-dm / 0.28) * 1.7 + 0.04) * (0.4 + 0.9 * grad))[..., None]
    silver = (shape * (1 - shape) * 4 * np.exp(-dm / 0.12))[..., None] * col(1.2, 1.15, 1.0) * 0.55
    hdr = hdr * (1 - shape[..., None] * 0.55) + ccol * shape[..., None] + silver
    hdr += st * (1 - 0.92 * np.clip(shape * 1.4, 0, 1))[..., None]
    # moon + halos
    mrgb, malpha, mx0, my0 = make_moon(rng, moon_x, moon_y, R, (-0.58, -0.32, 0.74))
    sl = (slice(my0, my0 + mrgb.shape[0]), slice(mx0, mx0 + mrgb.shape[1]))
    hdr[sl] = hdr[sl] * (1 - malpha[..., None]) + mrgb * malpha[..., None]
    lights[sl] += mrgb * malpha[..., None] * 0.9
    d2 = np.hypot(xx - moon_x, yy - moon_y) / R
    hdr += (np.exp(-d2 / 3.0) * 0.07 + np.exp(-d2 / 14) * 0.035)[..., None] * col(0.55, 0.68, 1.0)
    hdr += (np.exp(-((d2 - 8.6) / 0.35) ** 2) * 0.012 * (1 - shape * 0.6) * (d2 > 5))[..., None] * col(0.9, 0.95, 1.0)   # 22° halo
    # city glow on the horizon
    hdr += np.exp(-((Y - hor) / 0.11) ** 2)[..., None] * col(0.20, 0.115, 0.060) * 0.30 * (1 + 0.4 * drift[..., None])
    hdr += np.exp(-((Y - hor + 0.02) / 0.04) ** 2)[..., None] * col(0.30, 0.16, 0.07) * 0.10
    fogc = col(0.040, 0.062, 0.100)
    for top_lo, top_hi, wmin, wmax, depth, pitch, p_lit in (
            (0.66, 0.735, 14, 46, 0.95, 5.0, 0.28), (0.64, 0.760, 18, 58, 0.62, 6.5, 0.34),
            (0.67, 0.800, 26, 84, 0.34, 8.5, 0.40), (0.74, 0.835, 40, 120, 0.10, 11.0, 0.40)):
        mask, top, wins = city_layer(rng, top_lo, top_hi, wmin, wmax, depth, pitch, p_lit)
        facade = 1 + 0.09 * fbm(H, W, 0.7, rng)
        body = (fogc * depth * 1.2 + col(0.0022, 0.0034, 0.0075) * (1 - depth)) * facade[..., None] \
            + col(0.10, 0.045, 0.02) * 0.040 * (1 - depth) * smooth(0.70, 1.0, Y)[..., None]
        m = gb(mask, 0.55 * SCM)[..., None]
        hdr = hdr * (1 - m) + body * m
        rimc = edge_light(mask, top, moon_x, col(0.45, 0.58, 0.85), 0.10 + 0.5 * (1 - depth))
        hdr += rimc * m; lights += rimc * 0.5
        wl = wins * mask[..., None]
        hdr += wl * (0.9 - 0.35 * depth); lights += wl * (1.1 - 0.4 * depth)
        f = (0.20 * np.exp(-((Y - hor) / 0.16) ** 2) + 0.06)[..., None] * np.clip(depth * 1.4, 0, 1) * 0.7
        hdr = hdr * (1 - f) + (fogc * 1.3 + col(0.05, 0.03, 0.02)) * f
    foreground(hdr, lights, rng, True, (moon_x, moon_y), col(0.55, 0.68, 1.0))
    ghosts = (moon_x, moon_y, [(0.45, 38, (0.4, 0.6, 1.0), 0.020), (0.72, 70, (0.7, 0.9, 1.0), 0.012),
                                (1.25, 52, (1.0, 0.8, 0.6), 0.014), (1.6, 120, (0.5, 0.7, 1.0), 0.008)])
    return post(hdr, lights, rng, 1.25, 0.42, 0.009, (moon_x, moon_y, 0.55, 1.0, col(0.45, 0.60, 1.0)), ghosts)


def day_scene(seed=11):
    rng = np.random.default_rng(seed); hor = 0.70
    Y = np.linspace(0, 1, H, dtype=F32)[:, None]
    sun_x, sun_y = 0.690 * W, 0.470 * H
    sky = vgrad([(0.0, (0.020, 0.060, 0.170)), (0.30, (0.060, 0.130, 0.290)), (0.55, (0.300, 0.250, 0.380)),
                 (0.70, (0.80, 0.42, 0.26)), (0.80, (1.05, 0.55, 0.25)), (1.0, (0.9, 0.45, 0.2))], 0.82) * np.ones((1, W, 1), F32)
    yy, xx = np.mgrid[0:H, 0:W].astype(F32)
    d = np.hypot(xx - sun_x, yy - sun_y) / H
    hdr = sky.copy(); lights = np.zeros((H, W, 3), F32)
    hdr += (np.exp(-d / 0.05) * 1.6 + np.exp(-d / 0.16) * 0.75 + np.exp(-d / 0.55) * 0.35)[..., None] * col(1.0, 0.58, 0.26)
    cl = fbm(H, W, 2.1, rng, (1.0, 3.0)); cl2 = fbm(H, W, 1.2, rng, (1.0, 2.2)); cl3 = fbm(H, W, 0.8, rng, (1.0, 1.4))
    dens = cl * 0.8 + cl2 * 0.30 + cl3 * 0.08
    cband = np.exp(-((Y - 0.32) / 0.22) ** 2) * 0.9 + np.exp(-((Y - 0.56) / 0.07) ** 2) * 0.45
    shape = np.clip(smooth(-0.3, 1.2, dens - 0.1) * cband * 1.25, 0, 1) * (1 - smooth(0.66, 0.80, Y))
    k = px(9)
    g = np.roll(np.roll(dens, -k, 1), -int(k * 0.6), 0) - dens             # bump-shading toward the sun
    lightf = np.clip(0.55 - 0.45 * g, 0, 1.4) * (0.35 + 0.65 * np.exp(-d / 0.55)) + np.exp(-d / 0.16) * 0.7
    shadow, gold, pink = col(0.26, 0.20, 0.34), col(1.40, 0.78, 0.40), col(0.95, 0.45, 0.45)
    cc = shadow + (pink * 0.6 + gold * 0.5 - shadow) * np.clip(lightf, 0, 1)[..., None]
    cc += (shape * (1 - shape) * 4 * np.exp(-d / 0.30))[..., None] * gold * 0.7
    hdr = hdr * (1 - shape[..., None] * 0.85) + cc * shape[..., None]
    sd = disc(sun_x, sun_y, 0.020 * H, 1.5)
    hdr = hdr * (1 - sd[..., None]) + col(1.0, 0.94, 0.80) * 18 * sd[..., None]
    lights += col(1.0, 0.78, 0.50) * (sd * 18 * 0.10)[..., None] + (np.exp(-d / 0.012) * 2.0)[..., None] * col(1.0, 0.72, 0.42)
    haze = col(0.95, 0.52, 0.34)
    for top_lo, top_hi, wmin, wmax, depth, pitch, p_lit in (
            (0.64, 0.730, 14, 46, 0.95, 5.0, 0.07), (0.62, 0.755, 18, 58, 0.66, 6.5, 0.10),
            (0.66, 0.800, 26, 84, 0.38, 8.5, 0.12), (0.74, 0.835, 40, 120, 0.12, 11.0, 0.12)):
        mask, top, wins = city_layer(rng, top_lo, top_hi, wmin, wmax, depth, pitch, p_lit)
        facade = 1 + 0.10 * fbm(H, W, 0.7, rng)
        sidel = smooth(0.0, 1.0, np.clip(1 - np.abs(np.arange(W)[None, :] - sun_x) / (W * 0.55), 0, 1))[..., None]
        body = (haze * depth * 0.42 + col(0.010, 0.008, 0.016) * (1 - depth) + col(0.10, 0.05, 0.05) * depth * 0.3) * facade[..., None] \
            + col(0.9, 0.42, 0.18) * 0.10 * (1 - depth * 0.6) * sidel
        m = gb(mask, 0.55 * SCM)[..., None]
        hdr = hdr * (1 - m) + body * m
        rimc = edge_light(mask, top, sun_x, col(1.10, 0.62, 0.30), 0.55 + 0.9 * (1 - depth))
        hdr += rimc * m; lights += rimc * 0.9
        wl = wins * mask[..., None] * col(1.0, 0.75, 0.45) * 1.2
        hdr += wl * (0.55 - 0.3 * depth); lights += wl * 0.3
        f = (0.34 * np.exp(-((Y - hor) / 0.14) ** 2) + 0.05)[..., None] * np.clip(depth * 1.3, 0, 1) * 0.8
        hdr = hdr * (1 - f) + haze * 0.85 * f
    foreground(hdr, lights, rng, False, (sun_x, sun_y), col(1.30, 0.72, 0.34))
    bird = Image.new('L', (W * 2, H * 2), 0); bd = ImageDraw.Draw(bird)                 # birds
    for _ in range(13):
        bx = (0.18 + 0.50 * rng.random()) * W * 2; by = (0.16 + 0.16 * rng.random()) * H * 2
        sz = (14 + 20 * rng.random()) * SC * 2
        for sgn in (-1, 1):
            bd.line([(bx, by), (bx + sgn * sz * 0.5, by - sz * 0.42), (bx + sgn * sz, by - sz * 0.08)], fill=255, width=max(1, int(4 * SC)))
    bm = np.asarray(bird.resize((W, H), Image.LANCZOS), F32) / 255.0
    hdr = hdr * (1 - bm[..., None] * 0.9) + col(0.05, 0.03, 0.04) * bm[..., None] * 0.9
    ghosts = (sun_x, sun_y, [(0.30, 46, (1.0, 0.6, 0.3), 0.050), (0.62, 86, (0.5, 0.8, 1.0), 0.030),
                              (1.20, 58, (1.0, 0.75, 0.45), 0.040), (1.55, 140, (0.7, 0.6, 1.0), 0.020), (1.85, 40, (1.0, 0.5, 0.3), 0.04)])
    return post(hdr, lights, rng, 0.85, 0.36, 0.009, (sun_x, sun_y, 0.80, 1.6, col(1.0, 0.62, 0.32)), ghosts, 0.0011)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'assets/wallpapers'
    os.makedirs(out, exist_ok=True)
    for name, fn in (('night', night_scene), ('day', day_scene)):
        img = Image.fromarray((fn() * 255 + 0.5).astype(np.uint8))
        p = os.path.join(out, name + '.jpg')
        img.save(p, quality=90, optimize=True, subsampling=0)
        print(name, img.size, os.path.getsize(p) // 1024, 'KiB')


if __name__ == '__main__':
    main()
