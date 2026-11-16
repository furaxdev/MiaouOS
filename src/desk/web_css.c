/* Miaou Web: CSS parser, selector matching and cascade (a pragmatic subset of CSS 2.1 + a little flexbox). */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web.h"

typedef struct { char tag[20], id[40], cls[5][32], ak[2][24], av[2][40], aop[2], notx[40]; int ncls, na, comb, pseudo, never; } Cmp;
typedef struct { Cmp c[8]; int n; } Sel;
enum { P_FIRST = 1, P_LAST = 2, P_ONLY = 4, P_ROOT = 8, P_LINK = 16, P_EMPTY = 32 };

static int idch(int c) { return isalnum(c) || c == '-' || c == '_' || (unsigned char)c >= 0x80; }
static const char *ident(const char *s, char *out, size_t n) { size_t k = 0; while (*s && idch((unsigned char)*s)) { if (k + 1 < n) out[k++] = *s; s++; } out[k] = 0; return s; }

static int parse_sel(const char *s, Sel *o) {
    o->n = 0; int comb = ' ';
    while (*s) {
        while (isspace((unsigned char)*s)) s++;
        if (*s == '>' || *s == '+' || *s == '~') { comb = *s++; continue; }
        if (!*s) break;
        Cmp *c = &o->c[o->n]; memset(c, 0, sizeof *c); c->comb = comb; comb = ' ';
        while (*s && !isspace((unsigned char)*s) && *s != '>' && *s != '+' && *s != '~') {
            if (*s == '*') s++;
            else if (*s == '#') s = ident(s + 1, c->id, sizeof c->id);
            else if (*s == '.') { if (c->ncls < 5) s = ident(s + 1, c->cls[c->ncls++], 32); else s = ident(s + 1, c->notx, 2); }
            else if (*s == '[') {
                const char *e = strchr(s, ']'); if (!e) return 0;
                if (c->na < 2) {
                    const char *q = s + 1; char *k = c->ak[c->na]; size_t kl = 0; while (q < e && idch((unsigned char)*q) && kl < 23) k[kl++] = (char)tolower((unsigned char)*q++); k[kl] = 0;
                    if (q < e) { char op = *q; if (op == '=') { c->aop[c->na] = '='; q++; } else if (q[1] == '=') { c->aop[c->na] = op; q += 2; } else q = e;
                        if (q < e) { if (*q == '"' || *q == '\'') q++; size_t vl = 0; char *v = c->av[c->na]; while (q < e && *q != '"' && *q != '\'' && vl < 39) v[vl++] = *q++; v[vl] = 0; } }
                    else c->aop[c->na] = 'e';
                    c->na++;
                }
                s = e + 1;
            }
            else if (*s == ':') {
                if (s[1] == ':') return 0;                                   /* pseudo-elements: not rendered */
                char nm[24]; s = ident(s + 1, nm, sizeof nm);
                if (!strcmp(nm, "first-child")) c->pseudo |= P_FIRST; else if (!strcmp(nm, "last-child")) c->pseudo |= P_LAST;
                else if (!strcmp(nm, "only-child")) c->pseudo |= P_ONLY; else if (!strcmp(nm, "root")) c->pseudo |= P_ROOT;
                else if (!strcmp(nm, "link")) c->pseudo |= P_LINK; else if (!strcmp(nm, "empty")) c->pseudo |= P_EMPTY;
                else if (!strcmp(nm, "not") && *s == '(') { const char *e = strchr(s, ')'); if (!e) return 0; size_t l = (size_t)(e - s - 1); if (l > 39) l = 39; memcpy(c->notx, s + 1, l); c->notx[l] = 0; s = e; }
                else c->never = 1;
                if (*s == '(') { int d = 0; while (*s) { if (*s == '(') d++; if (*s == ')' && --d == 0) { s++; break; } s++; } if (*s == ')') s++; }
                else if (*s == ')') s++;
            }
            else if (idch((unsigned char)*s)) s = ident(s, c->tag, sizeof c->tag);
            else return 0;
        }
        for (char *t = c->tag; *t; t++) *t = (char)tolower((unsigned char)*t);
        if (++o->n >= 8) return 0;
    }
    return o->n > 0;
}
static int spec_of(const Sel *s) { int v = 0; for (int i = 0; i < s->n; i++) { const Cmp *c = &s->c[i]; v += (c->id[0] ? 10000 : 0) + (c->ncls + c->na + (c->pseudo ? 1 : 0)) * 100 + (c->tag[0] ? 1 : 0); } return v; }

