/* MiaouDesk graphics: framebuffer, anti-aliased shapes, TrueType text, PNG/JPEG images. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "desk.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

int SW, SH, UIP = 100;
static int fbfd = -1;
static unsigned char *fbmem;
static size_t fbsize;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static u32 *scene;
static int cx0, cy0, cx1, cy1;

int ui_auto_scale(int w) { return w < 1700 ? 100 : w < 2500 ? 150 : 200; }   /* whole steps keep the 4px grid */

int gfx_open(void) {
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) return 1;
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fix) < 0 || ioctl(fbfd, FBIOGET_VSCREENINFO, &var) < 0) return 2;
    if (var.bits_per_pixel != 16 && var.bits_per_pixel != 24 && var.bits_per_pixel != 32) return 3;
    fbsize = fix.smem_len;
    fbmem = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbmem == MAP_FAILED) { fbmem = NULL; return 4; }
    SW = (int)var.xres; SH = (int)var.yres;
    scene = calloc((size_t)SW * SH, 4);
    if (!scene) return 5;
    gfx_unclip();
    UIP = ui_auto_scale(SW);
    return 0;
}

void gfx_close(void) {
    if (fbmem) munmap(fbmem, fbsize);
    if (fbfd >= 0) close(fbfd);
    fbmem = NULL; fbfd = -1;
}

static int bx0, by0, bx1 = 1 << 20, by1 = 1 << 20;     /* outer clip for partial redraws: gfx_clip/gfx_unclip never leave it */
void gfx_clip(int x, int y, int w, int h) {
    cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
    cx1 = x + w > SW ? SW : x + w; cy1 = y + h > SH ? SH : y + h;
    if (cx0 < bx0) cx0 = bx0; if (cy0 < by0) cy0 = by0; if (cx1 > bx1) cx1 = bx1; if (cy1 > by1) cy1 = by1;
}
void gfx_unclip(void) { cx0 = bx0 > 0 ? bx0 : 0; cy0 = by0 > 0 ? by0 : 0; cx1 = bx1 < SW ? bx1 : SW; cy1 = by1 < SH ? by1 : SH; }
void gfx_set_base_clip(int x, int y, int w, int h) { bx0 = x; by0 = y; bx1 = x + w; by1 = y + h; gfx_unclip(); }
void gfx_clear_base_clip(void) { bx0 = by0 = 0; bx1 = by1 = 1 << 20; gfx_unclip(); }

static inline u32 mixa(u32 d, u32 s, int a) {   /* a: 0..255 */
    if (a >= 255) return s;
    if (a <= 0) return d;
    int ia = 255 - a;
    u32 r = (((d >> 16) & 255) * ia + ((s >> 16) & 255) * a + 127) / 255;
    u32 g = (((d >> 8) & 255) * ia + ((s >> 8) & 255) * a + 127) / 255;
    u32 b = ((d & 255) * ia + (s & 255) * a + 127) / 255;
    return (r << 16) | (g << 8) | b;
}
u32 gfx_mix(u32 a, u32 b, int t) { return mixa(a, b, t); }

static inline void put(int x, int y, u32 c, int a) {
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    u32 *p = &scene[(size_t)y * SW + x];
    *p = mixa(*p, c, a);
}

void gfx_fill(int x, int y, int w, int h, u32 c, int a) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    if (x1 <= x0 || y1 <= y0 || a <= 0) return;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = &scene[(size_t)yy * SW + x0];
        if (a >= 255) for (int xx = x0; xx < x1; xx++) *p++ = c;
        else for (int xx = x0; xx < x1; xx++, p++) *p = mixa(*p, c, a);
    }
}
void gfx_hline(int x, int y, int w, u32 c, int a) { gfx_fill(x, y, w, 1, c, a); }
void gfx_vline(int x, int y, int h, u32 c, int a) { gfx_fill(x, y, 1, h, c, a); }

/* coverage (0..255) of pixel centre (px,py) relative to corner circle centre, radius r */
static inline int cover(float dx, float dy, float r) {
    float d = sqrtf(dx * dx + dy * dy);
    float v = r - d + 0.5f;
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return (int)(v * 255);
}

