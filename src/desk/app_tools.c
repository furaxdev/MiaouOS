/* Everyday tools: Notes (text editor), Calculator, Image viewer, Clock (calendar, stopwatch, timer). */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

static void card_bg(int x, int y, int w, int h) { gfx_rrect(x, y, w, h, S(8), TH.panel, 255); gfx_rrect_line(x, y, w, h, S(8), TH.line, 255); }

/* ================================================================== Notes: text editor */
typedef struct { size_t pos; char *del, *ins; } EditOp;
typedef struct {
    char path[320]; char *buf; size_t len, cap, cur, anchor;
    int scroll, hscroll, modified, find_on, mouse_sel;
    char find[64]; char msg[120];
    EditOp undo[200]; int nundo, redo_from;
    int px, cw, lh;
} Notes;
enum { NT_SAVE = 1, NT_OPEN, NT_FIND, NT_UNDO, NT_NEW };

static void nt_title(Win *w) {
    Notes *n = w->st; const char *base = n->path[0] ? strrchr(n->path, '/') + 1 : "Sans titre"; char t[100];
    snprintf(t, sizeof t, "%s%s", n->modified ? "● " : "", base); set_title(w, t);
}
static void nt_reserve(Notes *n, size_t extra) { if (n->len + extra + 1 > n->cap) { n->cap = (n->len + extra + 1) * 2; n->buf = realloc(n->buf, n->cap); } }
static void nt_raw_insert(Notes *n, size_t pos, const char *s, size_t k) { nt_reserve(n, k); memmove(n->buf + pos + k, n->buf + pos, n->len - pos); memcpy(n->buf + pos, s, k); n->len += k; n->buf[n->len] = 0; }
static void nt_raw_delete(Notes *n, size_t pos, size_t k) { memmove(n->buf + pos, n->buf + pos + k, n->len - pos - k); n->len -= k; n->buf[n->len] = 0; }
static void nt_push(Notes *n, size_t pos, const char *del, size_t dl, const char *ins, size_t il) {
    for (int i = n->redo_from; i < n->nundo; i++) { free(n->undo[i].del); free(n->undo[i].ins); }
    n->nundo = n->redo_from;
    if (n->nundo == 200) { free(n->undo[0].del); free(n->undo[0].ins); memmove(n->undo, n->undo + 1, 199 * sizeof(EditOp)); n->nundo--; }
    EditOp *o = &n->undo[n->nundo++]; o->pos = pos; o->del = dl ? strndup(del, dl) : NULL; o->ins = il ? strndup(ins, il) : NULL; n->redo_from = n->nundo;
}
static void nt_edit(Notes *n, size_t pos, size_t dl, const char *ins, size_t il) {      /* replace [pos,pos+dl) by ins, recording undo */
    char *d = dl ? strndup(n->buf + pos, dl) : NULL;
    nt_push(n, pos, d, dl, ins, il);
    if (dl) nt_raw_delete(n, pos, dl);
    if (il) nt_raw_insert(n, pos, ins, il);
    free(d); n->modified = 1; n->cur = n->anchor = pos + il;
}
static void nt_undo(Notes *n) {
    if (n->redo_from <= 0) return;
    EditOp *o = &n->undo[--n->redo_from];
    if (o->ins) nt_raw_delete(n, o->pos, strlen(o->ins));
    if (o->del) nt_raw_insert(n, o->pos, o->del, strlen(o->del));
    n->cur = n->anchor = o->pos + (o->del ? strlen(o->del) : 0); n->modified = 1;
}
static void nt_redo(Notes *n) {
    if (n->redo_from >= n->nundo) return;
    EditOp *o = &n->undo[n->redo_from++];
    if (o->del) nt_raw_delete(n, o->pos, strlen(o->del));
    if (o->ins) nt_raw_insert(n, o->pos, o->ins, strlen(o->ins));
    n->cur = n->anchor = o->pos + (o->ins ? strlen(o->ins) : 0); n->modified = 1;
}
static int nt_load(Win *w, const char *path) {
    Notes *n = w->st; size_t k = 0; char *d = read_file(path, &k, 1 << 20);
    if (!d) { snprintf(n->msg, sizeof n->msg, "Lecture impossible : %s", strerror(errno)); return -1; }
    free(n->buf); n->buf = d; n->len = k; n->cap = k + 1; n->cur = n->anchor = 0; n->modified = 0; n->scroll = n->hscroll = 0; n->nundo = n->redo_from = 0;
    snprintf(n->path, sizeof n->path, "%s", path); n->msg[0] = 0; nt_title(w); return 0;
}
static void nt_save(Win *w) {
    Notes *n = w->st;
    if (!n->path[0]) { char dir[300]; snprintf(dir, sizeof dir, "%s/Documents", cur_home); fo_mkdir_p(dir); fo_unique(dir, "Sans titre.txt", n->path, sizeof n->path); }
    FILE *f = fopen(n->path, "w");
    if (!f) { snprintf(n->msg, sizeof n->msg, "Enregistrement impossible : %s", strerror(errno)); toast("Enregistrement impossible"); return; }
    fwrite(n->buf, 1, n->len, f); fclose(f); n->modified = 0; n->msg[0] = 0; nt_title(w); toast("Enregistré : %s", strrchr(n->path, '/') + 1);
}
static void *nt_create(Win *w) { (void)w; Notes *n = calloc(1, sizeof *n); n->cap = 256; n->buf = calloc(1, n->cap); return n; }
static void nt_destroy(Win *w) { Notes *n = w->st; for (int i = 0; i < n->nundo; i++) { free(n->undo[i].del); free(n->undo[i].ins); } free(n->buf); free(n); }
static void nt_open_arg(Win *w, const char *path) { nt_load(w, path); }

