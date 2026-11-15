/* Files: browse, preview (text / images), create, rename and delete. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

typedef struct { char name[128]; int dir; unsigned long long size; time_t mtime; } Ent;
typedef struct {
    char cwd[512];
    Ent *e; int n, sel, scroll, last_row;
    char *prev; size_t prevn; Img *img; char prev_name[128]; int prev_dir;
    int mode;                   /* 0 none, 1 mkdir, 2 rename, 3 delete?, 4 newfile, 5 goto */
    char input[128];
    char msg[160];
} FS;

enum { A_UP = 1, A_REFRESH, A_MKDIR, A_NEWFILE, A_RENAME, A_DELETE, A_GOTO, A_OK, A_CANCEL, A_PLACE = 20, A_ROW = 1000 };
static const struct { const char *name, *path, *icon; } PLACES[] = {
    { "Accueil", "/root", "home" }, { "Racine", "/", "drive" }, { "Applications", "/opt/miaou/packages", "package" },
    { "Système", "/etc", "folder-system" }, { "Temporaire", "/tmp", "folder" }, { "Matériel", "/sys", "display" } };

static int cmp_ent(const void *a, const void *b) {
    const Ent *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}
static void load_preview(FS *f) {
    free(f->prev); f->prev = NULL; f->prevn = 0; img_free(f->img); f->img = NULL; f->prev_name[0] = 0;
    if (f->sel < 0 || f->sel >= f->n) return;
    Ent *e = &f->e[f->sel];
    snprintf(f->prev_name, sizeof f->prev_name, "%s", e->name); f->prev_dir = e->dir;
    if (e->dir) return;
    char path[700]; snprintf(path, sizeof path, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", e->name);
    size_t l = strlen(e->name);
    if (l > 4 && (!strcasecmp(e->name + l - 4, ".png") || !strcasecmp(e->name + l - 4, ".jpg") || (l > 5 && !strcasecmp(e->name + l - 5, ".jpeg")))) {
        Img *im = img_load_file(path);
        if (im) { int mw = S(300), mh = S(220); int nw = im->w, nh = im->h; if (nw > mw) { nh = nh * mw / nw; nw = mw; } if (nh > mh) { nw = nw * mh / nh; nh = mh; } f->img = img_scale(im, nw > 0 ? nw : 1, nh > 0 ? nh : 1); img_free(im); return; }
    }
    f->prev = read_file(path, &f->prevn, 6000);
}
static void load_dir(Win *w, FS *f, const char *path) {
    char real[PATH_MAX];
    if (!realpath(path, real)) { snprintf(f->msg, sizeof f->msg, "Dossier introuvable : %s", path); return; }
    DIR *d = opendir(real);
    if (!d) { snprintf(f->msg, sizeof f->msg, "Accès refusé : %s", real); return; }
    free(f->e); f->e = NULL; f->n = 0;
    int cap = 64; f->e = malloc((size_t)cap * sizeof(Ent));
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (f->n >= cap) { cap *= 2; f->e = realloc(f->e, (size_t)cap * sizeof(Ent)); }
        Ent *e = &f->e[f->n++];
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
        char full[800]; snprintf(full, sizeof full, "%s/%s", strcmp(real, "/") ? real : "", de->d_name);
        struct stat st; e->dir = 0; e->size = 0; e->mtime = 0;
        if (stat(full, &st) == 0) { e->dir = S_ISDIR(st.st_mode); e->size = (unsigned long long)st.st_size; e->mtime = st.st_mtime; }
    }
    closedir(d);
    qsort(f->e, (size_t)f->n, sizeof(Ent), cmp_ent);
    snprintf(f->cwd, sizeof f->cwd, "%s", real);
    f->sel = -1; f->scroll = 0; f->msg[0] = 0;
    load_preview(f);
    char t[64]; const char *base = strrchr(real, '/'); snprintf(t, sizeof t, "Fichiers — %s", base && base[1] ? base + 1 : "/");
    set_title(w, t);
}
static void *files_create(Win *w) { FS *f = calloc(1, sizeof *f); f->last_row = -1; load_dir(w, f, "/root"); return f; }
static void files_destroy(Win *w) { FS *f = w->st; free(f->e); free(f->prev); img_free(f->img); free(f); }