void gfx_rrect(int x, int y, int w, int h, int r, u32 c, int a) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) { gfx_fill(x, y, w, h, c, a); return; }
    gfx_fill(x, y + r, w, h - 2 * r, c, a);
    for (int row = 0; row < r; row++) {
        /* top row y+row and bottom row y+h-1-row */
        int span0 = 0;
        for (int col = 0; col < r; col++) {
            float dx = (float)(r - col) - 0.5f, dy = (float)(r - row) - 0.5f;
            int cv = cover(dx, dy, (float)r);
            int aa = cv * a / 255;
            if (aa <= 0) { span0 = col + 1; continue; }
            if (cv >= 255) break;
            put(x + col, y + row, c, aa); put(x + w - 1 - col, y + row, c, aa);
            put(x + col, y + h - 1 - row, c, aa); put(x + w - 1 - col, y + h - 1 - row, c, aa);
            span0 = col + 1;
        }
        int inner = span0;
        /* fully covered middle part of the row */
        int fx = x + inner;
        gfx_fill(fx, y + row, w - 2 * inner, 1, c, a);
        gfx_fill(fx, y + h - 1 - row, w - 2 * inner, 1, c, a);
    }
}

void gfx_rrect_line(int x, int y, int w, int h, int r, u32 c, int a) {
    /* 1px outline: draw outer rrect then the inner one in a way that only the ring remains via per-pixel coverage */
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    gfx_fill(x + r, y, w - 2 * r, 1, c, a); gfx_fill(x + r, y + h - 1, w - 2 * r, 1, c, a);
    gfx_fill(x, y + r, 1, h - 2 * r, c, a); gfx_fill(x + w - 1, y + r, 1, h - 2 * r, c, a);
    for (int row = 0; row < r; row++)
        for (int col = 0; col < r; col++) {
            float dx = (float)(r - col) - 0.5f, dy = (float)(r - row) - 0.5f;
            float d = sqrtf(dx * dx + dy * dy);
            float v = 1.0f - fabsf(d - (float)r + 0.5f);
            if (v <= 0) continue;
            int aa = (int)(v * a);
            put(x + col, y + row, c, aa); put(x + w - 1 - col, y + row, c, aa);
            put(x + col, y + h - 1 - row, c, aa); put(x + w - 1 - col, y + h - 1 - row, c, aa);
        }
}

void gfx_circle(int cx, int cy, int r, u32 c, int a) {
    for (int y = -r - 1; y <= r; y++)
        for (int x = -r - 1; x <= r; x++) {
            int cv = cover((float)x + 0.5f, (float)y + 0.5f, (float)r);
            if (cv) put(cx + x, cy + y, c, cv * a / 255);
        }
}

/* Soft shadow: the alpha mask depends only on (w,h,r,spread), so it is computed once and cached. */
static struct { int w, h, r, sp; unsigned char *m; } scache[8];
static int snext;
static unsigned char *shadow_mask(int w, int h, int r, int spread) {
    for (int i = 0; i < 8; i++) if (scache[i].m && scache[i].w == w && scache[i].h == h && scache[i].r == r && scache[i].sp == spread) return scache[i].m;
    int mw = w + 2 * spread, mh = h + 2 * spread;
    unsigned char *m = calloc((size_t)mw * mh, 1);
    for (int yy = 0; yy < mh; yy++)
        for (int xx = 0; xx < mw; xx++) {
            float qx = fabsf((float)xx + 0.5f - mw / 2.0f) - (w / 2.0f - r);
            float qy = fabsf((float)yy + 0.5f - mh / 2.0f) - (h / 2.0f - r);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, mx = qx > qy ? qx : qy;
            float d = sqrtf(ox * ox + oy * oy) + (mx < 0 ? mx : 0) - (float)r;
            if (d <= 0 || d >= spread) continue;
            float t = 1.0f - d / (float)spread;
            m[(size_t)yy * mw + xx] = (unsigned char)(t * t * 255);
        }
    free(scache[snext].m);
    scache[snext].w = w; scache[snext].h = h; scache[snext].r = r; scache[snext].sp = spread; scache[snext].m = m;
    snext = (snext + 1) % 8;
    return m;
}
void gfx_shadow(int x, int y, int w, int h, int r, int spread, int maxa) {
    if (w <= 0 || h <= 0) return;
    unsigned char *m = shadow_mask(w, h, r, spread);
    int mw = w + 2 * spread, mh = h + 2 * spread, x0 = x - spread, y0 = y - spread;
    for (int yy = 0; yy < mh; yy++) {
        int py = y0 + yy; if (py < cy0 || py >= cy1) continue;
        int mid = yy >= spread + r && yy < spread + h - r;
        for (int xx = 0; xx < mw; xx++) {
            if (mid && xx >= spread && xx < spread + w) { xx = spread + w - 1; continue; }
            int px = x0 + xx; if (px < cx0 || px >= cx1) continue;
            int a = m[(size_t)yy * mw + xx]; if (!a) continue;
            u32 *p = &scene[(size_t)py * SW + px]; *p = mixa(*p, 0, a * maxa / 255);
        }
    }
}

