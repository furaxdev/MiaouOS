/* Miaou Web: block/inline layout into a display list, painting and hit-testing. */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web.h"

static Page *LP;
static int ly, lpend;

static int di_add(int kind, int x, int y, int w, int h) {
    if (LP->ndi >= LP->capdi) { LP->capdi = LP->capdi ? LP->capdi * 2 : 1024; LP->di = realloc(LP->di, (size_t)LP->capdi * sizeof(DI)); }
    DI *d = &LP->di[LP->ndi]; memset(d, 0, sizeof *d); d->kind = kind; d->x = x; d->y = y; d->w = w; d->h = h; return LP->ndi++;
}
static void di_truncate(int n) { for (int i = n; i < LP->ndi; i++) free(LP->di[i].s); LP->ndi = n; }
static void di_shift(int a, int b, int dx, int dy) { for (int i = a; i < b; i++) { LP->di[i].x += dx; LP->di[i].y += dy; } }
static void flush_pending(void) { ly += lpend; lpend = 0; }

typedef struct { int kind, w, h, gap, px, d0, d1, ul, strike, has_bg, fixed; char *s; Font *f; u32 c, bg; Node *node, *owner; } LI;   /* kind 0 word, 2 br, 3 box, 4 control */
typedef struct { LI *it; int n, cap, x, w, align, pend_space, nowrap; Node *flow; } LB;

static LI *li_add(LB *lb) { if (lb->n >= lb->cap) { lb->cap = lb->cap ? lb->cap * 2 : 64; lb->it = realloc(lb->it, (size_t)lb->cap * sizeof(LI)); } LI *i = &lb->it[lb->n++]; memset(i, 0, sizeof *i); return i; }
static Font *font_of(const Style *s) { return s->mono ? F_MONO : s->bold ? F_BOLD : F_UI; }
static int line_h(const Style *s) { return s->lh ? S(s->lh) : S(s->fs) * 135 / 100; }

static int force_tw, force_h;           /* flex/grid items: exact total width / minimum height for the next layout_block */
static void layout_block(Node *n, int x, int w);
static void layout_children(Node *n, int x, int w);
static void collect(LB *lb, Node *e);

static void add_text(LB *lb, Node *tn, const Style *st) {
    const char *t = tn->text ? tn->text : ""; Font *f = font_of(st); int px = S(st->fs);
    if (st->pre) {
        for (const char *p = t; *p; ) {
            const char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p);
            if (l || !e) { LI *i = li_add(lb); i->kind = 0; i->s = strndup(p, l); i->f = f; i->px = px; i->w = text_w(f, i->s, px); i->h = line_h(st); i->c = st->color; i->ul = st->ul; i->strike = st->strike; i->node = tn->parent; i->fixed = 1; }
            if (e) { LI *b = li_add(lb); b->kind = 2; b->h = line_h(st); p = e + 1; } else break;
        }
        return;
    }
    const char *p = t;
    while (*p) {
        int sp = 0; while (*p && isspace((unsigned char)*p)) { sp = 1; p++; }
        if (sp) lb->pend_space = 1;
        if (!*p) break;
        const char *s = p; while (*p && !isspace((unsigned char)*p)) p++;
        char *w = strndup(s, (size_t)(p - s));
        if (st->upper) for (char *c = w; *c; c++) *c = (char)toupper((unsigned char)*c); else if (st->lower) for (char *c = w; *c; c++) *c = (char)tolower((unsigned char)*c);
        LI *i = li_add(lb); i->kind = 0; i->s = w; i->f = f; i->px = px; i->w = text_w(f, w, px); i->h = line_h(st); i->c = st->color; i->ul = st->ul; i->strike = st->strike;
        i->has_bg = st->has_bg; i->bg = st->bg; i->node = tn->parent; i->gap = lb->pend_space; lb->pend_space = 0;
    }
}

