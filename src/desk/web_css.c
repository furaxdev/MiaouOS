/* Miaou Web: CSS parser, selector matching and cascade (a pragmatic subset of CSS 2.1 + a little flexbox). */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web.h"

typedef struct Cmp { char tag[20], id[40], cls[5][32], ak[2][24], av[2][40], aop[2]; int ncls, na, comb, pseudo, never, pel, nth_a, nth_b, nth_kind, nsub, subneg; struct Cmp *sub[4]; } Cmp;
typedef struct { Cmp c[8]; int n; } Sel;
enum { P_FIRST = 1, P_LAST = 2, P_ONLY = 4, P_ROOT = 8, P_LINK = 16, P_EMPTY = 32, P_HOVER = 64, P_FOCUS = 128, P_CHECKED = 256, P_DISABLED = 512, P_FIRSTT = 1024, P_LASTT = 2048, P_ENABLED = 4096, P_NTH = 8192 };
static Node *g_hover, *g_focus; static int g_has_hover;

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
            else if (*s == '.') { char tmpc[32]; if (c->ncls < 5) s = ident(s + 1, c->cls[c->ncls++], 32); else s = ident(s + 1, tmpc, 32); }
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
                int dbl = s[1] == ':'; char nm[28]; s = ident(s + 1 + dbl, nm, sizeof nm);
                if (!strcmp(nm, "before") || !strcmp(nm, "after")) { c->pel = !strcmp(nm, "before") ? 1 : 2; continue; }
                if (dbl) return 0;                                           /* other pseudo-elements: not rendered */
                const char *arg = NULL; size_t arglen = 0;
                if (*s == '(') { int d = 0; const char *a0 = s + 1; while (*s) { if (*s == '(') d++; if (*s == ')' && --d == 0) break; s++; } arg = a0; arglen = (size_t)(s - a0); if (*s == ')') s++; }
                if (!strcmp(nm, "first-child")) c->pseudo |= P_FIRST; else if (!strcmp(nm, "last-child")) c->pseudo |= P_LAST;
                else if (!strcmp(nm, "only-child")) c->pseudo |= P_ONLY; else if (!strcmp(nm, "root")) c->pseudo |= P_ROOT;
                else if (!strcmp(nm, "link") || !strcmp(nm, "any-link")) c->pseudo |= P_LINK; else if (!strcmp(nm, "empty")) c->pseudo |= P_EMPTY;
                else if (!strcmp(nm, "hover")) { c->pseudo |= P_HOVER; g_has_hover = 1; } else if (!strcmp(nm, "focus") || !strcmp(nm, "focus-within") || !strcmp(nm, "focus-visible")) { c->pseudo |= P_FOCUS; g_has_hover = 1; }
                else if (!strcmp(nm, "checked")) c->pseudo |= P_CHECKED; else if (!strcmp(nm, "disabled")) c->pseudo |= P_DISABLED; else if (!strcmp(nm, "enabled")) c->pseudo |= P_ENABLED;
                else if (!strcmp(nm, "first-of-type")) c->pseudo |= P_FIRSTT; else if (!strcmp(nm, "last-of-type")) c->pseudo |= P_LASTT;
                else if (!strncmp(nm, "nth-", 4) && arg) {
                    char buf[40]; size_t l = arglen < 39 ? arglen : 39; memcpy(buf, arg, l); buf[l] = 0; int a = 0, b = 0;
                    char *t = buf; while (*t == ' ') t++; for (char *q = t; *q; q++) *q = (char)tolower((unsigned char)*q);
                    if (!strncmp(t, "odd", 3)) { a = 2; b = 1; } else if (!strncmp(t, "even", 4)) { a = 2; b = 0; }
                    else { char *np = strchr(t, 'n'); if (np) { char as[16]; size_t al = (size_t)(np - t); if (al > 15) al = 15; memcpy(as, t, al); as[al] = 0; a = !as[0] || !strcmp(as, "+") ? 1 : !strcmp(as, "-") ? -1 : atoi(as); b = atoi(np + 1 + (np[1] == '+' ? 1 : 0)); if (np[1] == '-') b = -atoi(np + 2); } else { a = 0; b = atoi(t); } }
                    c->nth_a = a; c->nth_b = b; c->nth_kind = !strcmp(nm, "nth-child") ? 1 : !strcmp(nm, "nth-of-type") ? 2 : !strcmp(nm, "nth-last-child") ? 3 : 4; c->pseudo |= P_NTH;
                }
                else if ((!strcmp(nm, "not") || !strcmp(nm, "is") || !strcmp(nm, "where") || !strcmp(nm, "matches") || !strcmp(nm, "-webkit-any")) && arg) {
                    c->subneg = !strcmp(nm, "not"); char *copy = strndup(arg, arglen); char *save = NULL;
                    for (char *part = strtok_r(copy, ",", &save); part && c->nsub < 4; part = strtok_r(NULL, ",", &save)) { Sel *tmp = malloc(sizeof *tmp); if (parse_sel(part, tmp) && tmp->n == 1) { Cmp *cc = malloc(sizeof *cc); *cc = tmp->c[0]; c->sub[c->nsub++] = cc; } free(tmp); }
                    free(copy); if (!c->nsub) c->never = 1;
                }
                else if (!strcmp(nm, "active") || !strcmp(nm, "visited") || !strcmp(nm, "target") || !strcmp(nm, "placeholder-shown") || !strcmp(nm, "required") || !strcmp(nm, "invalid") || !strcmp(nm, "lang") || !strcmp(nm, "dir")) c->never = 1;
                else c->never = 1;
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
        if (c->pseudo & P_HOVER) { int ok = 0; for (Node *x = g_hover; x; x = x->parent) if (x == n) ok = 1; if (!ok) return 0; }
        if (c->pseudo & P_FOCUS) { int ok = 0; for (Node *x = g_focus; x; x = x->parent) if (x == n) ok = 1; if (!ok) return 0; }
        if ((c->pseudo & P_CHECKED) && !wn_attr(n, "checked") && !wn_attr(n, "selected")) return 0;
        if ((c->pseudo & P_DISABLED) && !wn_attr(n, "disabled")) return 0;
        if ((c->pseudo & P_ENABLED) && wn_attr(n, "disabled")) return 0;
        if (c->pseudo & (P_FIRSTT | P_LASTT)) { int fi = 1, la = 1; for (Node *x = n->prev; x; x = x->prev) if (!x->type && !strcmp(x->tag, n->tag)) fi = 0; for (Node *x = n->next; x; x = x->next) if (!x->type && !strcmp(x->tag, n->tag)) la = 0; if ((c->pseudo & P_FIRSTT) && !fi) return 0; if ((c->pseudo & P_LASTT) && !la) return 0; }
        if (c->pseudo & P_NTH) {
            int idx = 1, ofs = 0; for (Node *x = n->prev; x; x = x->prev) if (!x->type && (c->nth_kind != 2 && c->nth_kind != 4 ? 1 : !strcmp(x->tag, n->tag)) && c->nth_kind <= 2) idx++;
            if (c->nth_kind >= 3) { idx = 1; for (Node *x = n->next; x; x = x->next) if (!x->type && (c->nth_kind != 4 || !strcmp(x->tag, n->tag))) idx++; }
            (void)ofs; int ok = 0;
            if (c->nth_a == 0) ok = idx == c->nth_b; else for (int k = 0; k < 400; k++) { int v = c->nth_a * k + c->nth_b; if (v == idx) { ok = 1; break; } if ((c->nth_a > 0 && v > idx) || (c->nth_a < 0 && v < 0)) break; }
            if (!ok) return 0;
        }
    }
    if (c->nsub) { int any = 0; for (int i = 0; i < c->nsub; i++) if (match_cmp(n, c->sub[i])) any = 1; if (c->subneg ? any : !any) return 0; }
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
    if (!strncasecmp(s, "light-dark(", 11)) { return css_color(s + 11, ok); }
    if (!strncasecmp(s, "color-mix(", 10)) {
        const char *p = strchr(s + 10, ','); if (!p) { *ok = 0; return 0; } p++;
        while (isspace((unsigned char)*p)) p++;
        int ok1, ok2; u32 c1 = css_color(p, &ok1); double w1 = -1, w2 = -1;
        int d = 0; const char *q = p; while (*q && (d || *q != ',')) { if (*q == '(') d++; if (*q == ')') d--; if (d < 0) break; q++; }
        for (const char *t = p; t < q; t++) if (*t == '%') { const char *b = t; while (b > p && (isdigit((unsigned char)b[-1]) || b[-1] == '.')) b--; w1 = atof(b); }
        if (*q != ',') { *ok = 0; return 0; } q++; while (isspace((unsigned char)*q)) q++;
        u32 c2 = css_color(q, &ok2); d = 0; const char *e = q; while (*e && (d || *e != ')' || 0)) { if (*e == '(') d++; if (*e == ')') { if (!d) break; d--; } e++; }
        for (const char *t = q; t < e; t++) if (*t == '%') { const char *b = t; while (b > q && (isdigit((unsigned char)b[-1]) || b[-1] == '.')) b--; w2 = atof(b); }
        if (ok1 != 1 || ok2 != 1) { *ok = ok1 == 1 ? 1 : ok2 == 1 ? 1 : 0; return ok1 == 1 ? c1 : c2; }
        if (w1 < 0 && w2 < 0) w1 = 50; if (w1 < 0) w1 = 100 - w2; double f = w1 / 100.0;
        return RGB((int)(((c1 >> 16) & 255) * f + ((c2 >> 16) & 255) * (1 - f)), (int)(((c1 >> 8) & 255) * f + ((c2 >> 8) & 255) * (1 - f)), (int)((c1 & 255) * f + (c2 & 255) * (1 - f)));
    }
    for (size_t i = 0; i < sizeof N / sizeof N[0]; i++) { size_t l = strlen(N[i].n); if (!strncasecmp(s, N[i].n, l) && !idch((unsigned char)s[l])) return RGB((N[i].c >> 16) & 255, (N[i].c >> 8) & 255, N[i].c & 255); }
    *ok = 0; return 0;
}
/* calc()/min()/max()/clamp(): a value is (px, pct) = px + pct% of the container */
typedef struct { double px, pc; } CV;
static CV cv_expr(const char **p, int fs);
static CV cv_unit(double v, const char *e, int fs, const char **end) {
    CV r = { 0, 0 };
    if (!strncasecmp(e, "px", 2)) { r.px = v; *end = e + 2; } else if (!strncasecmp(e, "rem", 3)) { r.px = v * 16; *end = e + 3; } else if (!strncasecmp(e, "em", 2)) { r.px = v * fs; *end = e + 2; }
    else if (!strncasecmp(e, "pt", 2)) { r.px = v * 4 / 3; *end = e + 2; } else if (*e == '%') { r.pc = v; *end = e + 1; } else if (!strncasecmp(e, "vw", 2)) { r.px = v * 10; *end = e + 2; }
    else if (!strncasecmp(e, "vh", 2)) { r.px = v * 6; *end = e + 2; } else if (!strncasecmp(e, "ch", 2) || !strncasecmp(e, "ex", 2)) { r.px = v * fs / 2; *end = e + 2; } else { r.px = v; *end = e; }
    return r;
}
static double cv_flat(CV v) { return v.px + v.pc * 10.0; }                  /* percentages inside min/max/clamp: assume a 1000px container */
static CV cv_factor(const char **p, int fs) {
    while (isspace((unsigned char)**p)) (*p)++;
    CV r = { 0, 0 };
    if (**p == '(') { (*p)++; r = cv_expr(p, fs); while (isspace((unsigned char)**p)) (*p)++; if (**p == ')') (*p)++; return r; }
    if (!strncasecmp(*p, "calc(", 5)) { *p += 5; r = cv_expr(p, fs); while (isspace((unsigned char)**p)) (*p)++; if (**p == ')') (*p)++; return r; }
    int fn = !strncasecmp(*p, "min(", 4) ? 1 : !strncasecmp(*p, "max(", 4) ? 2 : !strncasecmp(*p, "clamp(", 6) ? 3 : 0;
    if (fn) {
        *p = strchr(*p, '(') + 1; CV a[3]; int n = 0;
        while (n < 3) { a[n++] = cv_expr(p, fs); while (isspace((unsigned char)**p)) (*p)++; if (**p == ',') { (*p)++; continue; } break; }
        if (**p == ')') (*p)++;
        if (fn == 1) { r = a[0]; for (int i = 1; i < n; i++) if (cv_flat(a[i]) < cv_flat(r)) r = a[i]; }
        else if (fn == 2) { r = a[0]; for (int i = 1; i < n; i++) if (cv_flat(a[i]) > cv_flat(r)) r = a[i]; }
        else if (n == 3) { r = a[1]; if (cv_flat(r) < cv_flat(a[0])) r = a[0]; if (cv_flat(r) > cv_flat(a[2])) r = a[2]; }
        return r;
    }
    char *e; double v = strtod(*p, &e); if (e == *p) return r;
    const char *end; r = cv_unit(v, e, fs, &end); *p = end; return r;
}
static CV cv_term(const char **p, int fs) {
    CV a = cv_factor(p, fs);
    for (;;) {
        while (isspace((unsigned char)**p)) (*p)++;
        if (**p == '*') { (*p)++; CV b = cv_factor(p, fs); double k = b.px != 0 || b.pc != 0 ? (b.px + b.pc) : 0; if (a.px == 0 && a.pc == 0) { a = b; } else { a.px *= k; a.pc *= k; } }
        else if (**p == '/') { (*p)++; CV b = cv_factor(p, fs); double k = b.px + b.pc; if (k != 0) { a.px /= k; a.pc /= k; } }
        else break;
    }
    return a;
}
static CV cv_expr(const char **p, int fs) {
    CV a = cv_term(p, fs);
    for (;;) {
        while (isspace((unsigned char)**p)) (*p)++;
        if (**p == '+') { (*p)++; CV b = cv_term(p, fs); a.px += b.px; a.pc += b.pc; }
        else if (**p == '-' && isspace((unsigned char)(*p)[1])) { (*p)++; CV b = cv_term(p, fs); a.px -= b.px; a.pc -= b.pc; }
        else break;
    }
    return a;
}
static int g_wadd;    /* px part of a "calc(P% + Npx)" width, reported to apply_decl */
/* returns 1 and sets *px (css px) when s starts with a length; *pct set when it is a percentage */
static int length(const char *s, int fs, int *px, int *pct) {
    while (isspace((unsigned char)*s)) s++;
    g_wadd = 0;
    if (!strncasecmp(s, "calc(", 5) || !strncasecmp(s, "min(", 4) || !strncasecmp(s, "max(", 4) || !strncasecmp(s, "clamp(", 6)) {
        const char *p = s; CV v = cv_factor(&p, fs);
        if (v.pc != 0 && (!strncasecmp(s, "calc(", 5))) { *pct = (int)v.pc; *px = 0; g_wadd = (int)v.px; return 1; }
        *pct = 0; *px = (int)(v.px + v.pc * 10.0 + (v.px + v.pc * 10.0 < 0 ? -0.5 : 0.5)); return 1;
    }
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

static char *cvk[256], *cvv[256]; static int ncv;      /* custom property stack (see cascade_node) */
typedef struct { Page *p; } Ctx;
static const char *var_lookup(Page *p, const char *name, size_t n) { for (int i = ncv - 1; i >= 0; i--) if (strlen(cvk[i]) == n && !strncmp(cvk[i], name, n)) return cvv[i]; for (int i = 0; i < p->nvars; i++) if (strlen(p->vk[i]) == n && !strncmp(p->vk[i], name, n)) return p->vv[i]; return NULL; }
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
    else if (IS("background-color") || IS("background") || IS("background-image")) {
        int found = 0;
        { const char *g = strstr(val, "gradient("); if (g) { const char *q = strchr(g, '('); int d = 0; if (q) q++;
            while (q && *q && !found) { while (isspace((unsigned char)*q)) q++; u32 c = css_color(q, &ok); if (ok == 1) { s->bg = c; s->has_bg = 1; found = 1; break; }
                while (*q && (d || (*q != ','))) { if (*q == '(') d++; if (*q == ')') { if (!d) { q = NULL; break; } d--; } q++; } if (q && *q == ',') q++; } } }
        if (found) goto bgdone; for (const char *w = val; *w && !found; ) { while (isspace((unsigned char)*w)) w++; u32 c = css_color(w, &ok); if (ok == 1) { s->bg = c; s->has_bg = 1; found = 1; } else if (ok == 2 && IS("background-color")) { s->has_bg = 0; found = 1; }
            if (*w == '(') w++; int d = 0; while (*w && (!isspace((unsigned char)*w) || d)) { if (*w == '(') d++; if (*w == ')') d--; w++; } }
        if (!found && (IS("background") && (!strncmp(val, "none", 4) || !strncmp(val, "transparent", 11)))) s->has_bg = 0;
        bgdone:;
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
        else if (!strncmp(val, "flex", 4)) { s->display = D_BLOCK; s->sticky_dummy = 1; s->isgrid = 0; }
        else if (!strncmp(val, "grid", 4)) { s->display = D_BLOCK; s->sticky_dummy = 1; s->isgrid = 1; } else if (!strncmp(val, "table-row", 9)) s->display = D_ROW; else if (!strncmp(val, "table-cell", 10)) s->display = D_IBLOCK; else if (!strncmp(val, "table", 5)) s->display = D_BLOCK;
        else if (!strncmp(val, "contents", 8)) s->display = D_INLINE;
    }
    else if (IS("flex-direction")) s->col = !strncmp(val, "column", 6);
    else if (IS("justify-content")) s->jcenter = !strncmp(val, "center", 6) ? 1 : !strncmp(val, "flex-end", 8) || !strncmp(val, "end", 3) || !strncmp(val, "right", 5) ? 2 : !strncmp(val, "space-between", 13) ? 3 : !strncmp(val, "space-around", 12) ? 4 : !strncmp(val, "space-evenly", 12) ? 5 : 0;
    else if (IS("align-items") || IS("align-self")) { if (IS("align-items") || !strncmp(val, "auto", 4) == 0) s->aitems = !strncmp(val, "center", 6) ? 2 : !strncmp(val, "flex-start", 10) || !strncmp(val, "start", 5) || !strncmp(val, "baseline", 8) ? 1 : !strncmp(val, "flex-end", 8) || !strncmp(val, "end", 3) ? 3 : 0; }
    else if (IS("flex-wrap")) s->flexwrap = strncmp(val, "nowrap", 6) != 0 && strncmp(val, "initial", 7) != 0;
    else if (IS("flex-flow")) { s->col = strstr(val, "column") != NULL; s->flexwrap = strstr(val, "wrap") && !strstr(val, "nowrap"); }
    else if (IS("gap") || IS("grid-gap")) { int a, b2, pc; const char *p2 = val; if (length(p2, s->fs, &a, &pc)) { s->rgap = (short)a; s->cgap = (short)a; while (*p2 && !isspace((unsigned char)*p2)) p2++; while (isspace((unsigned char)*p2)) p2++; if (*p2 && length(p2, s->fs, &b2, &pc)) s->cgap = (short)b2; } }
    else if (IS("row-gap") && length(val, s->fs, &px, &pct)) s->rgap = (short)px; else if ((IS("column-gap") || IS("grid-column-gap")) && length(val, s->fs, &px, &pct)) s->cgap = (short)px;
    else if (IS("flex-grow")) s->fgrow = (short)(atof(val) * 10);
    else if (IS("flex-basis")) { if (length(val, s->fs, &px, &pct) && !pct) s->fbasis = (short)px; }
    else if (IS("flex")) {
        if (!strncmp(val, "none", 4)) { s->fgrow = 0; s->fbasis = 0; } else if (!strncmp(val, "auto", 4)) { s->fgrow = 10; }
        else { char *e; double g = strtod(val, &e); if (e != val) { s->fgrow = (short)(g * 10); const char *r = e; while (isspace((unsigned char)*r)) r++; if (*r && isdigit((unsigned char)*r)) { strtod(r, (char **)&r); while (isspace((unsigned char)*r)) r++; } if (*r && (isdigit((unsigned char)*r)) && length(r, s->fs, &px, &pct) && !pct) s->fbasis = (short)px; } }
    }
    else if (IS("box-sizing")) s->bbox = !strncmp(val, "border-box", 10);
    else if (IS("list-style") || IS("list-style-type")) s->nolist = !strncmp(val, "none", 4) || strstr(val, " none") != NULL;
    else if (IS("position")) s->pos = !strncmp(val, "absolute", 8) ? 1 : !strncmp(val, "fixed", 5) ? 2 : !strncmp(val, "sticky", 6) ? 3 : 0;
    else if (IS("opacity")) s->hide_opacity = atof(val) < 0.05 && isdigit((unsigned char)val[0]);
    else if (IS("grid-template-columns")) {
        s->ngtrack = 0; s->gautofill = 0; s->gmin = 0; const char *t = val;
        while (*t && s->ngtrack < 9) {
            while (isspace((unsigned char)*t)) t++; if (!*t) break;
            if (!strncmp(t, "repeat(", 7)) {
                const char *q = t + 7; int cnt = 1, af = 0; if (!strncmp(q, "auto-fill", 9) || !strncmp(q, "auto-fit", 8)) { af = 1; } else cnt = atoi(q);
                const char *cm = strchr(q, ','); if (!cm) break; cm++; while (isspace((unsigned char)*cm)) cm++;
                int d = 1; const char *e2 = cm; while (*e2 && d) { if (*e2 == '(') d++; if (*e2 == ')') d--; if (d) e2++; }
                short tr = 0; int minpx = 0;
                if (!strncmp(cm, "minmax(", 7)) { if (length(cm + 7, s->fs, &px, &pct)) minpx = px; tr = -10; }
                else if (strstr(cm, "fr")) tr = (short)(-atof(cm) * 10); else if (length(cm, s->fs, &px, &pct)) tr = (short)(pct ? -10 : px); else tr = -10;
                if (af) { s->gautofill = 1; s->gmin = (short)(minpx > 0 ? minpx : (tr > 0 ? tr : 200)); s->gtrack[0] = tr > 0 ? tr : -10; s->ngtrack = 1; }
                else for (int k = 0; k < cnt && s->ngtrack < 9; k++) s->gtrack[s->ngtrack++] = tr;
                t = *e2 ? e2 + 1 : e2; continue;
            }
            if (!strncmp(t, "minmax(", 7)) { int d = 1; const char *e2 = t + 7; while (*e2 && d) { if (*e2 == '(') d++; if (*e2 == ')') d--; if (d) e2++; } s->gtrack[s->ngtrack++] = -10; t = *e2 ? e2 + 1 : e2; continue; }
            if (length(t, s->fs, &px, &pct)) { int isfr = 0; const char *q = t; while (isdigit((unsigned char)*q) || *q == '.') q++; isfr = !strncmp(q, "fr", 2); s->gtrack[s->ngtrack++] = isfr ? (short)(-atof(t) * 10) : (short)(pct ? -10 : px); }
            else s->gtrack[s->ngtrack++] = -10;
            while (*t && !isspace((unsigned char)*t)) t++;
        }
        if (!s->ngtrack) { s->gtrack[0] = -10; s->ngtrack = 1; }
    }
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
    else if (IS("width")) { if (length(val, s->fs, &px, &pct)) { if (pct) { s->wpct = (short)pct; s->wadd = (short)g_wadd; s->width = 0; } else { s->width = (short)px; s->wpct = 0; s->wadd = 0; } } else if (!strncmp(val, "auto", 4)) { s->width = 0; s->wpct = 0; s->wadd = 0; } }
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
static int media_feature(const char *f) {       /* "(min-width: 600px)" body without parentheses; viewport 1000x600 */
    char b[80]; size_t k = 0; for (; f[k] && k < 79; k++) b[k] = (char)tolower((unsigned char)f[k]); b[k] = 0;
    char *c = strchr(b, ':'); char *val = c ? c + 1 : (char *)""; if (c) *c = 0;
    while (*val == ' ') val++; char *nm = b; while (*nm == ' ') nm++;
    double v = atof(val); if (strstr(val, "em")) v *= 16;
    if (!strcmp(nm, "min-width")) return 1000 >= v; if (!strcmp(nm, "max-width")) return 1000 <= v;
    if (!strcmp(nm, "min-height")) return 600 >= v; if (!strcmp(nm, "max-height")) return 600 <= v;
    if (!strcmp(nm, "width")) return v == 1000; if (!strcmp(nm, "prefers-color-scheme")) return !strncmp(val, "light", 5);
    if (!strcmp(nm, "prefers-reduced-motion")) return !strncmp(val, "no-pref", 7); if (!strcmp(nm, "orientation")) return !strncmp(val, "landscape", 9);
    if (!strcmp(nm, "hover") || !strcmp(nm, "any-hover")) return !strncmp(val, "hover", 5); if (!strcmp(nm, "pointer") || !strcmp(nm, "any-pointer")) return !strncmp(val, "fine", 4);
    if (!strcmp(nm, "min-resolution") || !strcmp(nm, "-webkit-min-device-pixel-ratio") || !strcmp(nm, "min-device-pixel-ratio")) return v <= 1;
    if (!strcmp(nm, "display-mode")) return !strncmp(val, "browser", 7); if (!strcmp(nm, "forced-colors") || !strcmp(nm, "prefers-contrast")) return !strncmp(val, "none", 4) || !strncmp(val, "no-pref", 7);
    return 1;
}
static int media_ok(const char *h) {
    char *copy = strdup(h); char *save = NULL; int any = 0;
    for (char *q = strtok_r(copy, ",", &save); q && !any; q = strtok_r(NULL, ",", &save)) {
        int ok = 1, neg = 0; char low[200]; size_t k = 0; for (; q[k] && k < 199; k++) low[k] = (char)tolower((unsigned char)q[k]); low[k] = 0;
        if (strstr(low, "not ")) neg = 1; if (strstr(low, "print") && !strstr(low, "screen") && !strstr(low, "all")) ok = 0; if (strstr(low, "speech")) ok = 0;
        for (const char *t = low; (t = strchr(t, '(')); ) { const char *e = strchr(t, ')'); if (!e) break; char f[100]; size_t l = (size_t)(e - t - 1); if (l > 99) l = 99; memcpy(f, t + 1, l); f[l] = 0; if (!media_feature(f)) ok = 0; t = e; }
        if (neg) ok = !ok; any = ok;
    }
    free(copy); return any;
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
                use = media_ok(head + 6);
            } else if (!strncmp(head, "@supports", 9) || !strncmp(head, "@layer", 6)) use = 1;
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
void css_add_sheet(Page *p, const char *css) { int order = p->nrules; g_has_hover = 0; parse_css(p, css, strlen(css), &order, 0); if (g_has_hover) p->has_hover = 1; }

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
/* custom properties (--x) set on any element, inherited by its subtree (stack, popped when leaving the element) */
static void push_custom(const char *decl) {
    for (const char *q = decl; (q = strstr(q, "--")); ) {
        const char *k = q; q += 2; while (idch((unsigned char)*q)) q++; size_t kl = (size_t)(q - k); while (isspace((unsigned char)*q)) q++;
        if (*q != ':') continue; q++; const char *v = q; int d = 0; while (*q && (d || *q != ';')) { if (*q == '(') d++; if (*q == ')') d--; q++; }
        if (ncv < 256) { cvk[ncv] = strndup(k, kl); char *vv = strndup(v, (size_t)(q - v)); char *t = vv; while (isspace((unsigned char)*t)) t++; cvv[ncv] = strdup(t); free(vv); ncv++; }
    }
}
static char *content_text(const Node *host, const char *val) {
    while (isspace((unsigned char)*val)) val++;
    if (!strncmp(val, "none", 4) || !strncmp(val, "normal", 6) || !*val) return NULL;
    char *o = malloc(strlen(val) * 2 + 8); size_t k = 0;
    for (const char *p = val; *p; ) {
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) {
                if (*p == '\\') { p++; if (isxdigit((unsigned char)*p)) { unsigned cp = 0; int n = 0; while (isxdigit((unsigned char)*p) && n < 6) { cp = cp * 16 + (unsigned)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10)); p++; n++; } if (*p == ' ') p++; k += (size_t)utf8_put(o + k, (int)cp); } else if (*p) o[k++] = *p++; }
                else o[k++] = *p++;
            }
            if (*p) p++;
        } else if (!strncmp(p, "attr(", 5)) { const char *e = strchr(p, ')'); if (!e) break; char nm[40]; size_t l = (size_t)(e - p - 5); if (l > 39) l = 39; memcpy(nm, p + 5, l); nm[l] = 0; const char *v = wn_attr(host, nm); if (v) { size_t vl = strlen(v); o = realloc(o, k + vl + strlen(p) + 8); memcpy(o + k, v, vl); k += vl; } p = e + 1; }
        else p++;
    }
    o[k] = 0; return o;
}
static Node *find_gen(Node *host, int which) { for (Node *c = host->first; c; c = c->next) if (c->gen == which) return c; return NULL; }
static void cascade_node(Page *pg, Node *n, const Style *par, int *idx, int nr, int *m) {
    if (n->type == 1) { if (par) n->st = *par; return; }
    int cv_base = ncv;
    Style s; memset(&s, 0, sizeof s);
    s.color = RGB(0, 0, 0); s.fs = 16; s.display = D_INLINE;
    if (par) { s.color = par->color; s.fs = par->fs; s.bold = par->bold; s.italic = par->italic; s.ul = par->ul; s.strike = par->strike; s.align = par->align; s.pre = par->pre; s.upper = par->upper; s.lower = par->lower; s.hidden = par->hidden; s.mono = par->mono; s.nowrap = par->nowrap; s.lh = par->lh; s.nolist = par->nolist; }
    int cnt = 0, pcnt[3] = { 0, 0, 0 }; static int pm[3][512];
    if (!strcmp(n->tag, "#doc")) { s.display = D_BLOCK; }
    else {
        ua_style(n, &s, par);
        for (int i = 0; i < nr; i++) {
            Sel *sl = (Sel *)pg->rules[i].sel;
            if (!match_sel(n, sl, sl->n - 1)) continue;
            int pel = sl->c[sl->n - 1].pel;
            if (pel) { if (pcnt[pel] < 512) pm[pel][pcnt[pel]++] = i; } else m[cnt++] = i;
        }
        for (int i = 1; i < cnt; i++) { int v = m[i], j = i - 1; while (j >= 0 && (pg->rules[m[j]].spec > pg->rules[v].spec || (pg->rules[m[j]].spec == pg->rules[v].spec && pg->rules[m[j]].order > pg->rules[v].order))) { m[j + 1] = m[j]; j--; } m[j + 1] = v; }
        const char *inl = wn_attr(n, "style");
        for (int i = 0; i < cnt; i++) push_custom(pg->rules[m[i]].decl);
        if (inl) push_custom(inl);
        for (int i = 0; i < cnt; i++) apply_block(pg, n, &s, par, pg->rules[m[i]].decl, 0);
        if (inl) apply_block(pg, n, &s, par, inl, 0);
        for (int i = 0; i < cnt; i++) apply_block(pg, n, &s, par, pg->rules[m[i]].decl, 1);
        if (inl) apply_block(pg, n, &s, par, inl, 1);
        if (s.hide_opacity) s.display = D_NONE;
    }
    n->st = s;
    /* ::before / ::after: one generated child each, reused between cascades */
    for (int w = 1; w <= 2; w++) {
        Node *g = find_gen(n, w); char *txt = NULL; Style ps; memset(&ps, 0, sizeof ps); int have = 0;
        if (pcnt[w]) {
            for (int i = 1; i < pcnt[w]; i++) { int v = pm[w][i], j = i - 1; while (j >= 0 && (pg->rules[pm[w][j]].spec > pg->rules[v].spec || (pg->rules[pm[w][j]].spec == pg->rules[v].spec && pg->rules[pm[w][j]].order > pg->rules[v].order))) { pm[w][j + 1] = pm[w][j]; j--; } pm[w][j + 1] = v; }
            ps = s; ps.display = D_INLINE; ps.mt = ps.mb = ps.ml = ps.mr = ps.pt = ps.pb = ps.pl = ps.pr = 0; ps.bsides = 0; ps.bw = 0; ps.has_bg = 0; ps.width = ps.wpct = ps.wadd = ps.height = ps.maxw = ps.minw = 0; ps.sticky_dummy = 0; ps.radius = 0; ps.fgrow = ps.fbasis = 0;
            for (int pass = 0; pass < 2; pass++) for (int i = 0; i < pcnt[w]; i++) {
                apply_block(pg, n, &ps, &s, pg->rules[pm[w][i]].decl, pass);
                if (pass == 0) { const char *c = strstr(pg->rules[pm[w][i]].decl, "content"); if (c && (c == pg->rules[pm[w][i]].decl || !idch((unsigned char)c[-1]))) { const char *cc = c + 7; while (isspace((unsigned char)*cc)) cc++; if (*cc == ':') { cc++; const char *e = cc; int q = 0; while (*e && (q || *e != ';')) { if (*e == '"' || *e == '\'') q = q ? 0 : 1; e++; } char *tmp = strndup(cc, (size_t)(e - cc)); free(txt); txt = content_text(n, tmp); free(tmp); have = 1; } } }
            }
        }
        if (have && txt && ps.display != D_NONE) {
            if (!g) { g = wn_new(pg, 0, "span"); g->gen = (unsigned char)w; if (w == 1) wn_insert_before(n, g, n->first); else wn_append(n, g); Node *t = wn_new(pg, 1, NULL); t->text = strdup(""); wn_append(g, t); }
            free(g->first->text); g->first->text = txt; txt = NULL; g->st = ps;
        } else if (g) wn_unlink(g);
        free(txt);
    }
    (void)idx;
    for (Node *c = n->first; c; c = c->next) { if (c->gen) { c->st = c->st; for (Node *t = c->first; t; t = t->next) t->st = c->st; continue; } cascade_node(pg, c, &n->st, idx, nr, m); }
    for (int i = cv_base; i < ncv; i++) { free(cvk[i]); free(cvv[i]); } ncv = cv_base;
}
void css_cascade(Page *p) {
    g_hover = p->hover; g_focus = p->focus;
    int *m = malloc((size_t)(p->nrules + 1) * sizeof(int)); int idx = 0; ncv = 0; cascade_node(p, p->doc, NULL, &idx, p->nrules, m); free(m);
}

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