void gfx_blit(const u32 *src, int sw, int sh, int dx, int dy) {
    int xa = dx < cx0 ? cx0 : dx, xb = dx + sw > cx1 ? cx1 : dx + sw;
    if (xb <= xa) return;
    for (int y = 0; y < sh; y++) {
        int yy = dy + y; if (yy < cy0 || yy >= cy1) continue;
        memcpy(&scene[(size_t)yy * SW + xa], &src[(size_t)y * sw + (xa - dx)], (size_t)(xb - xa) * 4);
    }
}

/* snapshot of a scene region, used to fade an element in/out: draw it, then blend with the snapshot */
Snap gfx_snap(int x, int y, int w, int h) {
    Snap s = { NULL, 0, 0, 0, 0 };
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > SW ? SW : x + w, y1 = y + h > SH ? SH : y + h;
    if (x1 <= x0 || y1 <= y0) return s;
    s.x = x0; s.y = y0; s.w = x1 - x0; s.h = y1 - y0;
    s.px = malloc((size_t)s.w * s.h * 4);
    for (int j = 0; j < s.h; j++) memcpy(&s.px[(size_t)j * s.w], &scene[(size_t)(y0 + j) * SW + x0], (size_t)s.w * 4);
    return s;
}
void gfx_snap_blend(Snap *s, int alpha) {      /* alpha: opacity of what was drawn since the snapshot (0..255) */
    if (!s->px) return;
    for (int j = 0; j < s->h; j++) {
        u32 *d = &scene[(size_t)(s->y + j) * SW + s->x]; const u32 *o = &s->px[(size_t)j * s->w];
        for (int i = 0; i < s->w; i++) d[i] = mixa(o[i], d[i], alpha);
    }
    free(s->px); s->px = NULL;
}

/* ------------------------------------------------------------------ presentation */
static inline u32 packpx(u32 rgb) {
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    u32 v = 0;
    if (var.red.length) v |= ((r >> (8 - var.red.length)) << var.red.offset);
    if (var.green.length) v |= ((g >> (8 - var.green.length)) << var.green.offset);
    if (var.blue.length) v |= ((b >> (8 - var.blue.length)) << var.blue.offset);
    return v;
}
void gfx_present_rect(int x, int y, int w, int h) {
    if (!fbmem) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > SW ? SW : x + w, y1 = y + h > SH ? SH : y + h;
    if (x1 <= x0 || y1 <= y0) return;
    int bpp = (int)var.bits_per_pixel / 8;
    int fast = bpp == 4 && var.red.offset == 16 && var.green.offset == 8 && var.blue.offset == 0 && var.red.length == 8;
    for (int yy = y0; yy < y1; yy++) {
        unsigned char *row = fbmem + (size_t)yy * fix.line_length;
        const u32 *s = &scene[(size_t)yy * SW];
        if (fast) { memcpy(row + (size_t)x0 * 4, s + x0, (size_t)(x1 - x0) * 4); continue; }
        for (int xx = x0; xx < x1; xx++) {
            u32 v = packpx(s[xx]);
            memcpy(row + (size_t)xx * bpp, &v, (size_t)bpp);
        }
    }
}
u32 gfx_scene_px(int x, int y) { if (!scene || x < 0 || y < 0 || x >= SW || y >= SH) return 0; return scene[(size_t)y * SW + x]; }
void gfx_fb_pixel(int x, int y, u32 rgb) {
    if (!fbmem || x < 0 || y < 0 || x >= SW || y >= SH) return;
    int bpp = (int)var.bits_per_pixel / 8;
    u32 v = packpx(rgb);
    memcpy(fbmem + (size_t)y * fix.line_length + (size_t)x * bpp, &v, (size_t)bpp);
}
void gfx_present(void) { gfx_present_rect(0, 0, SW, SH); }

/* ------------------------------------------------------------------ text */
struct Glyph { int cp, px; unsigned char *bm; int w, h, xo, yo, adv; };
struct Font { stbtt_fontinfo info; unsigned char *data; struct Glyph *cache; int ncache; };

Font *font_load(const char *path) {
    size_t n = 0;
    unsigned char *d = (unsigned char *)read_file(path, &n, 8 * 1024 * 1024);
    if (!d) return NULL;
    Font *f = calloc(1, sizeof *f);
    f->data = d;
    if (!stbtt_InitFont(&f->info, d, stbtt_GetFontOffsetForIndex(d, 0))) { free(d); free(f); return NULL; }
    f->ncache = 4096;
    f->cache = calloc((size_t)f->ncache, sizeof(struct Glyph));
    return f;
}