static void emit_line(LB *lb, int a, int b, int width_used) {
    if (a >= b) return;
    int lh = 0; for (int i = a; i < b; i++) if (lb->it[i].h > lh) lh = lb->it[i].h;
    flush_pending();
    int off = lb->align == 1 ? (lb->w - width_used) / 2 : lb->align == 2 ? lb->w - width_used : 0; if (off < 0) off = 0;
    int x = lb->x + off, sp = text_w(F_UI, " ", S(16));
    for (int i = a; i < b; i++) {
        LI *it = &lb->it[i];
        if (it->kind == 2) continue;
        if (i > a && it->gap) { sp = text_w(it->f ? it->f : F_UI, " ", it->px ? it->px : S(16)); x += sp; }
        int y = ly + (lh - it->h) / 2;
        if (it->kind == 0) {
            if (it->has_bg) { int d = di_add(DI_FILL, x, ly, it->w + (i + 1 < b && lb->it[i + 1].gap && lb->it[i + 1].has_bg ? sp : 0), lh); LP->di[d].c = it->bg; }
            int d = di_add(DI_TEXT, x, y, it->w, it->h); DI *di = &LP->di[d]; di->s = it->s; it->s = NULL; di->f = it->f; di->px = it->px; di->c = it->c; di->node = it->node;
            di->ul = it->ul; di->strike = it->strike;
            if ((it->ul || it->strike) && i + 1 < b && lb->it[i + 1].node == it->node && lb->it[i + 1].gap && lb->it[i + 1].kind == 0) di->w += text_w(it->f, " ", it->px);
        } else if (it->kind == 3 || it->kind == 4) {
            if (it->kind == 4) { int d = di_add(DI_FIELD, x, y, it->w, it->h); LP->di[d].node = it->node; }
            else di_shift(it->d0, it->d1, x, y);
        }
        x += it->w;
    }
    ly += lh;
}
static void flush_lines(LB *lb) {
    int a = 0, x = 0, used = 0, sp;
    for (int i = 0; i < lb->n; i++) {
        LI *it = &lb->it[i];
        if (it->kind == 2) { emit_line(lb, a, i + 1, used); a = i + 1; used = 0; x = 0; continue; }
        sp = (i > a && it->gap) ? text_w(it->f ? it->f : F_UI, " ", it->px ? it->px : S(16)) : 0;
        if (i > a && !lb->nowrap && x + sp + it->w > lb->w) { emit_line(lb, a, i, used); a = i; x = 0; used = 0; sp = 0; }
        x += sp + it->w; used = x;
    }
    emit_line(lb, a, lb->n, used);
    for (int i = 0; i < lb->n; i++) free(lb->it[i].s);
    lb->n = 0; lb->pend_space = 0;
}

/* ---- form controls ---- */
static const char *ctl_type(Node *e) { const char *t = wn_attr(e, "type"); return t ? t : "text"; }
static int ctl_size(Node *e, int *w, int *h) {
    const char *t = e->tag;
    if (!strcmp(t, "input")) {
        const char *ty = ctl_type(e);
        if (!strcasecmp(ty, "hidden")) return 0;
        if (!strcasecmp(ty, "checkbox") || !strcasecmp(ty, "radio")) { *w = *h = S(16); return 1; }
        if (!strcasecmp(ty, "submit") || !strcasecmp(ty, "button") || !strcasecmp(ty, "reset")) { const char *v = wn_attr(e, "value"); *w = text_w(F_UI, v ? v : "OK", S(14)) + S(28); *h = S(32); return 1; }
        const char *sz = wn_attr(e, "size"); *w = e->st.width ? S(e->st.width) : S(sz ? atoi(sz) * 8 : 200); *h = S(32); if (*w > S(600)) *w = S(600); return 1;
    }
    if (!strcmp(t, "button")) { char *x = wn_text(e); *w = text_w(F_UI, x, S(14)) + S(28); *h = S(32); free(x); return 1; }
    if (!strcmp(t, "textarea")) { const char *r = wn_attr(e, "rows"); *w = e->st.width ? S(e->st.width) : S(300); *h = S((r ? atoi(r) : 4) * 20 + 10); return 1; }
    if (!strcmp(t, "select")) { int mw = 40; for (Node *o = e->first; o; o = o->next) if (!o->type && !strcmp(o->tag, "option")) { char *x = wn_text(o); int tw = text_w(F_UI, x, S(14)); if (tw > mw) mw = tw; free(x); } *w = mw + S(40); *h = S(32); return 1; }
    if (!strcmp(t, "img")) {
        const char *al = wn_attr(e, "alt"), *wa = wn_attr(e, "width"), *ha = wn_attr(e, "height");
        if (wa && ha) { *w = S(atoi(wa)); *h = S(atoi(ha)); return *w > 0; }
        if (!al || !*al) return 0;
        *w = text_w(F_UI, al, S(13)) + S(16); *h = S(26); return 1;
    }
    return 0;
}