static void go_parent(Win *w, FS *f) { char p[520]; snprintf(p, sizeof p, "%s/..", f->cwd); load_dir(w, f, p); }
static void open_entry(Win *w, FS *f, int i) {
    if (i < 0 || i >= f->n) return;
    if (f->e[i].dir) { char p[800]; snprintf(p, sizeof p, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", f->e[i].name); load_dir(w, f, p); }
    else { f->sel = i; load_preview(f); }
}
static void do_prompt(Win *w, FS *f) {
    char path[800], to[800];
    snprintf(path, sizeof path, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", f->mode == 2 && f->sel >= 0 ? f->e[f->sel].name : f->input);
    if (f->mode == 1) { if (mkdir(path, 0755) < 0) snprintf(f->msg, sizeof f->msg, "mkdir : %s", strerror(errno)); else toast("Dossier créé : %s", f->input); }
    else if (f->mode == 4) { FILE *fp = fopen(path, "wx"); if (!fp) snprintf(f->msg, sizeof f->msg, "Création impossible : %s", strerror(errno)); else { fclose(fp); toast("Fichier créé : %s", f->input); } }
    else if (f->mode == 2 && f->sel >= 0) { snprintf(to, sizeof to, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", f->input); if (rename(path, to) < 0) snprintf(f->msg, sizeof f->msg, "Renommage : %s", strerror(errno)); else toast("Renommé en %s", f->input); }
    else if (f->mode == 3 && f->sel >= 0) {
        snprintf(path, sizeof path, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", f->e[f->sel].name);
        int r = f->e[f->sel].dir ? rmdir(path) : unlink(path);
        if (r < 0) snprintf(f->msg, sizeof f->msg, "Suppression : %s", errno == ENOTEMPTY ? "dossier non vide" : strerror(errno)); else toast("Supprimé");
    }
    else if (f->mode == 5) { f->mode = 0; load_dir(w, f, f->input); return; }
    f->mode = 0; f->input[0] = 0;
    char keep[512]; snprintf(keep, sizeof keep, "%s", f->cwd); char m[160]; snprintf(m, sizeof m, "%s", f->msg);
    load_dir(w, f, keep); if (m[0]) snprintf(f->msg, sizeof f->msg, "%s", m);
}

static void files_action(Win *w, int id, int mx, int my, int btn) {
    FS *f = w->st; (void)mx; (void)my; (void)btn;
    if (id >= A_ROW) { f->sel = id - A_ROW; f->last_row = f->sel; load_preview(f); return; }
    if (id >= A_PLACE && id < A_PLACE + 10) { load_dir(w, f, PLACES[id - A_PLACE].path); return; }
    switch (id) {
    case A_UP: go_parent(w, f); break;
    case A_REFRESH: { char k[512]; snprintf(k, sizeof k, "%s", f->cwd); load_dir(w, f, k); } break;
    case A_MKDIR: f->mode = 1; f->input[0] = 0; break;
    case A_NEWFILE: f->mode = 4; f->input[0] = 0; break;
    case A_RENAME: if (f->sel >= 0) { f->mode = 2; snprintf(f->input, sizeof f->input, "%s", f->e[f->sel].name); } break;
    case A_DELETE: if (f->sel >= 0) f->mode = 3; break;
    case A_GOTO: f->mode = 5; snprintf(f->input, sizeof f->input, "%s", f->cwd); break;
    case A_OK: do_prompt(w, f); break;
    case A_CANCEL: f->mode = 0; break;
    }
}
static void files_mouse(Win *w, int type, int x, int y, int dbl) {
    FS *f = w->st; (void)x; (void)y;
    if (type == M_DOWN && dbl && f->last_row >= 0 && f->last_row == f->sel) { open_entry(w, f, f->sel); f->last_row = -1; }
    if (type == M_WHEEL) { f->scroll += -dbl * 3; if (f->scroll < 0) f->scroll = 0; }
}
static void files_key(Win *w, int key, int mods) {
    FS *f = w->st; (void)mods;
    if (f->mode) {
        int n = (int)strlen(f->input);
        if (key == K_ESC) f->mode = 0;
        else if (key == K_ENTER) do_prompt(w, f);
        else if (key == K_BS) { if (n) { while (n > 0 && (f->input[n - 1] & 0xc0) == 0x80) n--; if (n) n--; f->input[n] = 0; } }
        else if (f->mode != 3 && key >= 32 && key < 0x200000 && n < 100) { n += utf8_put(f->input + n, key); f->input[n] = 0; }
        else if (f->mode == 3 && (key == 'o' || key == 'y')) do_prompt(w, f);
        return;
    }
    switch (key) {
    case K_DOWN: if (f->sel < f->n - 1) { f->sel++; load_preview(f); } break;
    case K_UP: if (f->sel > 0) { f->sel--; load_preview(f); } break;
    case K_ENTER: open_entry(w, f, f->sel); break;
    case K_BS: go_parent(w, f); break;
    case K_DELETE: if (f->sel >= 0) f->mode = 3; break;
    case K_F1 + 1: files_action(w, A_RENAME, 0, 0, 1); break;
    case K_F1 + 4: files_action(w, A_REFRESH, 0, 0, 1); break;
    case 'l': if (mods & MOD_CTRL) files_action(w, A_GOTO, 0, 0, 1); break;
    case K_HOME: f->sel = f->n ? 0 : -1; load_preview(f); break;
    case K_END: f->sel = f->n - 1; load_preview(f); break;
    }
}

static void files_draw(Win *w, int x, int y, int cw, int ch) {
    FS *f = w->st;
    int side = S(176), tb = S(52), px = S(14), rowh = S(32);
    gfx_fill(x, y, side, ch, TH.panel, 255);
    gfx_vline(x + side, y, ch, TH.line, 255);
    text_draw(F_UI, x + S(16), y + S(12), "Lieux", TH.dim, S(12));
    for (int i = 0; i < 6; i++) {
        int sel = !strcmp(f->cwd, PLACES[i].path);
        ui_tab(w, x + S(8), y + S(34) + i * S(38), side - S(16), S(34), PLACES[i].name, PLACES[i].icon, A_PLACE + i, sel);
    }
    int bx = x + side + S(10);
    ui_button(w, bx, y + S(8), S(40), S(36), NULL, "back", A_UP, 0);
    ui_button(w, bx + S(46), y + S(8), S(40), S(36), NULL, "reload", A_REFRESH, 0);
    int fx = bx + S(94), tools = S(3) * S(46) + S(46);
    int fw = cw - side - (fx - x - side) - tools - S(14);
    gfx_rrect(fx, y + S(8), fw, S(36), S(8), TH.field, 255);
    gfx_rrect_line(fx, y + S(8), fw, S(36), S(8), TH.line, 255);
    gfx_clip(fx + S(4), y + S(8), fw - S(8), S(36));
    text_draw(F_UI, fx + S(12), y + S(8) + (S(36) - font_line_h(F_UI, px)) / 2, f->cwd, TH.text, px);
    gfx_unclip();
    zone(w, fx, y + S(8), fw, S(36), A_GOTO);
    int tx = fx + fw + S(8);
    ui_button(w, tx, y + S(8), S(40), S(36), NULL, "folder-new", A_MKDIR, 0);
    ui_button(w, tx + S(46), y + S(8), S(40), S(36), NULL, "note", A_NEWFILE, 0);
    ui_button(w, tx + S(92), y + S(8), S(40), S(36), NULL, "rename", A_RENAME, 0);
    ui_button(w, tx + S(138), y + S(8), S(40), S(36), NULL, "trash", A_DELETE, 0);
    gfx_hline(x + side + 1, y + tb, cw - side - 1, TH.line, 255);

    int prevw = cw - side > S(820) ? S(330) : 0;
    int lx = x + side + 1, lw = cw - side - 1 - prevw, ly = y + tb + S(28), bar = S(44), lh = ch - tb - S(28) - bar;
    int vis = lh / rowh;
    if (f->scroll > f->n - vis) f->scroll = f->n - vis; if (f->scroll < 0) f->scroll = 0;
    if (f->sel >= 0) { if (f->sel < f->scroll) f->scroll = f->sel; if (f->sel >= f->scroll + vis) f->scroll = f->sel - vis + 1; }
    text_draw(F_BOLD, lx + S(48), y + tb + S(6), "Nom", TH.dim, S(12));
    text_draw(F_BOLD, lx + lw - S(210), y + tb + S(6), "Taille", TH.dim, S(12));
    text_draw(F_BOLD, lx + lw - S(110), y + tb + S(6), "Modifié", TH.dim, S(12));
    gfx_clip(lx, ly, lw, lh);
    for (int i = 0; i < vis + 1 && f->scroll + i < f->n; i++) {
        int idx = f->scroll + i; Ent *e = &f->e[idx]; int ry = ly + i * rowh;
        int hot = hover(lx, ry, lw - S(10), rowh);
        if (idx == f->sel) gfx_rrect(lx + S(6), ry, lw - S(20), rowh - S(2), S(8), TH.sel, 255);
        else if (hot) gfx_rrect(lx + S(6), ry, lw - S(20), rowh - S(2), S(8), TH.btn, 160);
        Img *im = img_get(e->dir ? "folder" : "file", S(22)); if (im) img_draw(im, lx + S(14), ry + (rowh - S(22)) / 2, 255);
        text_draw_clip(idx == f->sel ? F_BOLD : F_UI, lx + S(48), ry + (rowh - font_line_h(F_UI, px)) / 2, e->name, TH.text, px, lw - S(270));
        char sbuf[32] = ""; if (!e->dir) human_size(e->size, sbuf, sizeof sbuf);
        text_draw(F_UI, lx + lw - S(210), ry + (rowh - font_line_h(F_UI, px - 1)) / 2, e->dir ? "—" : sbuf, TH.dim, px - 1);
        char dbuf[24] = ""; struct tm lt; if (localtime_r(&e->mtime, &lt)) strftime(dbuf, sizeof dbuf, "%d/%m/%y %H:%M", &lt);
        text_draw(F_UI, lx + lw - S(110), ry + (rowh - font_line_h(F_UI, px - 1)) / 2, dbuf, TH.dim, px - 1);
        zone(w, lx, ry, lw - S(10), rowh, A_ROW + idx);
    }
    if (!f->n) text_draw(F_UI, lx + S(20), ly + S(14), "Dossier vide", TH.dim, px);
    gfx_unclip();
    ui_scrollbar(lx + lw - S(10), ly, lh, f->n, vis, f->scroll);

    /* preview */
    if (prevw) {
        int pxx = x + cw - prevw;
        gfx_vline(pxx, y + tb, ch - tb, TH.line, 255);
        gfx_fill(pxx + 1, y + tb + 1, prevw - 1, ch - tb - 1, TH.panel, 120);
        int py = y + tb + S(14);
        if (f->prev_name[0]) {
            Img *im = img_get(f->prev_dir ? "folder" : "file", S(40)); if (im) img_draw(im, pxx + S(16), py, 255);
            text_draw_clip(F_BOLD, pxx + S(66), py + S(2), f->prev_name, TH.text, S(15), prevw - S(80));
            char sb[32] = "dossier"; if (!f->prev_dir && f->sel >= 0) human_size(f->e[f->sel].size, sb, sizeof sb);
            text_draw(F_UI, pxx + S(66), py + S(22), sb, TH.dim, S(12));
            py += S(56);
            if (f->img) img_draw(f->img, pxx + S(16), py, 255);
            else if (f->prev) {
                gfx_clip(pxx + S(8), py, prevw - S(16), ch - tb - S(86));
                int lh2 = S(16), cy = py, cols = (prevw - S(32)) / font_cell_w(F_MONO, S(12));
                char *p = f->prev;
                while (*p && cy < y + ch) {
                    char line[128]; int k = 0;
                    while (*p && *p != '\n' && k < cols && k < 120) { unsigned char c = (unsigned char)*p; line[k++] = (c < 32 && c != '\t') ? '.' : (char)c; p++; }
                    line[k] = 0; if (*p == '\n') p++;
                    text_draw(F_MONO, pxx + S(16), cy, line, TH.text, S(12)); cy += lh2;
                }
                gfx_unclip();
            } else if (!f->prev_dir) text_draw(F_UI, pxx + S(16), py, "Aucun aperçu disponible", TH.dim, S(13));
        } else text_draw(F_UI, pxx + S(16), py, "Sélectionnez un fichier", TH.dim, S(13));
    }
    /* status / prompt bar */
    int by = y + ch - bar;
    gfx_hline(x + side + 1, by, cw - side - 1, TH.line, 255);
    if (f->mode) {
        const char *lab = f->mode == 1 ? "Nouveau dossier :" : f->mode == 4 ? "Nouveau fichier :" : f->mode == 2 ? "Renommer en :" : f->mode == 5 ? "Aller à :" : "Supprimer ?";
        int lw2 = text_w(F_BOLD, lab, px) + S(10);
        text_draw(F_BOLD, lx + S(10), by + (bar - font_line_h(F_BOLD, px)) / 2, lab, TH.text, px);
        if (f->mode == 3) { text_draw(F_UI, lx + S(10) + lw2, by + (bar - font_line_h(F_UI, px)) / 2, f->sel >= 0 ? f->e[f->sel].name : "", TH.warn, px); lw2 += text_w(F_UI, f->sel >= 0 ? f->e[f->sel].name : "", px) + S(10); }
        else ui_field(lx + S(10) + lw2, by + S(6), S(260), bar - S(12), f->input, 1, NULL);
        int ox = lx + S(10) + lw2 + (f->mode == 3 ? 0 : S(270));
        ui_button(w, ox, by + S(6), S(84), bar - S(12), f->mode == 3 ? "Supprimer" : "OK", NULL, A_OK, 1);
        ui_button(w, ox + S(92), by + S(6), S(84), bar - S(12), "Annuler", NULL, A_CANCEL, 0);
    } else {
        char st[200]; snprintf(st, sizeof st, "%s", f->msg);
        if (!f->msg[0]) snprintf(st, sizeof st, "%d élément(s)  ·  F2 renommer  ·  Suppr effacer  ·  Ctrl+L aller à…", f->n);
        text_draw(F_UI, lx + S(12), by + (bar - font_line_h(F_UI, px - 1)) / 2, st, f->msg[0] ? TH.warn : TH.dim, px - 1);
    }
}

App app_files = { "files", "Fichiers", "files", "Parcourir, prévisualiser et ranger", 960, 600, files_create, files_destroy, files_draw, files_action, files_mouse, files_key, NULL };