static struct Glyph *glyph(Font *f, int cp, int px) {
    unsigned h = ((unsigned)cp * 2654435761u + (unsigned)px * 97u) % (unsigned)f->ncache;
    for (int probe = 0; probe < 32; probe++) {
        struct Glyph *g = &f->cache[(h + (unsigned)probe) % (unsigned)f->ncache];
        if (g->px == px && g->cp == cp + 1) return g;
        if (g->px == 0) {
            float sc = stbtt_ScaleForPixelHeight(&f->info, (float)px);
            int adv, lsb, x0, y0, x1, y1;
            stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
            stbtt_GetCodepointBitmapBox(&f->info, cp, sc, sc, &x0, &y0, &x1, &y1);
            g->cp = cp + 1; g->px = px; g->adv = (int)(adv * sc + 0.5f);
            g->w = x1 - x0; g->h = y1 - y0; g->xo = x0; g->yo = y0;
            g->bm = NULL;
            if (g->w > 0 && g->h > 0) {
                g->bm = malloc((size_t)g->w * g->h);
                stbtt_MakeCodepointBitmap(&f->info, g->bm, g->w, g->h, g->w, sc, sc, cp);
            }
            return g;
        }
    }
    return &f->cache[0];
}

int utf8_next(const char **sp) {
    const unsigned char *s = (const unsigned char *)*sp;
    int c = *s;
    if (!c) return 0;
    int n = 1;
    if (c < 0x80) { *sp += 1; return c; }
    if ((c & 0xe0) == 0xc0) { c &= 0x1f; n = 2; }
    else if ((c & 0xf0) == 0xe0) { c &= 0x0f; n = 3; }
    else if ((c & 0xf8) == 0xf0) { c &= 0x07; n = 4; }
    else { *sp += 1; return '?'; }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xc0) != 0x80) { *sp += i; return '?'; }
        c = (c << 6) | (s[i] & 0x3f);
    }
    *sp += n;
    return c;
}
int utf8_put(char *o, int cp) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xc0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xe0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
    o[0] = (char)(0xf0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 63)); o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63));
    return 4;
}

int glyph_draw(Font *f, int x, int y, int cp, u32 c, int px) {
    struct Glyph *g = glyph(f, cp, px);
    if (g->bm) {
        int base = y + (int)(px * 0.80f);       /* baseline position inside the line box */
        for (int j = 0; j < g->h; j++) {
            int yy = base + g->yo + j;
            if (yy < cy0 || yy >= cy1) continue;
            for (int i = 0; i < g->w; i++) {
                int a = g->bm[j * g->w + i];
                if (a) put(x + g->xo + i, yy, c, a);
            }
        }
    }
    return g->adv;
}

int text_w(Font *f, const char *s, int px) {
    int w = 0;
    while (*s) { int cp = utf8_next(&s); w += glyph(f, cp, px)->adv; }
    return w;
}
int text_draw(Font *f, int x, int y, const char *s, u32 c, int px) {
    int x0 = x;
    while (*s) { int cp = utf8_next(&s); x += glyph_draw(f, x, y, cp, c, px); }
    return x - x0;
}
int text_draw_clip(Font *f, int x, int y, const char *s, u32 c, int px, int maxw) {
    int sx = cx0, sy = cy0, ex = cx1, ey = cy1;
    int nx = x < cx0 ? cx0 : x, nex = x + maxw < cx1 ? x + maxw : cx1;
    cx0 = nx; cx1 = nex;
    int r = text_draw(f, x, y, s, c, px);
    cx0 = sx; cy0 = sy; cx1 = ex; cy1 = ey;
    return r;
}
int font_line_h(Font *f, int px) { (void)f; return (int)(px * 1.3f); }
int font_cell_w(Font *f, int px) { return glyph(f, 'M', px)->adv; }

/* ------------------------------------------------------------------ images */
static const char *ASSET_DIR = "/usr/share/miaou";

Img *img_load_file(const char *path) {
    size_t n = 0;
    unsigned char *d = (unsigned char *)read_file(path, &n, 16 * 1024 * 1024);
    if (!d) return NULL;
    int w, h, ch;
    unsigned char *px = stbi_load_from_memory(d, (int)n, &w, &h, &ch, 4);
    free(d);
    if (!px) return NULL;
    Img *im = malloc(sizeof *im);
    im->w = w; im->h = h; im->mono = 0; im->px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        unsigned r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2], a = px[i * 4 + 3];
        im->px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    stbi_image_free(px);
    return im;
}
void img_free(Img *im) { if (im) { free(im->px); free(im); } }