static void ibox(LB *lb, Node *e, int fixed_w) {
    const Style *s = &e->st;
    int cw, ch;
    if (!strcmp(e->tag, "input") || !strcmp(e->tag, "button") || !strcmp(e->tag, "textarea") || !strcmp(e->tag, "select") || !strcmp(e->tag, "img")) {
        if (!ctl_size(e, &cw, &ch)) return;
        LI *i = li_add(lb); i->kind = 4; i->w = cw + S(s->ml) + S(s->mr); i->h = ch; i->node = e; i->gap = lb->pend_space; lb->pend_space = 0; return;
    }
    if (!strcmp(e->tag, "svg") || !strcmp(e->tag, "canvas") || !strcmp(e->tag, "video") || !strcmp(e->tag, "iframe")) return;
    int save_y = ly, save_p = lpend, d0 = LP->ndi;
    int avail = lb->w; int bl = (s->bsides & 8) ? S(s->bw) : 0, br = (s->bsides & 2) ? S(s->bw) : 0;
    int extra = S(s->ml) + S(s->mr) + S(s->pl) + S(s->pr) + bl + br;
    int W;
    if (fixed_w > 0) W = fixed_w; else if (s->width) W = S(s->width) + (s->bbox ? S(s->ml) + S(s->mr) : extra); else if (s->wpct) W = avail * s->wpct / 100 + S(s->wadd); else W = avail;
    if (W > avail && fixed_w <= 0) W = avail;
    ly = 0; lpend = 0; force_tw = 1; layout_block(e, 0, W);
    if (fixed_w <= 0 && !s->width && !s->wpct) {   /* shrink-to-fit: measure the widest line and lay out again */
        int maxr = 0; for (int i = d0; i < LP->ndi; i++) { DI *d = &LP->di[i]; if (d->kind == DI_TEXT || d->kind == DI_FIELD) { int r = d->x + d->w; if (r > maxr) maxr = r; } }
        int nw = maxr + S(s->pr) + S(s->mr) + br + S(2);
        if (nw > 0 && nw < W) { di_truncate(d0); ly = 0; lpend = 0; force_tw = 1; layout_block(e, 0, nw); W = nw; }
    }
    int h = ly + lpend;
    ly = save_y; lpend = save_p;
    LI *i = li_add(lb); i->kind = 3; i->w = W; i->h = h > 0 ? h : S(4); i->d0 = d0; i->d1 = LP->ndi; i->node = e; i->gap = lb->pend_space; lb->pend_space = 0;
}

static void collect(LB *lb, Node *e) {
    for (Node *c = e->first; c; c = c->next) {
        if (c->type == 1) { add_text(lb, c, &e->st); continue; }
        const Style *s = &c->st;
        if (s->display == D_NONE) continue;
        if (!strcmp(c->tag, "br")) { LI *b = li_add(lb); b->kind = 2; b->h = line_h(&e->st); continue; }
        if (s->display == D_IBLOCK) { ibox(lb, c, 0); continue; }
        if (s->display == D_INLINE) { collect(lb, c); continue; }
        /* block inside an inline run: break the run */
        flush_lines(lb); layout_block(c, lb->x, lb->w);
    }
}