static int has_class(const Node *n, const char *cl) {
    const char *a = wn_attr(n, "class"); if (!a) return 0; size_t l = strlen(cl);
    for (const char *p = a; *p; ) { while (isspace((unsigned char)*p)) p++; const char *s = p; while (*p && !isspace((unsigned char)*p)) p++; if ((size_t)(p - s) == l && !strncmp(s, cl, l)) return 1; }
    return 0;
}
static int elem_index(const Node *n, int *last) {
    int idx = 0, after = 0; for (const Node *c = n->parent ? n->parent->first : NULL; c; c = c->next) { if (c->type) continue; if (c == n) idx = 1; else if (idx) after = 1; }
    *last = !after; int first = 1; for (const Node *c = n->prev; c; c = c->prev) if (!c->type) { first = 0; break; } return first;
}
static int match_cmp(const Node *n, const Cmp *c) {
    if (c->never || n->type) return 0;
    if (c->tag[0] && strcmp(c->tag, n->tag)) return 0;
    if (c->id[0]) { const char *i = wn_attr(n, "id"); if (!i || strcmp(i, c->id)) return 0; }
    for (int i = 0; i < c->ncls; i++) if (!has_class(n, c->cls[i])) return 0;
    for (int i = 0; i < c->na; i++) {
        const char *v = wn_attr(n, c->ak[i]); if (!v) return 0;
        const char *w = c->av[i]; size_t wl = strlen(w), vl = strlen(v);
        switch (c->aop[i]) {
        case '=': if (strcmp(v, w)) return 0; break;
        case '^': if (strncmp(v, w, wl)) return 0; break;
        case '$': if (vl < wl || strcmp(v + vl - wl, w)) return 0; break;
        case '*': if (!strstr(v, w)) return 0; break;
        case '~': if (!has_class(n, w) && strcmp(v, w)) return 0; break;
        case '|': if (strncmp(v, w, wl)) return 0; break;
        default: break;
        }
    }
    if (c->pseudo) {
        int last, first = elem_index(n, &last);
        if ((c->pseudo & P_FIRST) && !first) return 0;
        if ((c->pseudo & P_LAST) && !last) return 0;
        if ((c->pseudo & P_ONLY) && !(first && last)) return 0;
        if ((c->pseudo & P_ROOT) && strcmp(n->tag, "html")) return 0;
        if ((c->pseudo & P_LINK) && (strcmp(n->tag, "a") || !wn_attr(n, "href"))) return 0;
        if ((c->pseudo & P_EMPTY) && n->first) return 0;
    }
    if (c->notx[0]) {
        const char *x = c->notx;
        if (x[0] == '.' && has_class(n, x + 1)) return 0;
        if (x[0] == '#') { const char *i = wn_attr(n, "id"); if (i && !strcmp(i, x + 1)) return 0; }
        if (isalpha((unsigned char)x[0]) && !strcmp(x, n->tag)) return 0;
    }
    return 1;
}
static int match_sel(const Node *n, const Sel *s, int i) {
    if (!match_cmp(n, &s->c[i])) return 0;
    if (i == 0) return 1;
    switch (s->c[i].comb) {
    case '>': return n->parent && match_sel(n->parent, s, i - 1);
    case '+': { const Node *p = n->prev; while (p && p->type) p = p->prev; return p && match_sel(p, s, i - 1); }
    case '~': for (const Node *p = n->prev; p; p = p->prev) if (!p->type && match_sel(p, s, i - 1)) return 1; return 0;
    default: for (const Node *a = n->parent; a; a = a->parent) if (match_sel(a, s, i - 1)) return 1; return 0;
    }
}

static void query_rec(Node *n, const Sel *s, Node **out, int *cnt, int max, int first_only) {
    for (Node *c = n->first; c && *cnt < max; c = c->next) {
        if (!c->type && match_sel(c, s, s->n - 1)) { out[(*cnt)++] = c; if (first_only) return; }
        if (!c->type) { query_rec(c, s, out, cnt, max, first_only); if (first_only && *cnt) return; }
    }
}
int css_query_all(Node *root, const char *selector, Node **out, int max) {
    int cnt = 0; char *copy = strdup(selector); char *save = NULL;
    for (char *part = strtok_r(copy, ",", &save); part; part = strtok_r(NULL, ",", &save)) { Sel s; if (parse_sel(part, &s)) query_rec(root, &s, out, &cnt, max, 0); }
    free(copy); return cnt;
}
Node *css_query_first(Node *root, const char *selector) { Node *r[1]; return css_query_all(root, selector, r, 1) ? r[0] : NULL; }