Img *img_scale(Img *src, int w, int h) {
    Img *o = malloc(sizeof *o);
    o->mono = src->mono;
    o->w = w; o->h = h; o->px = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            /* area-ish sampling: average the source pixels covered (box filter) */
            int sx0 = x * src->w / w, sx1 = (x + 1) * src->w / w; if (sx1 <= sx0) sx1 = sx0 + 1;
            int sy0 = y * src->h / h, sy1 = (y + 1) * src->h / h; if (sy1 <= sy0) sy1 = sy0 + 1;
            unsigned long a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int yy = sy0; yy < sy1 && yy < src->h; yy++)
                for (int xx = sx0; xx < sx1 && xx < src->w; xx++) {
                    u32 p = src->px[(size_t)yy * src->w + xx];
                    unsigned pa = p >> 24;
                    a += pa; r += ((p >> 16) & 255) * pa; g += ((p >> 8) & 255) * pa; b += (p & 255) * pa; n++;
                }
            if (!n || !a) { o->px[(size_t)y * w + x] = 0; continue; }
            o->px[(size_t)y * w + x] = ((u32)(a / n) << 24) | ((u32)(r / a) << 16) | ((u32)(g / a) << 8) | (u32)(b / a);
        }
    return o;
}

void img_draw_tint(Img *im, int x, int y, int a, u32 color) {
    if (!im) return;
    for (int j = 0; j < im->h; j++) {
        int yy = y + j; if (yy < cy0 || yy >= cy1) continue;
        for (int i = 0; i < im->w; i++) {
            u32 p = im->px[(size_t)j * im->w + i];
            int pa = (int)(p >> 24) * a / 255;
            if (pa) put(x + i, yy, im->mono ? color : (p & 0xffffff), pa);
        }
    }
}
void img_draw(Img *im, int x, int y, int a) { img_draw_tint(im, x, y, a, TH.text); }

/* anti-aliased thick line (distance to segment) */
void gfx_line(float x0, float y0, float x1, float y1, float width, u32 c, int a) {
    float minx = fminf(x0, x1) - width, maxx = fmaxf(x0, x1) + width, miny = fminf(y0, y1) - width, maxy = fmaxf(y0, y1) + width;
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy, hw = width / 2;
    for (int y = (int)miny; y <= (int)maxy + 1; y++)
        for (int x = (int)minx; x <= (int)maxx + 1; x++) {
            float px = (float)x + 0.5f - x0, py = (float)y + 0.5f - y0;
            float t = l2 > 0 ? (px * dx + py * dy) / l2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
            float ex = px - t * dx, ey = py - t * dy, d = sqrtf(ex * ex + ey * ey);
            float v = hw - d + 0.5f; if (v <= 0) continue; if (v > 1) v = 1;
            put(x, y, c, (int)(v * a));
        }
}
/* anti-aliased triangle */
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c, int a) {
    float minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2)), miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0) return;
    float sgn = area > 0 ? 1.0f : -1.0f;
    float ex[3] = { x1 - x0, x2 - x1, x0 - x2 }, ey[3] = { y1 - y0, y2 - y1, y0 - y2 }, ox[3] = { x0, x1, x2 }, oy[3] = { y0, y1, y2 };
    for (int y = (int)miny - 1; y <= (int)maxy + 1; y++)
        for (int x = (int)minx - 1; x <= (int)maxx + 1; x++) {
            float cov = 1.0f;
            for (int k = 0; k < 3; k++) {
                float len = sqrtf(ex[k] * ex[k] + ey[k] * ey[k]); if (len == 0) continue;
                float d = sgn * (ex[k] * ((float)y + 0.5f - oy[k]) - ey[k] * ((float)x + 0.5f - ox[k])) / len;   /* signed distance, >0 inside */
                float v = d + 0.5f; if (v < cov) cov = v;
            }
            if (cov > 0) put(x, y, c, (int)((cov > 1 ? 1 : cov) * a));
        }
}
/* Render a drawing twice (on black, then on white) into a private buffer and recover straight alpha: the result is a small
 * ARGB image that can be blitted cheaply every frame (icons, logo) instead of re-running the anti-aliased rasterisers. */