static void layout_flex(Node *n, int x, int w);
static void layout_children(Node *n, int x, int w) {
    if (n->st.sticky_dummy && n->st.display != D_ROW) { layout_flex(n, x, w); return; }
    LB lb; memset(&lb, 0, sizeof lb); lb.x = x; lb.w = w; lb.align = n->st.align; lb.nowrap = n->st.nowrap && !n->st.pre; lb.flow = n;
    int flex = n->st.sticky_dummy, row = n->st.display == D_ROW;
    if (flex && n->st.jcenter) lb.align = n->st.jcenter == 1 ? 1 : 2;
    if (row) {
        int cells = 0; for (Node *c = n->first; c; c = c->next) if (!c->type && c->st.display != D_NONE) cells++;
        int each = cells ? w / cells : w;
        for (Node *c = n->first; c; c = c->next) if (!c->type && c->st.display != D_NONE) ibox(&lb, c, each);
    } else for (Node *c = n->first; c; c = c->next) {
        if (c->type == 1) { add_text(&lb, c, &n->st); continue; }
        const Style *s = &c->st;
        if (s->display == D_NONE) continue;
        if (!strcmp(c->tag, "br")) { LI *b = li_add(&lb); b->kind = 2; b->h = line_h(&n->st); continue; }
        if (s->display == D_IBLOCK || (flex && !n->st.col && s->display != D_INLINE)) { ibox(&lb, c, 0); continue; }
        if (s->display == D_INLINE) { collect(&lb, c); continue; }
        flush_lines(&lb); layout_block(c, x, w);
    }
    flush_lines(&lb); free(lb.it);
}