/* ---- values ---- */
u32 css_color(const char *s, int *ok) {
    static const struct { const char *n; u32 c; } N[] = { { "black", 0 }, { "white", 0xffffff }, { "red", 0xff0000 }, { "green", 0x008000 }, { "blue", 0x0000ff }, { "yellow", 0xffff00 }, { "orange", 0xffa500 },
        { "purple", 0x800080 }, { "gray", 0x808080 }, { "grey", 0x808080 }, { "silver", 0xc0c0c0 }, { "maroon", 0x800000 }, { "navy", 0x000080 }, { "teal", 0x008080 }, { "lime", 0x00ff00 },
        { "aqua", 0x00ffff }, { "cyan", 0x00ffff }, { "fuchsia", 0xff00ff }, { "magenta", 0xff00ff }, { "pink", 0xffc0cb }, { "brown", 0xa52a2a }, { "gold", 0xffd700 }, { "indigo", 0x4b0082 },
        { "violet", 0xee82ee }, { "coral", 0xff7f50 }, { "salmon", 0xfa8072 }, { "khaki", 0xf0e68c }, { "beige", 0xf5f5dc }, { "ivory", 0xfffff0 }, { "whitesmoke", 0xf5f5f5 }, { "gainsboro", 0xdcdcdc },
        { "lightgray", 0xd3d3d3 }, { "lightgrey", 0xd3d3d3 }, { "darkgray", 0xa9a9a9 }, { "darkgrey", 0xa9a9a9 }, { "dimgray", 0x696969 }, { "lightblue", 0xadd8e6 }, { "skyblue", 0x87ceeb },
        { "darkblue", 0x00008b }, { "darkgreen", 0x006400 }, { "darkred", 0x8b0000 }, { "lightgreen", 0x90ee90 }, { "tomato", 0xff6347 }, { "crimson", 0xdc143c }, { "orangered", 0xff4500 },
        { "steelblue", 0x4682b4 }, { "royalblue", 0x4169e1 }, { "slategray", 0x708090 }, { "chocolate", 0xd2691e }, { "tan", 0xd2b48c }, { "plum", 0xdda0dd }, { "turquoise", 0x40e0d0 } };
    while (isspace((unsigned char)*s)) s++;
    *ok = 1;
    if (*s == '#') {
        char h[10]; size_t k = 0; s++; while (isxdigit((unsigned char)s[k]) && k < 8) { h[k] = s[k]; k++; } h[k] = 0;
        unsigned v = (unsigned)strtoul(h, NULL, 16);
        if (k == 3) return RGB(((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17);
        if (k == 6) return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
        if (k == 8) return RGB((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255);
        *ok = 0; return 0;
    }
    if (!strncasecmp(s, "rgb", 3)) {
        const char *p = strchr(s, '('); if (!p) { *ok = 0; return 0; }
        double v[4] = { 0, 0, 0, 1 }; int n = 0; p++;
        while (*p && *p != ')' && n < 4) { while (*p == ' ' || *p == ',' || *p == '/') p++; char *e; v[n] = strtod(p, &e); if (*e == '%') { v[n] = v[n] * 2.55; e++; } p = e; n++; }
        if (n >= 4 && v[3] < 0.35) { *ok = 2; return 0; }
        return RGB((int)v[0] & 255, (int)v[1] & 255, (int)v[2] & 255);
    }
    if (!strncasecmp(s, "hsl", 3)) {
        const char *p = strchr(s, '('); if (!p) { *ok = 0; return 0; }
        double h = strtod(p + 1, (char **)&p), sat = 0, l = 0; while (*p == ' ' || *p == ',' || *p == '%') p++; sat = strtod(p, (char **)&p) / 100; while (*p == ' ' || *p == ',' || *p == '%') p++; l = strtod(p, NULL) / 100;
        double c = (1 - (2 * l - 1 < 0 ? 1 - 2 * l : 2 * l - 1)) * sat, hh = h / 60; while (hh >= 6) hh -= 6; double x = c * (1 - (hh - 2 * (int)(hh / 2) - 1 < 0 ? 1 - (hh - 2 * (int)(hh / 2)) : hh - 2 * (int)(hh / 2) - 1)), m = l - c / 2, r = 0, g = 0, b = 0;
        if (hh < 1) { r = c; g = x; } else if (hh < 2) { r = x; g = c; } else if (hh < 3) { g = c; b = x; } else if (hh < 4) { g = x; b = c; } else if (hh < 5) { r = x; b = c; } else { r = c; b = x; }
        return RGB((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
    }
    if (!strncasecmp(s, "transparent", 11)) { *ok = 2; return 0; }
    for (size_t i = 0; i < sizeof N / sizeof N[0]; i++) { size_t l = strlen(N[i].n); if (!strncasecmp(s, N[i].n, l) && !idch((unsigned char)s[l])) return RGB((N[i].c >> 16) & 255, (N[i].c >> 8) & 255, N[i].c & 255); }
    *ok = 0; return 0;
}
/* returns 1 and sets *px (css px) when s starts with a length; *pct set when it is a percentage */
static int length(const char *s, int fs, int *px, int *pct) {
    while (isspace((unsigned char)*s)) s++;
    char *e; double v = strtod(s, &e); if (e == s) return 0;
    *pct = 0;
    if (!strncasecmp(e, "px", 2)) *px = (int)(v + (v < 0 ? -0.5 : 0.5));
    else if (!strncasecmp(e, "rem", 3)) *px = (int)(v * 16 + 0.5);
    else if (!strncasecmp(e, "em", 2)) *px = (int)(v * fs + 0.5);
    else if (!strncasecmp(e, "pt", 2)) *px = (int)(v * 4 / 3 + 0.5);
    else if (*e == '%') { *pct = (int)v; *px = 0; }
    else if (!strncasecmp(e, "vw", 2)) *px = (int)(v * 10 + 0.5);
    else if (!strncasecmp(e, "vh", 2)) *px = (int)(v * 6 + 0.5);
    else if (!strncasecmp(e, "ch", 2) || !strncasecmp(e, "ex", 2)) *px = (int)(v * fs / 2 + 0.5);
    else if (v == 0 || !isalpha((unsigned char)*e)) *px = (int)(v + 0.5);
    else return 0;
    return 1;
}
static void box4(const char *val, int fs, short *t, short *r, short *b, short *l, unsigned char *autol, unsigned char *autor) {
    int v[4], n = 0, px, pct; const char *p = val; int aut[4] = { 0, 0, 0, 0 };
    while (*p && n < 4) { while (isspace((unsigned char)*p)) p++; if (!*p) break; if (!strncmp(p, "auto", 4)) { v[n] = 0; aut[n++] = 1; p += 4; continue; } if (length(p, fs, &px, &pct)) v[n++] = pct ? 0 : px; else v[n++] = 0; while (*p && !isspace((unsigned char)*p)) p++; }
    if (!n) return;
    int T = v[0], R = n > 1 ? v[1] : v[0], B = n > 2 ? v[2] : v[0], L = n > 3 ? v[3] : (n > 1 ? v[1] : v[0]);
    int aR = n > 1 ? aut[1] : aut[0], aL = n > 3 ? aut[3] : (n > 1 ? aut[1] : aut[0]);
    if (t) *t = (short)T; if (r) *r = (short)R; if (b) *b = (short)B; if (l) *l = (short)L;
    if (autol) *autol = (unsigned char)aL; if (autor) *autor = (unsigned char)aR;
}

typedef struct { Page *p; } Ctx;
static const char *var_lookup(Page *p, const char *name, size_t n) { for (int i = 0; i < p->nvars; i++) if (strlen(p->vk[i]) == n && !strncmp(p->vk[i], name, n)) return p->vv[i]; return NULL; }
static char *subst_vars(Page *p, const char *v) {
    if (!strstr(v, "var(")) return strdup(v);
    char *o = malloc(strlen(v) * 4 + 64); size_t k = 0;
    for (const char *s = v; *s; ) {
        if (!strncmp(s, "var(", 4)) {
            const char *a = s + 4; const char *e = a; int d = 1; while (*e && d) { if (*e == '(') d++; if (*e == ')') d--; if (d) e++; }
            const char *comma = a; while (comma < e && *comma != ',') comma++;
            const char *val = var_lookup(p, a, (size_t)(comma - a));
            if (!val && comma < e) { val = comma + 1; while (isspace((unsigned char)*val)) val++; size_t l = (size_t)(e - val); memcpy(o + k, val, l); k += l; }
            else if (val) { size_t l = strlen(val); memcpy(o + k, val, l); k += l; }
            s = *e ? e + 1 : e;
        } else o[k++] = *s++;
    }
    o[k] = 0; return o;
}

static void apply_decl(Page *pg, Node *n, Style *s, const Style *par, const char *prop, const char *rawval) {
    char *val = subst_vars(pg, rawval); while (isspace((unsigned char)*val)) memmove(val, val + 1, strlen(val));
    int px, pct, ok; int pfs = par ? par->fs : 16;
    #define IS(x) (!strcmp(prop, x))
    if (IS("color")) { u32 c = css_color(val, &ok); if (ok == 1) s->color = c; }
    else if (IS("background-color") || IS("background")) {
        int found = 0; for (const char *w = val; *w && !found; ) { while (isspace((unsigned char)*w)) w++; u32 c = css_color(w, &ok); if (ok == 1) { s->bg = c; s->has_bg = 1; found = 1; } else if (ok == 2 && IS("background-color")) { s->has_bg = 0; found = 1; }
            if (*w == '(') w++; int d = 0; while (*w && (!isspace((unsigned char)*w) || d)) { if (*w == '(') d++; if (*w == ')') d--; w++; } }
        if (!found && (IS("background") && (!strncmp(val, "none", 4) || !strncmp(val, "transparent", 11)))) s->has_bg = 0;
    }
    else if (IS("font-size")) {
        static const struct { const char *n; int v; } K[] = { { "xx-small", 9 }, { "x-small", 10 }, { "small", 13 }, { "medium", 16 }, { "large", 18 }, { "x-large", 24 }, { "xx-large", 32 } };
        int done = 0; for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) if (!strcmp(val, K[i].n)) { s->fs = (short)K[i].v; done = 1; }
        if (!done && !strcmp(val, "smaller")) { s->fs = (short)(pfs * 5 / 6); done = 1; } if (!done && !strcmp(val, "larger")) { s->fs = (short)(pfs * 6 / 5); done = 1; }
        if (!done && length(val, pfs, &px, &pct)) { s->fs = (short)(pct ? pfs * pct / 100 : px); } if (s->fs < 6) s->fs = 6; if (s->fs > 120) s->fs = 120;
    }
    else if (IS("font-weight")) s->bold = !strcmp(val, "bold") || !strcmp(val, "bolder") || atoi(val) >= 600;
    else if (IS("font-style")) s->italic = !strncmp(val, "italic", 6) || !strncmp(val, "oblique", 7);
    else if (IS("font-family")) { char lo[100]; size_t k = 0; for (; val[k] && k < 99; k++) lo[k] = (char)tolower((unsigned char)val[k]); lo[k] = 0; if (strstr(lo, "mono") || strstr(lo, "courier") || strstr(lo, "consolas")) s->mono = 1; else if (!strstr(lo, "serif") || strstr(lo, "sans")) s->mono = 0; }
    else if (IS("text-decoration") || IS("text-decoration-line")) { s->ul = strstr(val, "underline") != NULL; s->strike = strstr(val, "line-through") != NULL; }
    else if (IS("text-align")) s->align = !strncmp(val, "center", 6) ? 1 : !strncmp(val, "right", 5) || !strncmp(val, "end", 3) ? 2 : 0;
    else if (IS("text-transform")) { s->upper = !strncmp(val, "uppercase", 9); s->lower = !strncmp(val, "lowercase", 9); }
    else if (IS("white-space")) { s->pre = !strncmp(val, "pre", 3); s->nowrap = !strncmp(val, "nowrap", 6); }
    else if (IS("visibility")) s->hidden = !strncmp(val, "hidden", 6) || !strncmp(val, "collapse", 8);
    else if (IS("display")) {
        if (!strncmp(val, "none", 4)) s->display = D_NONE; else if (!strncmp(val, "block", 5)) s->display = D_BLOCK; else if (!strncmp(val, "inline-block", 12) || !strncmp(val, "inline-flex", 11) || !strncmp(val, "inline-grid", 11) || !strncmp(val, "inline-table", 12)) s->display = D_IBLOCK;
        else if (!strncmp(val, "inline", 6)) s->display = D_INLINE; else if (!strncmp(val, "list-item", 9)) s->display = D_LISTITEM;
        else if (!strncmp(val, "flex", 4)) { s->display = D_BLOCK; s->jcenter = s->jcenter; s->col = s->col; s->sticky_dummy = 1; }
        else if (!strncmp(val, "grid", 4)) s->display = D_BLOCK; else if (!strncmp(val, "table-row", 9)) s->display = D_ROW; else if (!strncmp(val, "table-cell", 10)) s->display = D_IBLOCK; else if (!strncmp(val, "table", 5)) s->display = D_BLOCK;
        else if (!strncmp(val, "contents", 8)) s->display = D_INLINE;
    }
    else if (IS("flex-direction")) s->col = !strncmp(val, "column", 6);
    else if (IS("justify-content")) s->jcenter = !strncmp(val, "center", 6) ? 1 : !strncmp(val, "flex-end", 8) || !strncmp(val, "end", 3) ? 2 : 0;
    else if (IS("margin")) box4(val, s->fs, &s->mt, &s->mr, &s->mb, &s->ml, &s->automl, &s->automr);
    else if (IS("margin-top") && length(val, s->fs, &px, &pct)) s->mt = (short)px; else if (IS("margin-bottom") && length(val, s->fs, &px, &pct)) s->mb = (short)px;
    else if (IS("margin-left")) { if (!strncmp(val, "auto", 4)) s->automl = 1; else if (length(val, s->fs, &px, &pct)) { s->ml = (short)px; s->automl = 0; } }
    else if (IS("margin-right")) { if (!strncmp(val, "auto", 4)) s->automr = 1; else if (length(val, s->fs, &px, &pct)) { s->mr = (short)px; s->automr = 0; } }
    else if (IS("padding")) box4(val, s->fs, &s->pt, &s->pr, &s->pb, &s->pl, NULL, NULL);
    else if (IS("padding-top") && length(val, s->fs, &px, &pct)) s->pt = (short)px; else if (IS("padding-bottom") && length(val, s->fs, &px, &pct)) s->pb = (short)px;
    else if (IS("padding-left") && length(val, s->fs, &px, &pct)) s->pl = (short)px; else if (IS("padding-right") && length(val, s->fs, &px, &pct)) s->pr = (short)px;
    else if (IS("border") || IS("border-top") || IS("border-bottom") || IS("border-left") || IS("border-right") || IS("outline")) {
        if (IS("outline")) goto done;
        int w = 0, bs = strstr(val, "none") || strstr(val, "hidden") ? 0 : 1; u32 c = s->color; int any = 0;
        for (const char *t = val; *t; ) { while (isspace((unsigned char)*t)) t++; int okc; u32 cc = css_color(t, &okc); if (okc == 1) { c = cc; any = 1; } else if (length(t, s->fs, &px, &pct) && isdigit((unsigned char)*t)) { w = px; } else if (!strncmp(t, "thin", 4)) w = 1; else if (!strncmp(t, "medium", 6)) w = 2; else if (!strncmp(t, "thick", 5)) w = 3;
            int d = 0; while (*t && (!isspace((unsigned char)*t) || d)) { if (*t == '(') d++; if (*t == ')') d--; t++; } }
        if (!w && bs) w = 1; if (!bs) w = 0; (void)any;
        unsigned char sides = IS("border") ? 15 : IS("border-top") ? 1 : IS("border-right") ? 2 : IS("border-bottom") ? 4 : 8;
        if (w) { s->bsides |= sides; s->bw = (short)w; s->bc = c; } else s->bsides &= (unsigned char)~sides;
    }
    else if (IS("border-width") && length(val, s->fs, &px, &pct)) { s->bw = (short)px; if (px) s->bsides = s->bsides ? s->bsides : 15; }
    else if (IS("border-color")) { u32 c = css_color(val, &ok); if (ok == 1) s->bc = c; }
    else if (IS("border-style")) { if (!strncmp(val, "none", 4)) s->bsides = 0; else if (!s->bsides) { s->bsides = 15; if (!s->bw) s->bw = 1; } }
    else if (IS("border-radius") && length(val, s->fs, &px, &pct)) s->radius = (short)(pct ? 8 : px);
    else if (IS("width")) { if (length(val, s->fs, &px, &pct)) { if (pct) s->wpct = (short)pct; else { s->width = (short)px; s->wpct = 0; } } else if (!strncmp(val, "auto", 4)) { s->width = 0; s->wpct = 0; } }
    else if (IS("max-width")) { if (length(val, s->fs, &px, &pct)) s->maxw = pct ? 0 : (short)px; else s->maxw = 0; }
    else if (IS("min-width") && length(val, s->fs, &px, &pct)) s->minw = pct ? 0 : (short)px;
    else if (IS("height") && length(val, s->fs, &px, &pct)) s->height = pct ? 0 : (short)px;
    else if (IS("line-height")) { if (length(val, s->fs, &px, &pct)) s->lh = (short)(pct ? s->fs * pct / 100 : px); else { double v = atof(val); if (v > 0.5 && v < 6) s->lh = (short)(v * s->fs + 0.5); } }
    done:
    #undef IS
    free(val);
    (void)n;
}
static void apply_block(Page *pg, Node *n, Style *s, const Style *par, const char *decl, int important_pass) {
    const char *p = decl;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ';')) p++;
        const char *ks = p; while (*p && *p != ':' && *p != ';') p++; if (*p != ':') { if (*p) p++; continue; }
        char prop[40]; size_t kl = (size_t)(p - ks); while (kl && isspace((unsigned char)ks[kl - 1])) kl--; if (kl > 39) kl = 39; for (size_t i = 0; i < kl; i++) prop[i] = (char)tolower((unsigned char)ks[i]); prop[kl] = 0;
        p++; const char *vs = p; int d = 0; char qc = 0; while (*p && (d || qc || *p != ';')) { if (qc) { if (*p == qc) qc = 0; } else if (*p == '"' || *p == '\'') qc = *p; else if (*p == '(') d++; else if (*p == ')') d--; p++; }
        size_t vl = (size_t)(p - vs); char *v = strndup(vs, vl);
        int imp = 0; char *bang = strstr(v, "!important"); if (bang) { *bang = 0; imp = 1; }
        if (imp == important_pass && prop[0] != '-') apply_decl(pg, n, s, par, prop, v);
        free(v);
    }
}

/* ---- stylesheet parsing ---- */
static void add_rule(Page *p, const char *selector, size_t sl, const char *decl, size_t dl, int *order) {
    char *sel = strndup(selector, sl); char *save = NULL;
    for (char *part = strtok_r(sel, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
        Sel *sp = malloc(sizeof *sp);
        if (!parse_sel(part, sp)) { free(sp); continue; }
        if (p->nvars < 48 && dl > 0) {   /* custom properties on :root / html / body / * */
            const Cmp *c = &sp->c[sp->n - 1];
            if (sp->n == 1 && (!c->tag[0] || !strcmp(c->tag, "html") || !strcmp(c->tag, "body")) && !c->ncls && !c->id[0]) {
                char *d = strndup(decl, dl);
                for (const char *q = d; (q = strstr(q, "--")); ) { const char *k = q; q += 2; while (idch((unsigned char)*q)) q++; size_t kl = (size_t)(q - k); while (isspace((unsigned char)*q)) q++; if (*q != ':') continue; q++; const char *v = q; while (*q && *q != ';') q++;
                    if (p->nvars < 48) { p->vk[p->nvars] = strndup(k, kl); char *vv = strndup(v, (size_t)(q - v)); char *t = vv; while (isspace((unsigned char)*t)) t++; p->vv[p->nvars] = strdup(t); free(vv); p->nvars++; } }
                free(d);
            }
        }
        if (p->nrules >= p->caprules) { if (p->nrules >= 6000) { free(sp); continue; } p->caprules = p->caprules ? p->caprules * 2 : 128; p->rules = realloc(p->rules, (size_t)p->caprules * sizeof(Rule)); }
        Rule *r = &p->rules[p->nrules++]; r->sel = sp; r->decl = strndup(decl, dl); r->spec = spec_of(sp); r->order = (*order)++;
    }
    free(sel);
}
static void parse_css(Page *p, const char *s, size_t n, int *order, int depth) {
    char *buf = malloc(n + 1); size_t k = 0;
    for (size_t i = 0; i < n; i++) { if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++; i++; continue; } buf[k++] = s[i]; }
    buf[k] = 0;
    const char *c = buf;
    while (*c) {
        while (isspace((unsigned char)*c)) c++;
        if (!*c) break;
        if (*c == '@') {
            const char *a = c; while (*c && *c != '{' && *c != ';') c++;
            size_t al = (size_t)(c - a);
            if (*c == ';') { c++; continue; }
            if (!*c) break;
            const char *bs = ++c; int d = 1; while (*c && d) { if (*c == '{') d++; if (*c == '}') d--; c++; }
            size_t bl = (size_t)(c - bs) - (d == 0 ? 1 : 0);
            char *head = strndup(a, al); int use = 0;
            if (!strncmp(head, "@media", 6)) {
                use = !strstr(head, "print") && !strstr(head, "prefers-color-scheme: dark");
                const char *mw = strstr(head, "max-width:"); if (mw && atoi(mw + 10) > 0 && atoi(mw + 10) < 700) use = 0;
                const char *mn = strstr(head, "min-width:"); if (mn && atoi(mn + 10) > 1100) use = 0;
            } else if (!strncmp(head, "@supports", 9)) use = 1;
            free(head);
            if (use && depth < 4) parse_css(p, bs, bl, order, depth + 1);
            continue;
        }
        const char *sel = c; while (*c && *c != '{') c++;
        if (!*c) break;
        size_t sl = (size_t)(c - sel); const char *ds = ++c; int d = 1; while (*c && d) { if (*c == '{') d++; if (*c == '}') d--; c++; }
        size_t dl = (size_t)(c - ds) - (d == 0 ? 1 : 0);
        add_rule(p, sel, sl, ds, dl, order);
    }
    free(buf);
}
void css_add_sheet(Page *p, const char *css) { int order = p->nrules; parse_css(p, css, strlen(css), &order, 0); }

/* ---- UA defaults + cascade ---- */
static void ua_style(const Node *n, Style *s, const Style *par) {
    const char *t = n->tag;
    #define T(x) (!strcmp(t, x))
    if (T("html") || T("body") || T("div") || T("p") || T("ul") || T("ol") || T("dl") || T("dt") || T("dd") || T("blockquote") || T("pre") || T("form") || T("fieldset") || T("section") || T("article") || T("nav") || T("header") ||
        T("footer") || T("main") || T("aside") || T("figure") || T("figcaption") || T("address") || T("table") || T("tbody") || T("thead") || T("tfoot") || T("caption") || T("center") || T("details") || T("summary") || T("hr") || T("legend") ||
        (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2])) s->display = D_BLOCK;
    if (T("li")) s->display = D_LISTITEM;
    if (T("tr")) s->display = D_ROW;
    if (T("td") || T("th") || T("img") || T("input") || T("button") || T("select") || T("textarea") || T("svg") || T("canvas") || T("video") || T("iframe")) s->display = D_IBLOCK;
    if (T("head") || T("script") || T("style") || T("title") || T("meta") || T("link") || T("base") || T("noscript") || T("template") || T("option") || T("optgroup") || T("datalist") || T("area") || T("map") || T("source") || T("track") || wn_attr(n, "hidden")) s->display = D_NONE;
    if (T("body")) { s->ml = s->mr = s->mt = s->mb = 8; }
    if (T("p") || T("dl") || T("figure")) { s->mt = s->mb = (short)s->fs; }
    if (T("ul") || T("ol")) { s->mt = s->mb = (short)s->fs; s->pl = 32; if (par && (par->display == D_LISTITEM)) s->mt = s->mb = 0; }
    if (T("dd")) s->ml = 32; if (T("blockquote")) { s->ml = 32; s->mr = 32; s->mt = s->mb = (short)s->fs; }
    if (T("pre")) { s->mono = 1; s->pre = 1; s->fs = 13; s->mt = s->mb = 13; }
    if (T("code") || T("kbd") || T("samp") || T("tt")) { s->mono = 1; s->fs = (short)(par ? par->fs * 13 / 16 : 13); }
    if (T("h1")) { s->fs = 32; s->bold = 1; s->mt = s->mb = 21; } else if (T("h2")) { s->fs = 24; s->bold = 1; s->mt = s->mb = 20; } else if (T("h3")) { s->fs = 19; s->bold = 1; s->mt = s->mb = 19; }
    else if (T("h4")) { s->fs = 16; s->bold = 1; s->mt = s->mb = 21; } else if (T("h5")) { s->fs = 13; s->bold = 1; s->mt = s->mb = 22; } else if (T("h6")) { s->fs = 11; s->bold = 1; s->mt = s->mb = 25; }
    if (T("b") || T("strong") || T("th") || T("dt") || T("legend") || T("summary")) s->bold = 1;
    if (T("i") || T("em") || T("cite") || T("var") || T("address")) s->italic = 1;
    if (T("u") || T("ins")) s->ul = 1; if (T("s") || T("del") || T("strike")) s->strike = 1;
    if (T("a") && wn_attr(n, "href")) { s->color = RGB(0, 102, 204); s->ul = 1; }
    if (T("small") || T("sub") || T("sup")) s->fs = (short)(s->fs * 5 / 6); if (T("big")) s->fs = (short)(s->fs * 6 / 5);
    if (T("mark")) { s->bg = RGB(255, 240, 120); s->has_bg = 1; }
    if (T("hr")) { s->mt = s->mb = 8; s->bsides = 4; s->bw = 1; s->bc = RGB(190, 190, 190); s->height = 0; }
    if (T("center") || T("th")) s->align = 1;
    if (T("table")) { s->mt = s->mb = 4; }
    if (T("td") || T("th")) { s->pl = s->pr = 6; s->pt = s->pb = 3; }
    if (T("fieldset")) { s->bsides = 15; s->bw = 1; s->bc = RGB(190, 190, 190); s->pl = s->pr = 10; s->pt = s->pb = 6; }
    #undef T
}
static void cascade_node(Page *pg, Node *n, const Style *par, int *idx, int nr, int *m) {
    if (n->type == 1) { if (par) n->st = *par; return; }
    Style s; memset(&s, 0, sizeof s);
    s.color = RGB(0, 0, 0); s.fs = 16; s.display = D_INLINE;
    if (par) { s.color = par->color; s.fs = par->fs; s.bold = par->bold; s.italic = par->italic; s.ul = par->ul; s.strike = par->strike; s.align = par->align; s.pre = par->pre; s.upper = par->upper; s.lower = par->lower; s.hidden = par->hidden; s.mono = par->mono; s.nowrap = par->nowrap; s.lh = par->lh; }
    if (!strcmp(n->tag, "#doc")) { s.display = D_BLOCK; }
    else {
        ua_style(n, &s, par);
        int cnt = 0;
        for (int i = 0; i < nr; i++) if (match_sel(n, (Sel *)pg->rules[i].sel, ((Sel *)pg->rules[i].sel)->n - 1)) m[cnt++] = i;
        for (int i = 1; i < cnt; i++) { int v = m[i], j = i - 1; while (j >= 0 && (pg->rules[m[j]].spec > pg->rules[v].spec || (pg->rules[m[j]].spec == pg->rules[v].spec && pg->rules[m[j]].order > pg->rules[v].order))) { m[j + 1] = m[j]; j--; } m[j + 1] = v; }
        for (int i = 0; i < cnt; i++) apply_block(pg, n, &s, par, pg->rules[m[i]].decl, 0);
        const char *inl = wn_attr(n, "style"); if (inl) apply_block(pg, n, &s, par, inl, 0);
        for (int i = 0; i < cnt; i++) apply_block(pg, n, &s, par, pg->rules[m[i]].decl, 1);
        if (inl) apply_block(pg, n, &s, par, inl, 1);
        if (!strcmp(n->tag, "body") && !s.has_bg) { /* page canvas stays white */ }
    }
    n->st = s;
    (void)idx;
    for (Node *c = n->first; c; c = c->next) cascade_node(pg, c, &n->st, idx, nr, m);
}
void css_cascade(Page *p) { int *m = malloc((size_t)(p->nrules + 1) * sizeof(int)); int idx = 0; cascade_node(p, p->doc, NULL, &idx, p->nrules, m); free(m); }