static size_t nt_next(Notes *n, size_t p) { if (p >= n->len) return n->len; p++; while (p < n->len && (n->buf[p] & 0xc0) == 0x80) p++; return p; }
static size_t nt_prev(Notes *n, size_t p) { if (!p) return 0; p--; while (p > 0 && (n->buf[p] & 0xc0) == 0x80) p--; return p; }
static size_t nt_line_start(Notes *n, size_t p) { while (p > 0 && n->buf[p - 1] != '\n') p--; return p; }
static size_t nt_line_end(Notes *n, size_t p) { while (p < n->len && n->buf[p] != '\n') p++; return p; }
static int nt_col(Notes *n, size_t p) { size_t s = nt_line_start(n, p); int c = 0; for (size_t i = s; i < p; i = nt_next(n, i)) c++; return c; }
static size_t nt_at_col(Notes *n, size_t ls, int col) { size_t p = ls, e = nt_line_end(n, ls); for (int c = 0; c < col && p < e; c++) p = nt_next(n, p); return p; }
static int nt_line_no(Notes *n, size_t p) { int l = 0; for (size_t i = 0; i < p && i < n->len; i++) if (n->buf[i] == '\n') l++; return l; }
static size_t nt_nth_line(Notes *n, int line) { size_t p = 0; for (int l = 0; l < line && p < n->len; l++) { p = nt_line_end(n, p); if (p < n->len) p++; } return p; }
static void nt_clip_copy(Notes *n, int cut) {
    if (n->cur == n->anchor) return;
    size_t a = n->cur < n->anchor ? n->cur : n->anchor, b = n->cur < n->anchor ? n->anchor : n->cur;
    clip_set(n->buf + a, b - a); if (cut) nt_edit(n, a, b - a, NULL, 0);
}
static void nt_find_next(Notes *n) {
    if (!n->find[0]) return;
    char *p = strcasestr(n->buf + (n->cur < n->len ? n->cur : n->len), n->find);
    if (!p) p = strcasestr(n->buf, n->find);
    if (p) { n->anchor = (size_t)(p - n->buf); n->cur = n->anchor + strlen(n->find); n->msg[0] = 0; } else snprintf(n->msg, sizeof n->msg, "« %s » introuvable", n->find);
}
static void nt_action(Win *w, int id, int mx, int my, int btn) {
    Notes *n = w->st; (void)mx; (void)my; (void)btn;
    if (id == NT_SAVE) nt_save(w); else if (id == NT_FIND) n->find_on = !n->find_on; else if (id == NT_UNDO) nt_undo(n);
    else if (id == NT_NEW) { free(n->buf); n->cap = 256; n->buf = calloc(1, n->cap); n->len = 0; n->cur = n->anchor = 0; n->path[0] = 0; n->modified = 0; n->nundo = n->redo_from = 0; nt_title(w); }
    else if (id == NT_OPEN) open_app_arg("files", cur_home);
}
static void nt_key(Win *w, int key, int mods) {
    Notes *n = w->st; int sh = mods & MOD_SHIFT, ct = mods & MOD_CTRL;
    if (n->find_on && !ct && key != K_UP && key != K_DOWN && key != K_PGUP && key != K_PGDN) {
        size_t l = strlen(n->find);
        if (key == K_ESC) { n->find_on = 0; return; }
        if (key == K_ENTER) { nt_find_next(n); return; }
        if (key == K_BS) { if (l) n->find[l - 1] = 0; return; }
        if (key >= 32 && key < 0x200000 && l < 56) { l += (size_t)utf8_put(n->find + l, key); n->find[l] = 0; return; }
    }
    if (ct) {
        switch (key) {
        case 's': case 'S': nt_save(w); return;
        case 'a': case 'A': n->anchor = 0; n->cur = n->len; return;
        case 'c': case 'C': nt_clip_copy(n, 0); return;
        case 'x': case 'X': nt_clip_copy(n, 1); nt_title(w); return;
        case 'v': case 'V': { size_t k; const char *c = clip_get(&k); if (c && k) { size_t a = n->cur < n->anchor ? n->cur : n->anchor, b = n->cur < n->anchor ? n->anchor : n->cur; nt_edit(n, a, b - a, c, k); nt_title(w); } return; }
        case 'z': case 'Z': if (sh) nt_redo(n); else nt_undo(n); nt_title(w); return;
        case 'y': case 'Y': nt_redo(n); nt_title(w); return;
        case 'f': case 'F': n->find_on = 1; return;
        case 'g': case 'G': nt_find_next(n); return;
        case 'n': case 'N': nt_action(w, NT_NEW, 0, 0, 1); return;
        default: break;
        }
    }
    size_t a = n->cur < n->anchor ? n->cur : n->anchor, b = n->cur < n->anchor ? n->anchor : n->cur;
    int move = 0;
    switch (key) {
    case K_LEFT: n->cur = ct ? nt_prev(n, n->cur) : nt_prev(n, n->cur); if (!sh && a != b) n->cur = a; move = 1; break;
    case K_RIGHT: n->cur = nt_next(n, n->cur); if (!sh && a != b) n->cur = b; move = 1; break;
    case K_UP: case K_DOWN: {
        size_t ls = nt_line_start(n, n->cur); int col = nt_col(n, n->cur);
        if (key == K_UP) { if (ls > 0) n->cur = nt_at_col(n, nt_line_start(n, ls - 1), col); else n->cur = 0; }
        else { size_t le = nt_line_end(n, n->cur); if (le < n->len) n->cur = nt_at_col(n, le + 1, col); else n->cur = n->len; }
        move = 1; break; }
    case K_HOME: n->cur = ct ? 0 : nt_line_start(n, n->cur); move = 1; break;
    case K_END: n->cur = ct ? n->len : nt_line_end(n, n->cur); move = 1; break;
    case K_PGUP: case K_PGDN: { int rows = (w->ch - S(80)) / (n->lh ? n->lh : 20), l = nt_line_no(n, n->cur) + (key == K_PGDN ? rows : -rows); if (l < 0) l = 0; n->cur = nt_at_col(n, nt_nth_line(n, l), nt_col(n, n->cur)); move = 1; break; }
    case K_BS: if (a != b) nt_edit(n, a, b - a, NULL, 0); else if (n->cur > 0) { size_t p = nt_prev(n, n->cur); nt_edit(n, p, n->cur - p, NULL, 0); } nt_title(w); return;
    case K_DELETE: if (a != b) nt_edit(n, a, b - a, NULL, 0); else if (n->cur < n->len) { size_t q = nt_next(n, n->cur); nt_edit(n, n->cur, q - n->cur, NULL, 0); } nt_title(w); return;
    case K_ENTER: { size_t ls = nt_line_start(n, a); size_t ind = 0; while (ls + ind < a && (n->buf[ls + ind] == ' ' || n->buf[ls + ind] == '\t')) ind++;
        char ins[130]; ins[0] = '\n'; size_t k = 1; for (size_t i = 0; i < ind && k < 120; i++) ins[k++] = n->buf[ls + i]; nt_edit(n, a, b - a, ins, k); nt_title(w); return; }
    case K_TAB: nt_edit(n, a, b - a, "    ", 4); nt_title(w); return;
    default:
        if (key >= 32 && key < 0x200000 && !ct) { char u[5]; int k = utf8_put(u, key); nt_edit(n, a, b - a, u, (size_t)k); nt_title(w); }
        return;
    }
    if (move && !sh) n->anchor = n->cur;
}
static void nt_ensure_visible(Notes *n, int rows, int cols) {
    int ln = nt_line_no(n, n->cur), col = nt_col(n, n->cur);
    if (ln < n->scroll) n->scroll = ln; if (ln >= n->scroll + rows) n->scroll = ln - rows + 1;
    if (col < n->hscroll) n->hscroll = col; if (col >= n->hscroll + cols) n->hscroll = col - cols + 1;
}
static void nt_mouse(Win *w, int type, int x, int y, int arg) {
    Notes *n = w->st; int top = S(44), gut = S(56);
    if (type == M_WHEEL) { n->scroll -= arg * 3; if (n->scroll < 0) n->scroll = 0; return; }
    if (y < top || y > w->ch - S(28)) return;
    if (type == M_DOWN || (type == M_MOVE && n->mouse_sel)) {
        int ln = n->scroll + (y - top - S(6)) / (n->lh ? n->lh : 20), col = n->hscroll + (x - gut - S(8) + n->cw / 2) / (n->cw ? n->cw : 8);
        if (ln < 0) ln = 0; if (col < 0) col = 0;
        n->cur = nt_at_col(n, nt_nth_line(n, ln), col);
        if (type == M_DOWN) { n->anchor = n->cur; n->mouse_sel = 1; }
    }
    if (type == M_UP) n->mouse_sel = 0;
}
static void nt_draw(Win *w, int x, int y, int cw, int ch) {
    Notes *n = w->st; n->px = S(14); n->cw = font_cell_w(F_MONO, n->px); n->lh = (int)(n->px * 1.45f);
    int top = S(44), gut = S(56), sbar = S(28);
    ui_button(w, x + S(10), y + S(6), S(36), S(32), NULL, "note", NT_NEW, 0);
    ui_button(w, x + S(50), y + S(6), S(36), S(32), NULL, "folder-open", NT_OPEN, 0);
    ui_button(w, x + S(90), y + S(6), S(36), S(32), NULL, "save", NT_SAVE, 0);
    ui_button(w, x + S(134), y + S(6), S(36), S(32), NULL, "undo", NT_UNDO, 0);
    ui_button(w, x + S(174), y + S(6), S(36), S(32), NULL, "search", NT_FIND, n->find_on);
    if (n->find_on) { ui_field(x + S(222), y + S(6), S(240), S(32), n->find, 1, "Rechercher (Entrée : suivant)"); }
    gfx_hline(x, y + top - 1, cw, TH.line, 255);
    gfx_fill(x, y + top, cw, ch - top - sbar, TH.field, 255);
    gfx_fill(x, y + top, gut, ch - top - sbar, TH.panel, 255); gfx_vline(x + gut, y + top, ch - top - sbar, TH.line, 255);
    int rows = (ch - top - sbar - S(8)) / n->lh, cols = (cw - gut - S(16)) / n->cw;
    nt_ensure_visible(n, rows, cols);
    size_t a = n->cur < n->anchor ? n->cur : n->anchor, b = n->cur < n->anchor ? n->anchor : n->cur;
    size_t p = nt_nth_line(n, n->scroll); int ln = n->scroll, curln = nt_line_no(n, n->cur);
    gfx_clip(x, y + top, cw, ch - top - sbar);
    for (int r = 0; r < rows + 1 && (p <= n->len); r++, ln++) {
        int ty = y + top + S(6) + r * n->lh; size_t le = nt_line_end(n, p);
        if (ln == curln) gfx_fill(x + gut + 1, ty - S(2), cw - gut - 1, n->lh, TH.btn, 120);
        char num[12]; snprintf(num, sizeof num, "%d", ln + 1); text_draw(F_MONO, x + gut - S(10) - text_w(F_MONO, num, S(12)), ty + S(1), num, ln == curln ? TH.text : TH.dim, S(12));
        int col = 0;
        for (size_t i = p; i <= le; ) {
            int sx = x + gut + S(8) + (col - n->hscroll) * n->cw;
            int insel = i >= a && i < b;
            if (insel && i < le && sx >= x + gut && sx < x + cw) gfx_fill(sx, ty - S(2), n->cw, n->lh, TH.accent, 90);
            if (i == le) { if (insel && b > le && sx >= x + gut) gfx_fill(sx, ty - S(2), n->cw / 2, n->lh, TH.accent, 90); break; }
            const char *s = n->buf + i; int cp = utf8_next(&s); size_t ni = (size_t)(s - n->buf);
            if (cp == '\t') cp = ' ';
            if (col >= n->hscroll && col < n->hscroll + cols + 1 && cp > ' ') glyph_draw(F_MONO, sx, ty, cp, TH.text, n->px);
            col++; i = ni;
        }
        if (n->cur >= p && n->cur <= le && w == focus_win && !n->find_on && (now_ms() / 500) % 2 == 0) {
            int cc = nt_col(n, n->cur) - n->hscroll; gfx_fill(x + gut + S(8) + cc * n->cw, ty - S(2), 2, n->lh, TH.accent, 255);
        }
        if (le >= n->len) { if (le == n->len) { p = n->len + 1; } break; }
        p = le + 1;
    }
    gfx_unclip();
    if (w == focus_win) want_blink = 1;
    int sy = y + ch - sbar; gfx_fill(x, sy, cw, sbar, TH.panel, 255); gfx_hline(x, sy, cw, TH.line, 255);
    char st[260]; snprintf(st, sizeof st, "Ligne %d, colonne %d   ·   %zu caractères   ·   %s", curln + 1, nt_col(n, n->cur) + 1, n->len, n->msg[0] ? n->msg : (n->path[0] ? n->path : "Nouveau document"));
    text_draw_clip(F_UI, x + S(12), sy + (sbar - font_line_h(F_UI, S(12))) / 2, st, n->msg[0] ? TH.warn : TH.dim, S(12), cw - S(24));
}