/* ---- flexbox and grid ---- */
static int item_natural(Node *e, int avail) {
    const Style *s = &e->st; int cw, ch;
    int bl = (s->bsides & 8) ? S(s->bw) : 0, br = (s->bsides & 2) ? S(s->bw) : 0, ex = S(s->pl) + S(s->pr) + bl + br, mg = S(s->ml) + S(s->mr);
    int nat;
    if (!strcmp(e->tag, "input") || !strcmp(e->tag, "button") || !strcmp(e->tag, "textarea") || !strcmp(e->tag, "select") || !strcmp(e->tag, "img")) { if (!ctl_size(e, &cw, &ch)) return 0; nat = cw + mg; }
    else if (s->fbasis > 0) nat = S(s->fbasis) + (s->bbox ? 0 : ex) + mg;
    else if (s->width) nat = S(s->width) + (s->bbox ? 0 : ex) + mg;
    else if (s->wpct) nat = avail * s->wpct / 100 + S(s->wadd);
    else {
        int sy = ly, sp = lpend, d0 = LP->ndi; ly = 0; lpend = 0; force_tw = 1; layout_block(e, 0, avail);
        int maxr = 0; for (int i = d0; i < LP->ndi; i++) { DI *d = &LP->di[i]; if (d->kind == DI_TEXT || d->kind == DI_FIELD) { int r = d->x + d->w; if (r > maxr) maxr = r; } }
        di_truncate(d0); ly = sy; lpend = sp; nat = maxr + S(s->pr) + br + mg + S(2);
        if (nat < ex + mg) nat = ex + mg;
    }
    if (s->maxw && nat > S(s->maxw) + (s->bbox ? 0 : ex) + mg) nat = S(s->maxw) + (s->bbox ? 0 : ex) + mg;
    if (s->minw && nat < S(s->minw) + (s->bbox ? 0 : ex) + mg) nat = S(s->minw) + (s->bbox ? 0 : ex) + mg;
    return nat > avail ? avail : nat;
}
static int item_height(Node *e, int xi, int wi) {         /* trial layout: height only */
    int sy = ly, sp = lpend, d0 = LP->ndi; ly = 0; lpend = 0; force_tw = 1; layout_block(e, xi, wi); int h = ly + lpend; di_truncate(d0); ly = sy; lpend = sp; return h;
}
static void place_item(Node *e, int xi, int yi, int wi, int hforce) {
    int sy = ly, sp = lpend; ly = yi; lpend = 0; force_tw = 1; force_h = hforce; layout_block(e, xi, wi); ly = sy; lpend = sp;
}
static void layout_flex(Node *n, int x, int w) {
    const Style *fs = &n->st; int col = fs->col && !fs->isgrid, cg = S(fs->cgap), rg = S(fs->rgap);
    Node *items[96]; int ni = 0;
    LB tl; memset(&tl, 0, sizeof tl); tl.x = x; tl.w = w; tl.align = fs->align;                 /* stray text inside the container */
    for (Node *c = n->first; c && ni < 96; c = c->next) {
        if (c->type == 1) { add_text(&tl, c, &n->st); continue; }
        if (c->st.display == D_NONE || !strcmp(c->tag, "br")) continue;
        if (c->st.display == D_INLINE && !strcmp(c->tag, "span") && c->gen == 0 && 0) continue;
        items[ni++] = c;
    }
    flush_lines(&tl); free(tl.it);
    if (!ni) return;
    flush_pending();
    if (col) {
        for (int i = 0; i < ni; i++) {
            Node *e = items[i]; int wi = w, xi = x;
            if (fs->aitems) { int nat = item_natural(e, w); if (nat > 0 && nat < w) { wi = nat; xi = fs->aitems == 2 ? x + (w - nat) / 2 : fs->aitems == 3 ? x + w - nat : x; } }
            if (e->st.automl && e->st.automr) { int nat = item_natural(e, w); if (nat > 0 && nat < w) { wi = nat; xi = x + (w - nat) / 2; } }
            place_item(e, xi, ly, wi, 0); flush_pending(); if (i + 1 < ni) ly += rg;
        }
        return;
    }
    int widths[96], cols[16], ncols = 0;
    if (fs->isgrid) {
        int ng = fs->ngtrack ? fs->ngtrack : 1; int tr[16];
        if (fs->gautofill) { int per = S(fs->gmin) + cg; int cnt = per > 0 ? (w + cg) / per : 1; if (cnt < 1) cnt = 1; if (cnt > 12) cnt = 12; for (int i = 0; i < cnt; i++) tr[i] = fs->gtrack[0]; ng = cnt; }
        else for (int i = 0; i < ng; i++) tr[i] = fs->gtrack[i];
        int fixed = 0; int frs = 0; for (int i = 0; i < ng; i++) { if (tr[i] > 0) fixed += S(tr[i]); else frs += -tr[i] ? -tr[i] : 10; }
        int free_ = w - fixed - cg * (ng - 1); if (free_ < 0) free_ = 0;
        for (int i = 0; i < ng; i++) cols[i] = tr[i] > 0 ? S(tr[i]) : (frs ? free_ * (-tr[i] ? -tr[i] : 10) / frs : 0);
        ncols = ng;
        for (int i = 0; i < ni; i += ncols) {
            int rowh = 0, hs[16];
            for (int k = 0; k < ncols && i + k < ni; k++) { hs[k] = item_height(items[i + k], x, cols[k]); if (hs[k] > rowh) rowh = hs[k]; }
            int xi = x;
            for (int k = 0; k < ncols && i + k < ni; k++) { place_item(items[i + k], xi, ly, cols[k], fs->aitems == 0 || fs->aitems == 4 ? rowh : 0); xi += cols[k] + cg; }
            ly += rowh; if (i + ncols < ni) ly += rg;
        }
        return;
    }
    /* row flex: natural widths, line breaking, grow / shrink, justify */
    for (int i = 0; i < ni; i++) { widths[i] = item_natural(items[i], w); if (widths[i] <= 0) widths[i] = 1; }
    int a = 0;
    while (a < ni) {
        int b = a, sum = 0;
        while (b < ni) { int need = widths[b] + (b > a ? cg : 0); if (fs->flexwrap && b > a && sum + need > w) break; sum += need; b++; }
        int cnt = b - a, free_ = w - sum, grow = 0, autos = 0;
        for (int i = a; i < b; i++) { grow += items[i]->st.fgrow; if (items[i]->st.automl) autos++; }
        if (free_ < 0 && !fs->flexwrap) { int total = 0; for (int i = a; i < b; i++) total += widths[i]; int over = -free_; for (int i = a; i < b; i++) { widths[i] -= over * widths[i] / (total ? total : 1); if (widths[i] < S(24)) widths[i] = S(24); } free_ = 0; }
        if (free_ > 0 && grow > 0) { int left = free_; for (int i = a; i < b; i++) { int g = free_ * items[i]->st.fgrow / grow; widths[i] += g; left -= g; } widths[b - 1] += left > 0 ? 0 : 0; free_ = 0; }
        int off = 0, between = 0, per_auto = autos && free_ > 0 ? free_ / autos : 0; if (autos && free_ > 0) free_ = 0;
        if (free_ > 0) { if (fs->jcenter == 1) off = free_ / 2; else if (fs->jcenter == 2) off = free_; else if (fs->jcenter == 3 && cnt > 1) between = free_ / (cnt - 1); else if (fs->jcenter == 4) { between = free_ / cnt; off = between / 2; } else if (fs->jcenter == 5) { between = free_ / (cnt + 1); off = between; } }
        int lineh = 0, hs[96]; for (int i = a; i < b; i++) { hs[i] = item_height(items[i], x, widths[i]); if (hs[i] > lineh) lineh = hs[i]; }
        int xi = x + off;
        for (int i = a; i < b; i++) {
            if (items[i]->st.automl) xi += per_auto;
            int yo = fs->aitems == 2 ? (lineh - hs[i]) / 2 : fs->aitems == 3 ? lineh - hs[i] : 0;
            place_item(items[i], xi, ly + yo, widths[i], fs->aitems == 0 ? lineh : 0);
            xi += widths[i] + cg + between;
        }
        ly += lineh; a = b; if (a < ni) ly += rg;
    }
}