/* ---- inline style access (used by JS) ---- */
char *css_style_get(const Node *n, const char *prop) {
    const char *st = wn_attr(n, "style"); if (!st) return NULL; size_t pl = strlen(prop);
    for (const char *p = st; *p; ) {
        while (isspace((unsigned char)*p) || *p == ';') p++;
        const char *k = p; while (*p && *p != ':' && *p != ';') p++;
        size_t kl = (size_t)(p - k); while (kl && isspace((unsigned char)k[kl - 1])) kl--;
        if (*p != ':') continue; p++; while (isspace((unsigned char)*p)) p++; const char *v = p; while (*p && *p != ';') p++;
        if (kl == pl && !strncasecmp(k, prop, pl)) { size_t vl = (size_t)(p - v); while (vl && isspace((unsigned char)v[vl - 1])) vl--; return strndup(v, vl); }
    }
    return NULL;
}
void css_style_prop(Node *n, const char *prop, const char *val) {
    const char *st = wn_attr(n, "style"); size_t pl = strlen(prop);
    char *out = malloc((st ? strlen(st) * 2 : 0) + strlen(prop) + strlen(val) + 16); out[0] = 0; size_t o = 0;
    if (st) for (const char *p = st; *p; ) {
        while (isspace((unsigned char)*p) || *p == ';') p++;
        const char *k = p; while (*p && *p != ';') p++;
        const char *colon = memchr(k, ':', (size_t)(p - k)); size_t kl = colon ? (size_t)(colon - k) : 0; while (kl && isspace((unsigned char)k[kl - 1])) kl--;
        if (!colon || (kl == pl && !strncasecmp(k, prop, pl))) continue;
        o += (size_t)sprintf(out + o, "%.*s;", (int)(p - k), k);
    }
    if (val[0]) sprintf(out + o, "%s:%s;", prop, val);
    wn_set_attr(n, "style", out); free(out);
}