/* ================================================================== Calculator */
typedef struct { char expr[160]; char res[48]; char hist[6][96]; int nh; } Calc;
enum { C_BTN = 100 };
static const char *CALC_PARSE; static int calc_err;
static double c_expr(void);
static void c_ws(void) { while (*CALC_PARSE == ' ') CALC_PARSE++; }
static double c_primary(void) {
    c_ws();
    if (*CALC_PARSE == '(') { CALC_PARSE++; double v = c_expr(); c_ws(); if (*CALC_PARSE == ')') CALC_PARSE++; else calc_err = 1; return v; }
    if ((*CALC_PARSE >= '0' && *CALC_PARSE <= '9') || *CALC_PARSE == '.') { char *e; double v = strtod(CALC_PARSE, &e); CALC_PARSE = e; return v; }
    char name[12]; int k = 0; while (((*CALC_PARSE >= 'a' && *CALC_PARSE <= 'z') || *CALC_PARSE == '_') && k < 10) name[k++] = *CALC_PARSE++; name[k] = 0;
    if (!k) { calc_err = 1; return 0; }
    if (!strcmp(name, "pi")) return M_PI;
    if (!strcmp(name, "e")) return M_E;
    c_ws(); if (*CALC_PARSE != '(') { calc_err = 1; return 0; }
    CALC_PARSE++; double v = c_expr(); c_ws(); if (*CALC_PARSE == ')') CALC_PARSE++; else calc_err = 1;
    if (!strcmp(name, "sqrt")) { if (v < 0) calc_err = 1; return sqrt(v); } if (!strcmp(name, "sin")) return sin(v); if (!strcmp(name, "cos")) return cos(v); if (!strcmp(name, "tan")) return tan(v);
    if (!strcmp(name, "ln")) { if (v <= 0) calc_err = 1; return log(v); } if (!strcmp(name, "log")) { if (v <= 0) calc_err = 1; return log10(v); } if (!strcmp(name, "abs")) return fabs(v);
    calc_err = 1; return 0;
}
static double c_unary(void);
static double c_power(void) { double b = c_primary(); c_ws(); if (*CALC_PARSE == '^') { CALC_PARSE++; double e = c_unary(); return pow(b, e); } return b; }
static double c_unary(void) { c_ws(); if (*CALC_PARSE == '-') { CALC_PARSE++; return -c_unary(); } if (*CALC_PARSE == '+') { CALC_PARSE++; return c_unary(); } return c_power(); }
static double c_term(void) {
    double v = c_unary();
    for (;;) { c_ws(); char o = *CALC_PARSE; if (o != '*' && o != '/' && o != '%') break; CALC_PARSE++; double r = c_unary(); if (o == '*') v *= r; else if (!r) { calc_err = 1; v = 0; } else if (o == '/') v /= r; else v = fmod(v, r); }
    return v;
}
static double c_expr(void) { double v = c_term(); for (;;) { c_ws(); char o = *CALC_PARSE; if (o != '+' && o != '-') break; CALC_PARSE++; double r = c_term(); v = o == '+' ? v + r : v - r; } return v; }
int miaou_calc(const char *src, char *out, size_t n);
static int calc_eval(const char *src, double *out) {
    char norm[200]; size_t k = 0;
    for (const char *p = src; *p && k < 190; ) {                      /* accept the typographic operators too */
        if (!strncmp(p, "×", 2)) { norm[k++] = '*'; p += 2; } else if (!strncmp(p, "÷", 2)) { norm[k++] = '/'; p += 2; }
        else if (!strncmp(p, "−", 3)) { norm[k++] = '-'; p += 3; } else if (!strncmp(p, "π", 2)) { memcpy(norm + k, "pi", 2); k += 2; p += 2; }
        else if (!strncmp(p, "√", 3)) { memcpy(norm + k, "sqrt", 4); k += 4; p += 3; } else norm[k++] = *p++;
    }
    norm[k] = 0; CALC_PARSE = norm; calc_err = 0;
    double v = c_expr(); c_ws();
    if (*CALC_PARSE || calc_err || !isfinite(v)) return -1;
    *out = v; return 0;
}
static void calc_fmt(double v, char *out, size_t n) { if (fabs(v) < 1e15 && v == floor(v)) snprintf(out, n, "%.0f", v); else snprintf(out, n, "%.10g", v); }
static void calc_update(Calc *c) { double v; if (!c->expr[0]) c->res[0] = 0; else if (calc_eval(c->expr, &v) == 0) calc_fmt(v, c->res, sizeof c->res); else c->res[0] = 0; }
static void calc_push(Calc *c, const char *s) { size_t l = strlen(c->expr); size_t k = strlen(s); if (l + k < sizeof c->expr - 1) { memcpy(c->expr + l, s, k + 1); } calc_update(c); }
static void calc_equals(Calc *c) {
    double v; if (!c->expr[0]) return;
    if (calc_eval(c->expr, &v) != 0) { snprintf(c->res, sizeof c->res, "Erreur"); return; }
    char r[48]; calc_fmt(v, r, sizeof r);
    if (c->nh == 6) { memmove(c->hist[0], c->hist[1], 5 * 96); c->nh = 5; }
    { char line[96]; snprintf(line, sizeof line, "%.40s = %.40s", c->expr, r); memcpy(c->hist[c->nh++], line, 96); }
    snprintf(c->expr, sizeof c->expr, "%s", r); c->res[0] = 0;
}
static const char *CALC_KEYS[5][5] = { { "C", "(", ")", "Eff.", "÷" }, { "7", "8", "9", "×", "%" }, { "4", "5", "6", "−", "^" }, { "1", "2", "3", "+", "√(" }, { "0", ".", "π", "±", "=" } };
static void *calc_create(Win *w) { (void)w; return calloc(1, sizeof(Calc)); }
static void calc_destroy(Win *w) { free(w->st); }
static void calc_press(Calc *c, const char *k) {
    if (!strcmp(k, "C")) { c->expr[0] = 0; c->res[0] = 0; }
    else if (!strcmp(k, "Eff.")) { size_t l = strlen(c->expr); while (l > 0 && (c->expr[l - 1] & 0xc0) == 0x80) l--; if (l) l--; c->expr[l] = 0; calc_update(c); }
    else if (!strcmp(k, "=")) calc_equals(c);
    else if (!strcmp(k, "±")) { if (c->expr[0] == '-') memmove(c->expr, c->expr + 1, strlen(c->expr)); else if (strlen(c->expr) < 150) { memmove(c->expr + 1, c->expr, strlen(c->expr) + 1); c->expr[0] = '-'; } calc_update(c); }
    else calc_push(c, k);
}
static void calc_action(Win *w, int id, int mx, int my, int btn) { (void)mx; (void)my; (void)btn; if (id >= C_BTN && id < C_BTN + 25) calc_press(w->st, CALC_KEYS[(id - C_BTN) / 5][(id - C_BTN) % 5]); }
static void calc_key(Win *w, int key, int mods) {
    Calc *c = w->st; (void)mods;
    if (key == K_ENTER) calc_equals(c); else if (key == K_BS) calc_press(c, "Eff."); else if (key == K_ESC) calc_press(c, "C");
    else if (key >= 32 && key < 127) { char s[2] = { (char)key, 0 }; if (key == '*') calc_push(c, "×"); else if (key == '/') calc_push(c, "÷"); else if (key == '=') calc_equals(c); else calc_push(c, s); }
}
static void calc_draw(Win *w, int x, int y, int cw, int ch) {
    Calc *c = w->st; int pad = S(16), bw = (cw - pad * 2 - S(8) * 4) / 5, dh = S(112);
    card_bg(x + pad, y + pad, cw - pad * 2, dh);
    for (int i = 0; i < c->nh && i < 2; i++) { const char *hs = c->hist[c->nh - 1 - i]; int tw = text_w(F_UI, hs, S(12)); text_draw(F_UI, x + cw - pad - S(14) - tw, y + pad + S(8) + i * S(16), hs, TH.dim, S(12)); }
    int tw = text_w(F_UI, c->expr, S(26)); if (tw > cw - pad * 2 - S(28)) tw = cw - pad * 2 - S(28);
    gfx_clip(x + pad, y + pad, cw - pad * 2, dh);
    text_draw(F_UI, x + cw - pad - S(14) - text_w(F_UI, c->expr, S(26)), y + pad + S(44), c->expr, TH.text, S(26));
    gfx_unclip();
    if (c->res[0]) { int rw = text_w(F_UI, c->res, S(16)); text_draw(F_BOLD, x + cw - pad - S(14) - rw, y + pad + dh - S(30), c->res, TH.dim, S(16)); }
    int bh = (ch - pad * 2 - dh - S(16) - S(8) * 4) / 5; if (bh > S(56)) bh = S(56);
    int ty = y + pad + dh + S(16);
    for (int r = 0; r < 5; r++) for (int k = 0; k < 5; k++) {
        const char *l = CALC_KEYS[r][k]; int op = (k == 4) || !strcmp(l, "C") || !strcmp(l, "Eff.");
        ui_button(w, x + pad + k * (bw + S(8)), ty + r * (bh + S(8)), bw, bh, l, NULL, C_BTN + r * 5 + k, !strcmp(l, "="));
        (void)op;
    }
}