Img *gfx_cache_render(int w, int h, void (*draw)(void *ud, int ox, int oy), void *ud) {
    u32 *a = malloc((size_t)w * (size_t)h * 4), *b = malloc((size_t)w * (size_t)h * 4); if (!a || !b) { free(a); free(b); return NULL; }
    u32 *ss = scene; int ssw = SW, ssh = SH, sx0 = cx0, sy0 = cy0, sx1 = cx1, sy1 = cy1;
    scene = a; SW = w; SH = h; cx0 = 0; cy0 = 0; cx1 = w; cy1 = h;
    for (int i = 0; i < w * h; i++) a[i] = 0; draw(ud, 0, 0);
    scene = b; for (int i = 0; i < w * h; i++) b[i] = 0xffffff; draw(ud, 0, 0);
    scene = ss; SW = ssw; SH = ssh; cx0 = sx0; cy0 = sy0; cx1 = sx1; cy1 = sy1;
    Img *im = calloc(1, sizeof *im); im->w = w; im->h = h; im->px = malloc((size_t)w * (size_t)h * 4);
    for (int i = 0; i < w * h; i++) {
        int dr = (int)((b[i] >> 16) & 255) - (int)((a[i] >> 16) & 255), dg = (int)((b[i] >> 8) & 255) - (int)((a[i] >> 8) & 255), db = (int)(b[i] & 255) - (int)(a[i] & 255);
        int al = 255 - (dr + dg + db) / 3; if (al < 0) al = 0; if (al > 255) al = 255;
        if (al == 0) { im->px[i] = 0; continue; }
        int r = (int)((a[i] >> 16) & 255) * 255 / al, g = (int)((a[i] >> 8) & 255) * 255 / al, bl = (int)(a[i] & 255) * 255 / al;
        if (r > 255) r = 255; if (g > 255) g = 255; if (bl > 255) bl = 255;
        im->px[i] = ((u32)al << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
    }
    free(a); free(b); return im;
}
/* Rounded triangle: the triangle (x0,y0)-(x1,y1)-(x2,y2) inflated by rho, anti-aliased through its signed distance field. */
static float seg_d2(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float qx = px - (ax + dx * t), qy = py - (ay + dy * t); return qx * qx + qy * qy;
}
void gfx_rtri(float x0, float y0, float x1, float y1, float x2, float y2, float rho, u32 c, int a) {
    float minx = fminf(x0, fminf(x1, x2)) - rho, maxx = fmaxf(x0, fmaxf(x1, x2)) + rho, miny = fminf(y0, fminf(y1, y2)) - rho, maxy = fmaxf(y0, fmaxf(y1, y2)) + rho;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0), sg = area > 0 ? 1.0f : -1.0f;
    for (int y = (int)miny - 1; y <= (int)maxy + 1; y++)
        for (int x = (int)minx - 1; x <= (int)maxx + 1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float d2 = fminf(seg_d2(px, py, x0, y0, x1, y1), fminf(seg_d2(px, py, x1, y1, x2, y2), seg_d2(px, py, x2, y2, x0, y0)));
            /* inside test by edge signs */
            float e0 = sg * ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)), e1 = sg * ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)), e2 = sg * ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2));
            float d = sqrtf(d2); if (e0 >= 0 && e1 >= 0 && e2 >= 0) d = -d;
            float cov = 0.5f - (d - rho); if (cov <= 0) continue; if (cov > 1) cov = 1;
            put(x, y, c, (int)(cov * (float)a));
        }
}
/* MiaouOS logo: a cat head shaped like the letter M (its two peaks are the ears), closed happy eyes, an omega mouth and ///
 * blush marks. It is a brand mark: always amber with a slate face, whatever colours the caller passes (kept for the API). */