static void layout_block(Node *n, int x, int w) {
    const Style *s = &n->st;
    int ml = S(s->ml), mr = S(s->mr), pl = S(s->pl), pr = S(s->pr), pt = S(s->pt), pb = S(s->pb);
    int bwid = s->bsides ? S(s->bw) : 0; if (s->bsides && !bwid) bwid = 1;
    int bt = (s->bsides & 1) ? bwid : 0, br = (s->bsides & 2) ? bwid : 0, bb = (s->bsides & 4) ? bwid : 0, bl = (s->bsides & 8) ? bwid : 0;
    int avail = w - ml - mr, tw = avail; int ftw = force_tw, fh = force_h; force_tw = 0; force_h = 0;
    int ex = s->bbox ? 0 : pl + pr + bl + br;
    if (!ftw) {
        if (s->width) tw = S(s->width) + ex; else if (s->wpct) tw = avail * s->wpct / 100 + S(s->wadd) + (s->wadd || s->bbox ? 0 : 0);
        if (s->maxw && tw > S(s->maxw) + ex) tw = S(s->maxw) + ex;
        if (s->minw && tw < S(s->minw) + ex) tw = S(s->minw) + ex;
        if (tw > avail && !s->width) tw = avail;
    }
    if (tw < 8) tw = 8;
    if (s->automl && s->automr && tw < avail) { ml = (avail - tw) / 2; }
    else if (s->automl && tw < avail) ml = avail - tw - mr;
    int x0 = x + ml, mt = S(s->mt), mb = S(s->mb);
    if (mt > lpend) lpend = mt;
    int box = s->has_bg || s->bsides, top = 0, bi = -1;
    if (box || pt || bt) flush_pending();
    top = ly;
    if (box) { bi = di_add(DI_BOX, x0, top, tw, 0); DI *d = &LP->di[bi]; d->c = s->bg; d->c2 = s->bc; d->px = bwid; d->ul = s->bsides; d->strike = s->has_bg; d->radius = S(s->radius); d->node = n; }
    ly += pt + bt;
    int cx = x0 + bl + pl, cw = tw - bl - br - pl - pr; if (cw < 4) cw = 4;
    if (s->display == D_LISTITEM && !s->nolist) {
        int idx = 1; for (Node *p = n->prev; p; p = p->prev) if (!p->type && p->st.display == D_LISTITEM) idx++;
        char mk[16]; if (n->parent && !strcmp(n->parent->tag, "ol")) snprintf(mk, sizeof mk, "%d.", idx); else snprintf(mk, sizeof mk, "\xe2\x80\xa2");
        flush_pending();
        int mw2 = text_w(F_UI, mk, S(s->fs)); int d = di_add(DI_TEXT, cx - mw2 - S(8), ly + (line_h(s) - S(s->fs) * 135 / 100) / 2, mw2, line_h(s)); LP->di[d].s = strdup(mk); LP->di[d].f = F_UI; LP->di[d].px = S(s->fs); LP->di[d].c = s->color; LP->di[d].node = n;
    }
    int content_top = ly;
    layout_children(n, cx, cw);
    if (s->height && ly - content_top < S(s->height) - (s->bbox ? pt + pb + bt + bb : 0)) { flush_pending(); ly = content_top + S(s->height) - (s->bbox ? pt + pb + bt + bb : 0); }
    if (box || pb || bb) flush_pending();
    ly += pb + bb;
    if (fh > ly - top) { ly = top + fh; }
    if (bi >= 0) LP->di[bi].h = ly - top;
    if (mb > lpend) lpend = mb;
}