/* ================================================================== Image viewer */
typedef struct {
    char path[320]; Img *orig, *fit; int fw, fh; double zoom; int fitmode, ox, oy, drag, dx, dy;
    char files[64][128]; int nfiles, idx; unsigned long long fsize; char msg[96];
} View;
enum { V_PREV = 1, V_NEXT, V_ZIN, V_ZOUT, V_FIT, V_ONE, V_WALL };
static int is_img(const char *n) { size_t l = strlen(n); return (l > 4 && (!strcasecmp(n + l - 4, ".png") || !strcasecmp(n + l - 4, ".jpg"))) || (l > 5 && !strcasecmp(n + l - 5, ".jpeg")); }
static int cmp_str(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void view_load(Win *w, const char *path) {
    View *v = w->st; img_free(v->orig); v->orig = img_load_file(path); img_free(v->fit); v->fit = NULL;
    snprintf(v->path, sizeof v->path, "%s", path); v->fitmode = 1; v->zoom = 1; v->ox = v->oy = 0; v->msg[0] = 0;
    struct stat st; v->fsize = stat(path, &st) == 0 ? (unsigned long long)st.st_size : 0;
    if (!v->orig) snprintf(v->msg, sizeof v->msg, "Image illisible");
    char dir[320]; snprintf(dir, sizeof dir, "%s", path); char *sl = strrchr(dir, '/'); const char *base = sl ? sl + 1 : path; if (sl) *sl = 0; else strcpy(dir, ".");
    v->nfiles = 0; DIR *d = opendir(dir); struct dirent *de;
    while (d && (de = readdir(d)) && v->nfiles < 64) if (de->d_name[0] != '.' && is_img(de->d_name)) snprintf(v->files[v->nfiles++], 128, "%s", de->d_name);
    if (d) closedir(d);
    qsort(v->files, (size_t)v->nfiles, 128, cmp_str);
    v->idx = 0; for (int i = 0; i < v->nfiles; i++) if (!strcmp(v->files[i], base)) v->idx = i;
    char t[160]; snprintf(t, sizeof t, "%s", base); set_title(w, t);
}
static void *view_create(Win *w) { (void)w; View *v = calloc(1, sizeof *v); v->fitmode = 1; v->zoom = 1; return v; }
static void view_destroy(Win *w) { View *v = w->st; img_free(v->orig); img_free(v->fit); free(v); }
static void view_open_arg(Win *w, const char *path) { view_load(w, path); }
static void view_step(Win *w, int d) {
    View *v = w->st; if (v->nfiles < 2) return;
    v->idx = (v->idx + d + v->nfiles) % v->nfiles;
    char dir[320], p[480]; snprintf(dir, sizeof dir, "%s", v->path); char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
    snprintf(p, sizeof p, "%s/%s", dir, v->files[v->idx]); view_load(w, p);
}
static void view_action(Win *w, int id, int mx, int my, int btn) {
    View *v = w->st; (void)mx; (void)my; (void)btn;
    if (id == V_PREV) view_step(w, -1); else if (id == V_NEXT) view_step(w, 1);
    else if (id == V_ZIN) { v->fitmode = 0; v->zoom *= 1.25; if (v->zoom > 8) v->zoom = 8; }
    else if (id == V_ZOUT) { v->fitmode = 0; v->zoom /= 1.25; if (v->zoom < 0.05) v->zoom = 0.05; }
    else if (id == V_FIT) { v->fitmode = 1; v->ox = v->oy = 0; } else if (id == V_ONE) { v->fitmode = 0; v->zoom = 1; v->ox = v->oy = 0; }
    else if (id == V_WALL && v->path[0]) { cfg_set("wallpaper", v->path); wallpaper_set_custom(v->path); toast("Fond d'écran défini"); }
}
static void view_mouse(Win *w, int type, int x, int y, int arg) {
    View *v = w->st;
    if (type == M_WHEEL) { v->fitmode = 0; v->zoom *= arg > 0 ? 0.88 : 1.14; if (v->zoom < 0.05) v->zoom = 0.05; if (v->zoom > 8) v->zoom = 8; }
    else if (type == M_DOWN && y > S(44)) { v->drag = 1; v->dx = x - v->ox; v->dy = y - v->oy; }
    else if (type == M_MOVE && v->drag) { v->ox = x - v->dx; v->oy = y - v->dy; }
    else if (type == M_UP) v->drag = 0;
}
static void view_key(Win *w, int key, int mods) {
    (void)mods;
    if (key == K_LEFT) view_step(w, -1); else if (key == K_RIGHT) view_step(w, 1);
    else if (key == '+' || key == '=') view_action(w, V_ZIN, 0, 0, 1); else if (key == '-') view_action(w, V_ZOUT, 0, 0, 1);
    else if (key == '0' || key == 'f' || key == 'F') view_action(w, V_FIT, 0, 0, 1); else if (key == '1') view_action(w, V_ONE, 0, 0, 1);
}
static void view_draw(Win *w, int x, int y, int cw, int ch) {
    View *v = w->st; int top = S(44), sbar = S(28);
    gfx_fill(x, y, cw, ch, TH.dark ? RGB(12, 12, 13) : RGB(228, 228, 230), 255);
    gfx_fill(x, y, cw, top, TH.panel, 255); gfx_hline(x, y + top - 1, cw, TH.line, 255);
    ui_button(w, x + S(10), y + S(6), S(36), S(32), NULL, "back", V_PREV, 0); ui_button(w, x + S(50), y + S(6), S(36), S(32), NULL, "forward", V_NEXT, 0);
    ui_button(w, x + S(98), y + S(6), S(36), S(32), NULL, "zoom-out", V_ZOUT, 0); ui_button(w, x + S(138), y + S(6), S(36), S(32), NULL, "zoom-in", V_ZIN, 0);
    ui_button(w, x + S(182), y + S(6), S(72), S(32), "Ajuster", NULL, V_FIT, v->fitmode); ui_button(w, x + S(258), y + S(6), S(56), S(32), "1:1", NULL, V_ONE, 0);
    ui_button(w, x + cw - S(190), y + S(6), S(180), S(32), "Fond d'écran", "image", V_WALL, 0);
    int ax = x, ay = y + top, aw = cw, ah = ch - top - sbar;
    if (v->orig) {
        double z = v->zoom; if (v->fitmode) { z = fmin((double)aw / v->orig->w, (double)ah / v->orig->h); if (z > 1) z = 1; v->zoom = z; }
        int dw = (int)(v->orig->w * z), dh = (int)(v->orig->h * z), px = ax + (aw - dw) / 2 + v->ox, py = ay + (ah - dh) / 2 + v->oy;
        gfx_clip(ax, ay, aw, ah);
        if (z <= 1.0) { if (!v->fit || v->fw != dw || v->fh != dh) { img_free(v->fit); v->fit = img_scale(v->orig, dw > 0 ? dw : 1, dh > 0 ? dh : 1); v->fw = dw; v->fh = dh; } img_draw(v->fit, px, py, 255); }
        else {                                       /* zoom > 100 %: sample only what is visible */
            int x0 = px < ax ? ax : px, x1 = px + dw > ax + aw ? ax + aw : px + dw, y0 = py < ay ? ay : py, y1 = py + dh > ay + ah ? ay + ah : py + dh;
            for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { int sx = (int)((xx - px) / z), sy = (int)((yy - py) / z); if (sx >= v->orig->w) sx = v->orig->w - 1; if (sy >= v->orig->h) sy = v->orig->h - 1; u32 p = v->orig->px[(size_t)sy * v->orig->w + sx]; gfx_fill(xx, yy, 1, 1, p & 0xffffff, (int)(p >> 24)); }
        }
        gfx_unclip();
    } else text_draw(F_UI, x + S(24), y + top + S(24), v->path[0] ? "Impossible de lire cette image" : "Ouvrez une image depuis le gestionnaire de fichiers", TH.dim, S(14));
    int sy = y + ch - sbar; gfx_fill(x, sy, cw, sbar, TH.panel, 255); gfx_hline(x, sy, cw, TH.line, 255);
    char st[260], sz[32]; human_size(v->fsize, sz, sizeof sz);
    snprintf(st, sizeof st, "%dx%d  ·  %s  ·  %.0f %%  ·  %d/%d", v->orig ? v->orig->w : 0, v->orig ? v->orig->h : 0, sz, v->zoom * 100, v->nfiles ? v->idx + 1 : 0, v->nfiles);
    text_draw(F_UI, x + S(12), sy + (sbar - font_line_h(F_UI, S(12))) / 2, st, TH.dim, S(12));
}

/* ================================================================== Clock: calendar, stopwatch, timer */
static long timer_end;              /* monotonic ms when the timer fires, 0 = idle */
static int timer_secs = 300;
static long sw_start, sw_acc; static int sw_run; static long laps[8]; static int nlaps;
void clock_timers_tick(void) {
    if (timer_end && now_ms() >= timer_end) { timer_end = 0; toast("Minuteur terminé"); }
}
typedef struct { int tab, year, mon, day; } Clk;
enum { K_TAB0 = 1, K_PM = 10, K_NM, K_SW, K_LAP, K_RESET, K_TSTART, K_TP1 = 30, K_TPLUS = 40, K_TMINUS };
static void *clk_create(Win *w) { (void)w; Clk *c = calloc(1, sizeof *c); time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt); c->year = lt.tm_year + 1900; c->mon = lt.tm_mon; c->day = lt.tm_mday; return c; }
static void clk_destroy(Win *w) { free(w->st); }
static void clk_action(Win *w, int id, int mx, int my, int btn) {
    Clk *c = w->st; (void)mx; (void)my; (void)btn;
    if (id >= K_TAB0 && id < K_TAB0 + 3) c->tab = id - K_TAB0;
    else if (id == K_PM) { if (--c->mon < 0) { c->mon = 11; c->year--; } } else if (id == K_NM) { if (++c->mon > 11) { c->mon = 0; c->year++; } }
    else if (id == K_SW) { if (sw_run) { sw_acc += now_ms() - sw_start; sw_run = 0; } else { sw_start = now_ms(); sw_run = 1; } }
    else if (id == K_LAP) { if (sw_run && nlaps < 8) laps[nlaps++] = sw_acc + now_ms() - sw_start; }
    else if (id == K_RESET) { sw_run = 0; sw_acc = 0; nlaps = 0; }
    else if (id == K_TSTART) { if (timer_end) timer_end = 0; else timer_end = now_ms() + (long)timer_secs * 1000L; }
    else if (id >= K_TP1 && id < K_TP1 + 4) { static const int P[4] = { 60, 300, 600, 900 }; timer_secs = P[id - K_TP1]; timer_end = 0; }
    else if (id == K_TPLUS) { timer_secs += 60; } else if (id == K_TMINUS) { if (timer_secs > 60) timer_secs -= 60; }
}
static int clk_tick(Win *w) { Clk *c = w->st; return (c->tab == 1 && sw_run) || (c->tab == 2 && timer_end) || c->tab == 0; }
static void fmt_ms(long ms, char *o, size_t n) { snprintf(o, n, "%02ld:%02ld.%02ld", ms / 60000, (ms / 1000) % 60, (ms / 10) % 100); }
static void clk_draw(Win *w, int x, int y, int cw, int ch) {
    Clk *c = w->st; int pad = S(20);
    const char *tabs[] = { "Calendrier", "Chronomètre", "Minuteur" }; const char *ic[] = { "calendar", "clock", "timer" };
    for (int i = 0; i < 3; i++) ui_tab(w, x + pad + i * S(150), y + S(10), S(144), S(36), tabs[i], ic[i], K_TAB0 + i, c->tab == i);
    int cy = y + S(64);
    if (c->tab == 0) {
        static const char *MN[] = { "Janvier", "Février", "Mars", "Avril", "Mai", "Juin", "Juillet", "Août", "Septembre", "Octobre", "Novembre", "Décembre" };
        char h[48]; snprintf(h, sizeof h, "%s %d", MN[c->mon], c->year);
        text_draw(F_BOLD, x + pad, cy, h, TH.text, S(22));
        ui_button(w, x + cw - pad - S(84), cy - S(4), S(38), S(34), NULL, "chevron-left", K_PM, 0); ui_button(w, x + cw - pad - S(42), cy - S(4), S(38), S(34), NULL, "chevron-right", K_NM, 0);
        int gw = (cw - pad * 2) / 7, gy = cy + S(48);
        static const char *DN[] = { "lun", "mar", "mer", "jeu", "ven", "sam", "dim" };
        for (int i = 0; i < 7; i++) text_draw(F_UI, x + pad + i * gw + (gw - text_w(F_UI, DN[i], S(12))) / 2, gy - S(22), DN[i], TH.dim, S(12));
        struct tm first = { 0 }; first.tm_year = c->year - 1900; first.tm_mon = c->mon; first.tm_mday = 1; first.tm_hour = 12; mktime(&first);
        int off = (first.tm_wday + 6) % 7, dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (c->mon == 1 && ((c->year % 4 == 0 && c->year % 100 != 0) || c->year % 400 == 0)) dim[1] = 29;
        time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt);
        int rowh = (ch - (gy - y) - pad) / 6; if (rowh > S(56)) rowh = S(56);
        for (int d = 1; d <= dim[c->mon]; d++) {
            int cell = off + d - 1, col = cell % 7, row = cell / 7, dx0 = x + pad + col * gw, dy0 = gy + row * rowh;
            int today = lt.tm_year + 1900 == c->year && lt.tm_mon == c->mon && lt.tm_mday == d;
            if (today) gfx_rrect(dx0 + S(4), dy0 + S(2), gw - S(8), rowh - S(4), S(8), TH.accent, 255);
            char n[8]; snprintf(n, sizeof n, "%d", d); int tw = text_w(today ? F_BOLD : F_UI, n, S(15));
            text_draw(today ? F_BOLD : F_UI, dx0 + (gw - tw) / 2, dy0 + (rowh - font_line_h(F_UI, S(15))) / 2, n, today ? RGB(20, 20, 21) : TH.text, S(15));
        }
    } else if (c->tab == 1) {
        long ms = sw_acc + (sw_run ? now_ms() - sw_start : 0); char t[24]; fmt_ms(ms, t, sizeof t);
        int tw = text_w(F_UI, t, S(64)); text_draw(F_UI, x + (cw - tw) / 2, cy + S(10), t, TH.text, S(64));
        int bx = x + (cw - S(380)) / 2;
        ui_button(w, bx, cy + S(100), S(120), S(40), sw_run ? "Arrêter" : "Démarrer", sw_run ? "pause" : "run", K_SW, 1); ui_button(w, bx + S(130), cy + S(100), S(120), S(40), "Tour", "plus", K_LAP, 0); ui_button(w, bx + S(260), cy + S(100), S(120), S(40), "Zéro", "refresh", K_RESET, 0);
        for (int i = 0; i < nlaps; i++) { char l[48]; fmt_ms(laps[i], t, sizeof t); snprintf(l, sizeof l, "Tour %d      %s", i + 1, t); text_draw(F_MONO, bx, cy + S(164) + i * S(22), l, TH.dim, S(14)); }
    } else {
        long left = timer_end ? (timer_end - now_ms() + 999) / 1000 : timer_secs; if (left < 0) left = 0;
        char t[16]; snprintf(t, sizeof t, "%02ld:%02ld", left / 60, left % 60);
        int tw = text_w(F_UI, t, S(72)); text_draw(F_UI, x + (cw - tw) / 2, cy + S(6), t, timer_end ? TH.accent : TH.text, S(72));
        if (timer_end) ui_progress(x + pad + S(40), cy + S(104), cw - pad * 2 - S(80), S(8), (int)(100 - 100 * left / (timer_secs ? timer_secs : 1)), TH.accent);
        static const char *PL[4] = { "1 min", "5 min", "10 min", "15 min" }; int bx = x + (cw - S(4) * S(92)) / 2;
        for (int i = 0; i < 4; i++) ui_button(w, bx + i * S(100), cy + S(130), S(92), S(36), PL[i], NULL, K_TP1 + i, 0);
        ui_button(w, bx, cy + S(180), S(60), S(36), "−1", "minus", K_TMINUS, 0); ui_button(w, bx + S(70), cy + S(180), S(60), S(36), "+1", "plus", K_TPLUS, 0);
        ui_button(w, bx + S(140), cy + S(180), S(150), S(36), timer_end ? "Annuler" : "Démarrer", timer_end ? "x-circle" : "run", K_TSTART, !timer_end);
    }
}

App app_notes = { "notes", "Notes", "notes", "Éditeur de texte avec annuler et recherche", 860, 600, nt_create, nt_destroy, nt_draw, nt_action, nt_mouse, nt_key, NULL, nt_open_arg };
App app_calc = { "calc", "Calculatrice", "calculator", "Calculs, fonctions et historique", 380, 560, calc_create, calc_destroy, calc_draw, calc_action, NULL, calc_key, NULL };
App app_viewer = { "viewer", "Visionneuse", "image", "Images PNG et JPEG, zoom et diaporama", 900, 640, view_create, view_destroy, view_draw, view_action, view_mouse, view_key, NULL, view_open_arg };
App app_clock = { "clock", "Horloge", "clock", "Calendrier, chronomètre et minuteur", 640, 560, clk_create, clk_destroy, clk_draw, clk_action, NULL, NULL, clk_tick };

int miaou_calc(const char *src, char *out, size_t n) { double v; if (calc_eval(src, &v) != 0) return -1; calc_fmt(v, out, n); return 0; }