static void arc2(float x0, float y0, float cx, float cy, float x1, float y1, float w, u32 c) {
    float px = x0, py = y0;
    for (int i = 1; i <= 8; i++) {
        float t = (float)i / 8.0f, u = 1 - t, qx = u * u * x0 + 2 * u * t * cx + t * t * x1, qy = u * u * y0 + 2 * u * t * cy + t * t * y1;
        gfx_line(px, py, qx, qy, w, c, 255); px = qx; py = qy;
    }
    gfx_circle((int)(x0 + 0.5f), (int)(y0 + 0.5f), (int)(w / 2 + 0.5f), c, 255); gfx_circle((int)(x1 + 0.5f), (int)(y1 + 0.5f), (int)(w / 2 + 0.5f), c, 255);
}
static void logo_raw(int x, int y, int size) {
    const u32 head = RGB(242, 169, 59), ink = RGB(38, 50, 56), blush = RGB(217, 71, 92);
    float k = (float)size / 150.0f, ox = (float)x - 25.0f * k, oy = (float)y - 40.0f * k + 5.0f * k;
    #define LX(v) (ox + (v) * k)
    #define LY(v) (oy + (v) * k)
    float r = 9.0f * k;
    gfx_rtri(LX(36), LY(54), LX(100), LY(108), LX(36), LY(108), r, head, 255);       /* left ear */
    gfx_rtri(LX(164), LY(54), LX(100), LY(108), LX(164), LY(108), r, head, 255);     /* right ear */
    gfx_rtri(LX(36), LY(108), LX(164), LY(108), LX(36), LY(170), r, head, 255);      /* body = two triangles */
    gfx_rtri(LX(164), LY(108), LX(164), LY(170), LX(36), LY(170), r, head, 255);
    float ew = 7.0f * k;
    arc2(LX(62), LY(130), LX(74), LY(114), LX(86), LY(130), ew, ink);                /* ^ ^ eyes */
    arc2(LX(114), LY(130), LX(126), LY(114), LX(138), LY(130), ew, ink);
    float mw = 5.0f * k;
    arc2(LX(89), LY(146), LX(94.5f), LY(156), LX(100), LY(146), mw, ink);            /* omega mouth */
    arc2(LX(100), LY(146), LX(105.5f), LY(156), LX(111), LY(146), mw, ink);
    float bw = 4.5f * k; if (bw < 1.3f) bw = 1.3f;                                    /* blush stays visible at small sizes */
    if (size >= 16) for (int side = 0; side < 2; side++) for (int i = -1; i <= 1; i++) {
        float cx = side ? 142.0f : 58.0f;
        gfx_line(LX(cx + i * 9 - 4), LY(157), LX(cx + i * 9 + 4), LY(143), bw, blush, 255);
    }
    #undef LX
    #undef LY
}
typedef struct { int size; Img *im; } LogoC;
static LogoC logoc[8];
static void logo_draw_cb(void *ud, int ox, int oy) { logo_raw(ox + 2, oy + 2, *(int *)ud); }
void gfx_logo(int x, int y, int size, u32 color, u32 eye) {
    (void)color; (void)eye;
    LogoC *c = NULL; for (int i = 0; i < 8; i++) if (logoc[i].im && logoc[i].size == size) c = &logoc[i];
    if (!c) { for (int i = 0; i < 8 && !c; i++) if (!logoc[i].im) c = &logoc[i]; if (!c) { img_free(logoc[0].im); logoc[0].im = NULL; c = &logoc[0]; }
        int sz = size; c->im = gfx_cache_render(size + 4, size + 4, logo_draw_cb, &sz); c->size = size; }
    if (c->im) img_draw_tint(c->im, x - 2, y - 2, 255, 0); else logo_raw(x, y, size);
}

#define ICON_CACHE 96
static struct { char name[24]; int size; Img *im; } icache[ICON_CACHE];
Img *img_get(const char *name, int size) {
    for (int i = 0; i < ICON_CACHE; i++)
        if (icache[i].im && icache[i].size == size && !strcmp(icache[i].name, name)) return icache[i].im;
    char path[160]; int mono = 0;
    snprintf(path, sizeof path, "%s/icons/%s.png", ASSET_DIR, name);
    Img *src = img_load_file(path);
    if (!src) { snprintf(path, sizeof path, "%s/icons/mono/%s.png", ASSET_DIR, name); src = img_load_file(path); mono = 1; }
    if (!src) return NULL;
    src->mono = mono;
    Img *sc = img_scale(src, size, size);
    img_free(src);
    for (int i = 0; i < ICON_CACHE; i++)
        if (!icache[i].im) { snprintf(icache[i].name, sizeof icache[i].name, "%s", name); icache[i].size = size; icache[i].im = sc; break; }
    return sc;
}