void lay_page(Page *p, int width) {
    LP = p; for (int i = 0; i < p->ndi; i++) free(p->di[i].s); p->ndi = 0; ly = 0; lpend = 0; p->layout_w = width;
    layout_block(p->doc, 0, width);
    flush_pending(); p->height = ly + S(24);
}

/* ---- painting ---- */
static void paint_field(Page *p, DI *d, int ox, int oy) {
    Node *e = d->node; int x = ox + d->x, y = oy + d->y, w = d->w, h = d->h; const char *t = e->tag;
    int focus = p->focus == e;
    if (!strcmp(t, "img")) {
        const char *al = wn_attr(e, "alt"); gfx_rrect(x, y, w, h, S(4), RGB(238, 238, 240), 255); gfx_rrect_line(x, y, w, h, S(4), RGB(200, 200, 205), 255);
        if (al) text_draw_clip(F_UI, x + S(8), y + (h - font_line_h(F_UI, S(13))) / 2, al, RGB(110, 110, 120), S(13), w - S(12)); return;
    }
    if (!strcmp(t, "input") && (!strcasecmp(ctl_type(e), "checkbox") || !strcasecmp(ctl_type(e), "radio"))) {
        int radio = !strcasecmp(ctl_type(e), "radio"), on = wn_attr(e, "checked") != NULL;
        if (radio) { gfx_circle(x + w / 2, y + h / 2, w / 2, on ? RGB(0, 102, 204) : RGB(150, 150, 158), 255); gfx_circle(x + w / 2, y + h / 2, w / 2 - S(2), RGB(255, 255, 255), 255); if (on) gfx_circle(x + w / 2, y + h / 2, w / 4, RGB(0, 102, 204), 255); }
        else { gfx_rrect(x, y, w, h, S(3), on ? RGB(0, 102, 204) : RGB(255, 255, 255), 255); gfx_rrect_line(x, y, w, h, S(3), on ? RGB(0, 102, 204) : RGB(150, 150, 158), 255); if (on) { gfx_line((float)(x + w / 5), (float)(y + h / 2), (float)(x + w * 2 / 5), (float)(y + h * 7 / 10), (float)S(2), RGB(255, 255, 255), 255); gfx_line((float)(x + w * 2 / 5), (float)(y + h * 7 / 10), (float)(x + w * 4 / 5), (float)(y + h * 3 / 10), (float)S(2), RGB(255, 255, 255), 255); } }
        return;
    }
    int is_btn = !strcmp(t, "button") || (!strcmp(t, "input") && (!strcasecmp(ctl_type(e), "submit") || !strcasecmp(ctl_type(e), "button") || !strcasecmp(ctl_type(e), "reset")));
    if (is_btn) {
        char *lab; if (!strcmp(t, "button")) lab = wn_text(e); else { const char *v = wn_attr(e, "value"); lab = strdup(v ? v : "OK"); }
        u32 bg = e->st.has_bg ? e->st.bg : RGB(235, 235, 238);
        gfx_rrect(x, y, w, h, S(e->st.radius ? e->st.radius : 5), bg, 255); gfx_rrect_line(x, y, w, h, S(e->st.radius ? e->st.radius : 5), focus ? RGB(0, 102, 204) : RGB(170, 170, 178), 255);
        int tw = text_w(F_UI, lab, S(14)); text_draw(F_UI, x + (w - tw) / 2, y + (h - font_line_h(F_UI, S(14))) / 2, lab, e->st.has_bg && e->st.color ? e->st.color : RGB(30, 30, 34), S(14)); free(lab); return;
    }
    gfx_rrect(x, y, w, h, S(4), RGB(255, 255, 255), 255); gfx_rrect_line(x, y, w, h, S(4), focus ? RGB(0, 102, 204) : RGB(170, 170, 178), 255);
    if (focus) gfx_rrect_line(x - 1, y - 1, w + 2, h + 2, S(5), RGB(120, 170, 235), 255);
    char *val = NULL;
    if (!strcmp(t, "textarea")) val = wn_text(e); else if (!strcmp(t, "select")) { Node *sel = NULL; for (Node *o = e->first; o; o = o->next) if (!o->type && !strcmp(o->tag, "option")) { if (!sel || wn_attr(o, "selected")) sel = o; if (wn_attr(o, "selected")) break; } val = sel ? wn_text(sel) : strdup(""); }
    else { const char *v = wn_attr(e, "value"); val = strdup(v ? v : ""); }
    int pw = !strcmp(t, "input") && !strcasecmp(ctl_type(e), "password");
    char shown[300]; if (pw) { size_t n = strlen(val); if (n > 60) n = 60; for (size_t i = 0; i < n; i++) shown[i] = '*'; shown[n] = 0; } else snprintf(shown, sizeof shown, "%s", val);
    gfx_clip(x + S(4), y, w - S(8), h);
    if (!shown[0]) { const char *ph = wn_attr(e, "placeholder"); if (ph) text_draw(F_UI, x + S(8), y + (h - font_line_h(F_UI, S(14))) / 2, ph, RGB(150, 150, 158), S(14)); }
    else text_draw(F_UI, x + S(8), y + (!strcmp(t, "textarea") ? S(6) : (h - font_line_h(F_UI, S(14))) / 2), shown, RGB(25, 25, 30), S(14));
    if (focus && strcmp(t, "select") && ((now_ms() / 500) & 1)) { int cx = x + S(8) + text_w(F_UI, shown, S(14)); gfx_vline(cx, y + S(6), h - S(12), RGB(25, 25, 30), 255); want_blink = 1; }
    if (!strcmp(t, "select")) text_draw(F_UI, x + w - S(22), y + (h - font_line_h(F_UI, S(14))) / 2, "\xe2\x96\xbe", RGB(90, 90, 100), S(14));
    gfx_unclip(); free(val);
}
void lay_paint(Page *p, int x, int y, int w, int h, int scroll) {
    LP = p; int oy = y - scroll;
    for (int i = 0; i < p->ndi; i++) {
        DI *d = &p->di[i]; int dy = oy + d->y;
        if (dy > y + h || dy + (d->h > 0 ? d->h : 0) < y) continue;
        switch (d->kind) {
        case DI_FILL: gfx_fill(x + d->x, dy, d->w, d->h, d->c, 255); break;
        case DI_BOX:
            if (d->strike) { if (d->radius) gfx_rrect(x + d->x, dy, d->w, d->h, d->radius, d->c, 255); else gfx_fill(x + d->x, dy, d->w, d->h, d->c, 255); }
            if (d->ul) {
                if (d->ul == 15 && d->radius) gfx_rrect_line(x + d->x, dy, d->w, d->h, d->radius, d->c2, 255);
                else { int bw = d->px; if (d->ul & 1) gfx_fill(x + d->x, dy, d->w, bw, d->c2, 255); if (d->ul & 4) gfx_fill(x + d->x, dy + d->h - bw, d->w, bw, d->c2, 255); if (d->ul & 8) gfx_fill(x + d->x, dy, bw, d->h, d->c2, 255); if (d->ul & 2) gfx_fill(x + d->x + d->w - bw, dy, bw, d->h, d->c2, 255); }
            }
            break;
        case DI_TEXT:
            if (d->s) { text_draw(d->f, x + d->x, dy, d->s, d->c, d->px); if (d->ul) gfx_hline(x + d->x, dy + d->px * 11 / 10, d->w, d->c, 230); if (d->strike) gfx_hline(x + d->x, dy + d->px * 6 / 10, d->w, d->c, 230); }
            break;
        case DI_FIELD: paint_field(p, d, x, oy); break;
        }
    }
}
Node *lay_hit(Page *p, int px, int py) {
    for (int i = p->ndi - 1; i >= 0; i--) { DI *d = &p->di[i]; if (!d->node) continue; if (px >= d->x && px < d->x + d->w && py >= d->y && py < d->y + d->h) { if (d->kind == DI_FILL) continue; return d->node; } }
    return NULL;
}