Img *img_scale_smooth(Img *src, int w, int h) {
    Img *o = malloc(sizeof *o); o->mono = 0; o->w = w; o->h = h; o->px = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        float fy = ((float)y + 0.5f) * src->h / h - 0.5f; int y0 = (int)floorf(fy); float ty = fy - y0; int y1 = y0 + 1;
        if (y0 < 0) y0 = 0; if (y1 >= src->h) y1 = src->h - 1; if (y0 >= src->h) y0 = src->h - 1;
        for (int x = 0; x < w; x++) {
            float fx = ((float)x + 0.5f) * src->w / w - 0.5f; int x0 = (int)floorf(fx); float tx = fx - x0; int x1 = x0 + 1;
            if (x0 < 0) x0 = 0; if (x1 >= src->w) x1 = src->w - 1; if (x0 >= src->w) x0 = src->w - 1;
            u32 c00 = src->px[(size_t)y0 * src->w + x0], c10 = src->px[(size_t)y0 * src->w + x1], c01 = src->px[(size_t)y1 * src->w + x0], c11 = src->px[(size_t)y1 * src->w + x1];
            u32 out = 0xff000000u;
            for (int sh = 0; sh <= 16; sh += 8) {
                float a = (float)((c00 >> sh) & 255) * (1 - tx) + (float)((c10 >> sh) & 255) * tx, b = (float)((c01 >> sh) & 255) * (1 - tx) + (float)((c11 >> sh) & 255) * tx;
                out |= (u32)(a * (1 - ty) + b * ty) << sh;
            }
            o->px[(size_t)y * w + x] = out;
        }
    }
    return o;
}
static Img *wall_blur[2];
static void blur_1d(const unsigned char *src, unsigned char *dst, int n, int stride, int r) {    /* running-sum box blur of one line, edges clamped */
    long sum = 0;
    for (int k = -r; k <= r; k++) { int idx = k < 0 ? 0 : k >= n ? n - 1 : k; sum += src[(size_t)idx * stride]; }
    for (int i = 0; i < n; i++) {
        dst[(size_t)i * stride] = (unsigned char)(sum / (2 * r + 1));
        int add = i + r + 1, rem = i - r; if (add >= n) add = n - 1; if (rem < 0) rem = 0;
        sum += (long)src[(size_t)add * stride] - (long)src[(size_t)rem * stride];
    }
}
static void box_blur(Img *im, int r) {            /* planar per-channel separable blur */
    int w = im->w, h = im->h; size_t np = (size_t)w * h;
    unsigned char *pl = malloc(np), *t1 = malloc(np), *t2 = malloc(np);
    for (int ch = 0; ch <= 16; ch += 8) {
        for (size_t i = 0; i < np; i++) pl[i] = (unsigned char)(im->px[i] >> ch);
        for (int y = 0; y < h; y++) blur_1d(pl + (size_t)y * w, t1 + (size_t)y * w, w, 1, r);
        for (int x = 0; x < w; x++) blur_1d(t1 + x, t2 + x, h, w, r);
        for (size_t i = 0; i < np; i++) im->px[i] = (im->px[i] & ~(255u << ch)) | ((u32)t2[i] << ch);
    }
    free(pl); free(t1); free(t2);
}
Img *wallpaper_blur(int night) {          /* smooth gaussian-like blur at 1/6 resolution, cached */
    int k = night ? 1 : 0;
    if (wall_blur[k] && wall_blur[k]->w == SW && wall_blur[k]->h == SH) return wall_blur[k];
    img_free(wall_blur[k]); wall_blur[k] = NULL;
    Img *wp = wallpaper(night); if (!wp) return NULL;
    Img *small = img_scale(wp, SW / 6 > 16 ? SW / 6 : 16, SH / 6 > 16 ? SH / 6 : 16);
    for (int i = 0; i < 3; i++) box_blur(small, 4);
    wall_blur[k] = img_scale_smooth(small, SW, SH);
    img_free(small);
    return wall_blur[k];
}
int gfx_save_png(const char *path) {
    unsigned char *rgb = malloc((size_t)SW * SH * 3);
    for (int i = 0; i < SW * SH; i++) { u32 c = scene[i]; rgb[i * 3] = (unsigned char)(c >> 16); rgb[i * 3 + 1] = (unsigned char)(c >> 8); rgb[i * 3 + 2] = (unsigned char)c; }
    int ok = stbi_write_png(path, SW, SH, 3, rgb, SW * 3);
    free(rgb);
    return ok ? 0 : -1;
}
static Img *wall[2];
static char custom_wall[320];
void wallpaper_set_custom(const char *path) {
    if (!strcmp(custom_wall, path ? path : "")) return;
    snprintf(custom_wall, sizeof custom_wall, "%s", path ? path : "");
    for (int k = 0; k < 2; k++) { img_free(wall[k]); wall[k] = NULL; img_free(wall_blur[k]); wall_blur[k] = NULL; }
}
Img *wallpaper(int night) {
    int k = night ? 1 : 0;
    if (wall[k] && wall[k]->w == SW && wall[k]->h == SH) return wall[k];
    img_free(wall[k]); wall[k] = NULL;
    char path[330];
    if (custom_wall[0]) snprintf(path, sizeof path, "%s", custom_wall); else snprintf(path, sizeof path, "%s/wallpapers/%s.jpg", ASSET_DIR, night ? "night" : "day");
    Img *src = img_load_file(path);
    if (!src) return NULL;
    /* cover: crop the source to the screen aspect, then scale */
    int cw = src->w, ch = src->h;
    if ((long)cw * SH > (long)ch * SW) cw = (int)((long)ch * SW / SH); else ch = (int)((long)cw * SH / SW);
    Img crop = { cw, ch, malloc((size_t)cw * ch * 4), 0 };
    int ox = (src->w - cw) / 2, oy = (src->h - ch) / 2;
    for (int y = 0; y < ch; y++) memcpy(&crop.px[(size_t)y * cw], &src->px[(size_t)(y + oy) * src->w + ox], (size_t)cw * 4);
    img_free(src);
    wall[k] = img_scale(&crop, SW, SH);
    free(crop.px);
    return wall[k];
}
