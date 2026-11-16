/* Files: browse, select several items, copy/cut/paste, trash and restore, hidden files, sort, filter, properties, archives. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

typedef struct { char name[128]; int dir; unsigned long long size; time_t mtime; mode_t mode; uid_t uid; } Ent;
typedef struct {
    char cwd[512];
    Ent *e; int n; unsigned char *selv; int sel, anchor, scroll, last_row;
    char *prev; size_t prevn; Img *img; char prev_name[128]; int prev_dir;
    int mode;                   /* 0 none, 1 mkdir, 2 rename, 3 confirm permanent delete, 4 new file, 5 goto, 6 filter */
    char input[128], filter[64], msg[160];
    int hidden, sortk, in_trash;
} FS;

enum { A_UP = 1, A_REFRESH, A_MKDIR, A_NEWFILE, A_RENAME, A_DELETE, A_GOTO, A_OK, A_CANCEL, A_COPY, A_CUT, A_PASTE, A_HIDDEN, A_ARCHIVE, A_RESTORE, A_EMPTY, A_SORT0, A_SORT1, A_SORT2,
       A_PLACE = 30, A_ROW = 1000 };
static const char *PLACE_NAMES[] = { "Accueil", "Bureau", "Documents", "Images", "Corbeille", "Racine" };
static const char *PLACE_ICONS[] = { "home", "folder", "file", "image", "trash", "drive" };
static void place_path(int i, char *out, size_t n) {
    switch (i) { case 0: snprintf(out, n, "%s", cur_home); break; case 1: snprintf(out, n, "%s/Bureau", cur_home); break; case 2: snprintf(out, n, "%s/Documents", cur_home); break;
                 case 3: snprintf(out, n, "%s/Images", cur_home); break; case 4: snprintf(out, n, "%s/.local/share/Trash/files", cur_home); break; default: snprintf(out, n, "/"); }
}
static int sortk_g;
static int cmp_ent(const void *a, const void *b) {
    const Ent *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    if (sortk_g == 1 && x->size != y->size) return x->size < y->size ? 1 : -1;
    if (sortk_g == 2 && x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;
    return strcasecmp(x->name, y->name);
}
static int selcount(FS *f) { int c = 0; for (int i = 0; i < f->n; i++) if (f->selv[i]) c++; return c; }
static void join(FS *f, const char *name, char *out, size_t n) { snprintf(out, n, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", name); }
static void clear_sel(FS *f) { memset(f->selv, 0, (size_t)(f->n + 1)); }
static void load_preview(FS *f) {
    free(f->prev); f->prev = NULL; f->prevn = 0; img_free(f->img); f->img = NULL; f->prev_name[0] = 0;
    if (f->sel < 0 || f->sel >= f->n) return;
    Ent *e = &f->e[f->sel];
    snprintf(f->prev_name, sizeof f->prev_name, "%s", e->name); f->prev_dir = e->dir;
    if (e->dir) return;
    char path[700]; join(f, e->name, path, sizeof path);
    size_t l = strlen(e->name);
    if (l > 4 && (!strcasecmp(e->name + l - 4, ".png") || !strcasecmp(e->name + l - 4, ".jpg") || (l > 5 && !strcasecmp(e->name + l - 5, ".jpeg")))) {
        Img *im = img_load_file(path);
        if (im) { int mw = S(300), mh = S(200); int nw = im->w, nh = im->h; if (nw > mw) { nh = nh * mw / nw; nw = mw; } if (nh > mh) { nw = nw * mh / nh; nh = mh; } f->img = img_scale(im, nw > 0 ? nw : 1, nh > 0 ? nh : 1); img_free(im); return; }
    }
    f->prev = read_file(path, &f->prevn, 4000);
}
static void load_dir(Win *w, FS *f, const char *path) {
    char real[PATH_MAX];
    if (!realpath(path, real)) { snprintf(f->msg, sizeof f->msg, "Dossier introuvable : %s", path); return; }
    DIR *d = opendir(real);
    if (!d) { snprintf(f->msg, sizeof f->msg, "Accès refusé : %s", real); return; }
    if (strcmp(real, f->cwd)) f->filter[0] = 0;
    free(f->e); f->e = NULL; f->n = 0; free(f->selv);
    int cap = 64; f->e = malloc((size_t)cap * sizeof(Ent));
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (de->d_name[0] == '.' && !f->hidden) continue;
        if (f->filter[0] && !strcasestr(de->d_name, f->filter)) continue;
        if (f->n >= cap) { cap *= 2; f->e = realloc(f->e, (size_t)cap * sizeof(Ent)); }
        Ent *e = &f->e[f->n++];
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
        char full[800]; snprintf(full, sizeof full, "%s/%s", strcmp(real, "/") ? real : "", de->d_name);
        struct stat st; e->dir = 0; e->size = 0; e->mtime = 0; e->mode = 0; e->uid = 0;
        if (stat(full, &st) == 0) { e->dir = S_ISDIR(st.st_mode); e->size = (unsigned long long)st.st_size; e->mtime = st.st_mtime; e->mode = st.st_mode; e->uid = st.st_uid; }
    }
    closedir(d);
    sortk_g = f->sortk; qsort(f->e, (size_t)f->n, sizeof(Ent), cmp_ent);
    snprintf(f->cwd, sizeof f->cwd, "%s", real);
    f->selv = calloc((size_t)f->n + 1, 1);
    f->sel = -1; f->anchor = -1; f->scroll = 0; f->msg[0] = 0;
    f->in_trash = strstr(real, "/.local/share/Trash/files") != NULL;
    load_preview(f);
    char t[64]; const char *base = strrchr(real, '/'); snprintf(t, sizeof t, "Fichiers — %s", base && base[1] ? base + 1 : "/");
    set_title(w, t);
}
static void reload(Win *w, FS *f) { char k[512]; snprintf(k, sizeof k, "%s", f->cwd); load_dir(w, f, k); }
static void *files_create(Win *w) { FS *f = calloc(1, sizeof *f); f->last_row = -1; load_dir(w, f, cur_home); return f; }
static void files_open_arg(Win *w, const char *path) { FS *f = w->st; struct stat st; if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) load_dir(w, f, path); }
static void files_destroy(Win *w) { FS *f = w->st; free(f->e); free(f->selv); free(f->prev); img_free(f->img); free(f); }
static void go_parent(Win *w, FS *f) { char p[520]; snprintf(p, sizeof p, "%s/..", f->cwd); load_dir(w, f, p); }
static void open_entry(Win *w, FS *f, int i) {
    if (i < 0 || i >= f->n) return;
    char p[800]; join(f, f->e[i].name, p, sizeof p);
    if (f->e[i].dir) load_dir(w, f, p); else if (f->in_trash) { f->sel = i; load_preview(f); } else open_path(p);
}
static int collect(FS *f, char out[][512]) { int n = 0; for (int i = 0; i < f->n && n < 32; i++) if (f->selv[i]) { char p[700]; join(f, f->e[i].name, p, sizeof p); snprintf(out[n++], 512, "%s", p); } return n; }
static int is_archive(const char *n, int *kind) {
    size_t l = strlen(n);
    if ((l > 7 && !strcasecmp(n + l - 7, ".tar.gz")) || (l > 4 && !strcasecmp(n + l - 4, ".tgz"))) { *kind = 1; return 1; }
    if (l > 4 && !strcasecmp(n + l - 4, ".tar")) { *kind = 2; return 1; }
    if (l > 4 && !strcasecmp(n + l - 4, ".zip")) { *kind = 3; return 1; }
    return 0;
}
static void do_archive(Win *w, FS *f) {
    char paths[32][512]; int n = collect(f, paths); if (!n) { snprintf(f->msg, sizeof f->msg, "Sélectionnez des éléments à archiver ou une archive à extraire."); return; }
    int kind = 0;
    if (n == 1 && is_archive(f->e[f->sel >= 0 ? f->sel : 0].name, &kind)) {                  /* extract next to the archive */
        char base[200], dest[700]; snprintf(base, sizeof base, "%s", f->e[f->sel].name); char *dot = strstr(base, ".t"); if (!dot) dot = strrchr(base, '.'); if (dot) *dot = 0;
        fo_unique(f->cwd, base, dest, sizeof dest); fo_mkdir(dest);
        int r;
        if (kind == 3) { char *a[] = { "unzip", "-o", "-q", paths[0], "-d", dest, NULL }; r = fo_run(a); }
        else { char *a[] = { "tar", kind == 1 ? "-xzf" : "-xf", paths[0], "-C", dest, NULL }; r = fo_run(a); }
        toast(r == 0 ? "Archive extraite" : "Extraction impossible"); reload(w, f); return;
    }
    char dest[700]; fo_unique(f->cwd, "Archive.tar.gz", dest, sizeof dest);
    char *argv[40]; int k = 0; argv[k++] = "tar"; argv[k++] = "-czf"; argv[k++] = dest; argv[k++] = "-C"; argv[k++] = f->cwd;
    char names[32][130]; for (int i = 0, c = 0; i < f->n && c < 32; i++) if (f->selv[i]) { snprintf(names[c], 130, "%s", f->e[i].name); argv[k++] = names[c++]; }
    argv[k] = NULL; int r = fo_run(argv); toast(r == 0 ? "Archive créée" : "Archivage impossible"); reload(w, f);
}
static void do_prompt(Win *w, FS *f) {
    char path[800], to[800];
    if (f->mode == 6) { snprintf(f->filter, sizeof f->filter, "%s", f->input); f->mode = 0; reload(w, f); return; }
    if (f->mode == 5) { f->mode = 0; load_dir(w, f, f->input); return; }
    int sel = f->sel >= 0 ? f->sel : 0;
    snprintf(path, sizeof path, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", f->mode == 2 && f->n ? f->e[sel].name : f->input);
    if (f->mode == 1) { if (mkdir(path, 0755) < 0) snprintf(f->msg, sizeof f->msg, "mkdir : %s", strerror(errno)); else toast("Dossier créé : %s", f->input); }
    else if (f->mode == 4) { FILE *fp = fopen(path, "wx"); if (!fp) snprintf(f->msg, sizeof f->msg, "Création impossible : %s", strerror(errno)); else { fclose(fp); toast("Fichier créé : %s", f->input); } }
    else if (f->mode == 2 && f->n) { snprintf(to, sizeof to, "%s/%s", strcmp(f->cwd, "/") ? f->cwd : "", f->input); if (rename(path, to) < 0) snprintf(f->msg, sizeof f->msg, "Renommage : %s", strerror(errno)); else toast("Renommé en %s", f->input); }
    else if (f->mode == 3) {                                   /* permanent deletion (only inside the trash) */
        int c = 0; for (int i = 0; i < f->n; i++) if (f->selv[i]) { char p[800]; join(f, f->e[i].name, p, sizeof p); char *a[] = { "rm", "-rf", p, NULL }; if (fo_run(a) == 0) c++; char ip[900]; snprintf(ip, sizeof ip, "%s/.local/share/Trash/info/%s.trashinfo", cur_home, f->e[i].name); unlink(ip); }
        toast("%d élément(s) supprimé(s) définitivement", c);
    }
    f->mode = 0; f->input[0] = 0;
    char m[160]; snprintf(m, sizeof m, "%s", f->msg); reload(w, f); if (m[0]) snprintf(f->msg, sizeof f->msg, "%s", m);
}
static void select_row(FS *f, int i, int mods) {
    if (i < 0 || i >= f->n) return;
    if (mods & MOD_CTRL) { f->selv[i] = !f->selv[i]; f->anchor = i; }
    else if ((mods & MOD_SHIFT) && f->anchor >= 0) { clear_sel(f); int a = f->anchor < i ? f->anchor : i, b = f->anchor < i ? i : f->anchor; for (int k = a; k <= b; k++) f->selv[k] = 1; }
    else { clear_sel(f); f->selv[i] = 1; f->anchor = i; }
    f->sel = i; load_preview(f);
}
static void files_action(Win *w, int id, int mx, int my, int btn) {
    FS *f = w->st; (void)mx; (void)my; (void)btn; char paths[32][512]; int n;
    if (id >= A_ROW) { f->last_row = id - A_ROW; select_row(f, id - A_ROW, input_mods()); return; }
    if (id >= A_PLACE && id < A_PLACE + 6) { char p[400]; place_path(id - A_PLACE, p, sizeof p); fo_mkdir_p(p); load_dir(w, f, p); return; }
    switch (id) {
    case A_UP: go_parent(w, f); break;
    case A_REFRESH: reload(w, f); break;
    case A_MKDIR: f->mode = 1; f->input[0] = 0; break;
    case A_NEWFILE: f->mode = 4; f->input[0] = 0; break;
    case A_RENAME: if (f->sel >= 0) { f->mode = 2; snprintf(f->input, sizeof f->input, "%s", f->e[f->sel].name); } break;
    case A_DELETE:
        if (!selcount(f)) break;
        if (f->in_trash) f->mode = 3;
        else { int c = 0; for (int i = 0; i < f->n; i++) if (f->selv[i]) { char p[800]; join(f, f->e[i].name, p, sizeof p); if (fo_trash(p) == 0) c++; } toast("%d élément(s) mis à la corbeille", c); reload(w, f); }
        break;
    case A_GOTO: f->mode = 5; snprintf(f->input, sizeof f->input, "%s", f->cwd); break;
    case A_OK: do_prompt(w, f); break;
    case A_CANCEL: f->mode = 0; break;
    case A_COPY: case A_CUT: n = collect(f, paths); if (n) { fo_clip_set(paths, n, id == A_CUT); toast("%d élément(s) %s", n, id == A_CUT ? "coupé(s)" : "copié(s)"); } break;
    case A_PASTE: { int c = fo_paste(f->cwd); toast("%d élément(s) collé(s)", c); reload(w, f); } break;
    case A_HIDDEN: f->hidden = !f->hidden; reload(w, f); break;
    case A_ARCHIVE: do_archive(w, f); break;
    case A_RESTORE: { int c = 0; for (int i = 0; i < f->n; i++) if (f->selv[i] && fo_restore(f->e[i].name) == 0) c++; toast("%d élément(s) restauré(s)", c); reload(w, f); } break;
    case A_EMPTY: fo_trash_empty(); toast("Corbeille vidée"); reload(w, f); break;
    case A_SORT0: case A_SORT1: case A_SORT2: f->sortk = id - A_SORT0; reload(w, f); break;
    }
}
static void files_mouse(Win *w, int type, int x, int y, int arg) {
    FS *f = w->st; (void)x; (void)y;
    if (type == M_DOWN && arg && f->last_row >= 0 && f->last_row == f->sel) { open_entry(w, f, f->sel); f->last_row = -1; }
    if (type == M_WHEEL) { f->scroll += -arg * 3; if (f->scroll < 0) f->scroll = 0; }
}
static void files_key(Win *w, int key, int mods) {
    FS *f = w->st;
    if (f->mode) {
        int n = (int)strlen(f->input);
        if (key == K_ESC) { f->mode = 0; if (f->filter[0] || f->input[0]) {} }
        else if (key == K_ENTER) do_prompt(w, f);
        else if (key == K_BS) { if (n) { while (n > 0 && (f->input[n - 1] & 0xc0) == 0x80) n--; if (n) n--; f->input[n] = 0; if (f->mode == 6) { snprintf(f->filter, sizeof f->filter, "%s", f->input); reload(w, f); f->mode = 6; } } }
        else if (f->mode != 3 && key >= 32 && key < 0x200000 && n < 100) { n += utf8_put(f->input + n, key); f->input[n] = 0; if (f->mode == 6) { snprintf(f->filter, sizeof f->filter, "%s", f->input); int m6 = f->mode; reload(w, f); f->mode = m6; } }
        else if (f->mode == 3 && (key == 'o' || key == 'y')) do_prompt(w, f);
        return;
    }
    int ct = mods & MOD_CTRL;
    if (ct) {
        switch (key) {
        case 'c': case 'C': files_action(w, A_COPY, 0, 0, 1); return; case 'x': case 'X': files_action(w, A_CUT, 0, 0, 1); return; case 'v': case 'V': files_action(w, A_PASTE, 0, 0, 1); return;
        case 'a': case 'A': for (int i = 0; i < f->n; i++) f->selv[i] = 1; return; case 'h': case 'H': files_action(w, A_HIDDEN, 0, 0, 1); return;
        case 'f': case 'F': f->mode = 6; snprintf(f->input, sizeof f->input, "%s", f->filter); return; case 'l': case 'L': files_action(w, A_GOTO, 0, 0, 1); return;
        default: break;
        }
    }
    switch (key) {
    case K_DOWN: if (f->sel < f->n - 1) select_row(f, f->sel + 1, mods); break;
    case K_UP: if (f->sel > 0) select_row(f, f->sel - 1, mods); break;
    case K_ENTER: open_entry(w, f, f->sel); break;
    case K_BS: go_parent(w, f); break;
    case K_DELETE: if (mods & MOD_SHIFT) { if (selcount(f)) f->mode = 3; } else files_action(w, A_DELETE, 0, 0, 1); break;
    case K_F1 + 1: files_action(w, A_RENAME, 0, 0, 1); break;
    case K_F1 + 4: files_action(w, A_REFRESH, 0, 0, 1); break;
    case K_ESC: if (f->filter[0]) { f->filter[0] = 0; reload(w, f); } else clear_sel(f); break;
    case K_HOME: if (f->n) select_row(f, 0, 0); break;
    case K_END: if (f->n) select_row(f, f->n - 1, 0); break;
    case '/': f->mode = 6; snprintf(f->input, sizeof f->input, "%s", f->filter); break;
    }
}

static void perm_str(mode_t m, char *o) { const char *r = "rwxrwxrwx"; for (int i = 0; i < 9; i++) o[i] = (m & (1 << (8 - i))) ? r[i] : '-'; o[9] = 0; }
static void files_draw(Win *w, int x, int y, int cw, int ch) {
    FS *f = w->st;
    int side = S(176), tb = S(52), px = S(14), rowh = S(32);
    gfx_fill(x, y, side, ch, TH.panel, 255);
    gfx_vline(x + side, y, ch, TH.line, 255);
    text_draw(F_UI, x + S(16), y + S(12), "Lieux", TH.dim, S(12));
    for (int i = 0; i < 6; i++) { char p[400]; place_path(i, p, sizeof p); ui_tab(w, x + S(8), y + S(34) + i * S(38), side - S(16), S(34), PLACE_NAMES[i], PLACE_ICONS[i], A_PLACE + i, !strcmp(f->cwd, p)); }
    int bx = x + side + S(10), bs = S(36), gap = S(4);
    ui_button(w, bx, y + S(8), bs, bs, NULL, "back", A_UP, 0); ui_button(w, bx + bs + gap, y + S(8), bs, bs, NULL, "reload", A_REFRESH, 0);
    int tools = 9, tw2 = tools * (bs + gap);
    int fx = bx + 2 * (bs + gap) + S(4), fw = cw - side - (fx - x - side) - tw2 - S(14);
    gfx_rrect(fx, y + S(8), fw, bs, S(6), TH.field, 255); gfx_rrect_line(fx, y + S(8), fw, bs, S(6), TH.line, 255);
    gfx_clip(fx + S(4), y + S(8), fw - S(8), bs);
    text_draw(F_UI, fx + S(12), y + S(8) + (bs - font_line_h(F_UI, px)) / 2, f->cwd, TH.text, px);
    gfx_unclip();
    zone(w, fx, y + S(8), fw, bs, A_GOTO);
    int tx = fx + fw + S(8);
    static const struct { const char *ic; int id; } TOOLS[9] = { { "folder-new", A_MKDIR }, { "note", A_NEWFILE }, { "copy", A_COPY }, { "cut", A_CUT }, { "paste", A_PASTE }, { "rename", A_RENAME }, { "trash", A_DELETE }, { "archive", A_ARCHIVE }, { "eye", A_HIDDEN } };
    for (int i = 0; i < tools; i++) ui_button(w, tx + i * (bs + gap), y + S(8), bs, bs, NULL, TOOLS[i].ic, TOOLS[i].id, TOOLS[i].id == A_HIDDEN && f->hidden);
    gfx_hline(x + side + 1, y + tb, cw - side - 1, TH.line, 255);

    int prevw = cw - side > S(820) ? S(330) : 0;
    int lx = x + side + 1, lw = cw - side - 1 - prevw, ly = y + tb + S(28), bar = S(44), lh = ch - tb - S(28) - bar;
    int vis = lh / rowh;
    if (f->scroll > f->n - vis) f->scroll = f->n - vis; if (f->scroll < 0) f->scroll = 0;
    if (f->sel >= 0) { if (f->sel < f->scroll) f->scroll = f->sel; if (f->sel >= f->scroll + vis) f->scroll = f->sel - vis + 1; }
    static const char *HN[3] = { "Nom", "Taille", "Modifié" }; int hx[3] = { lx + S(48), lx + lw - S(210), lx + lw - S(110) };
    for (int i = 0; i < 3; i++) { text_draw(f->sortk == i ? F_BOLD : F_UI, hx[i], y + tb + S(6), HN[i], f->sortk == i ? TH.text : TH.dim, S(12)); zone(w, hx[i] - S(4), y + tb + S(2), text_w(F_UI, HN[i], S(12)) + S(8), S(24), A_SORT0 + i); }
    gfx_clip(lx, ly, lw, lh);
    for (int i = 0; i < vis + 1 && f->scroll + i < f->n; i++) {
        int idx = f->scroll + i; Ent *e = &f->e[idx]; int ry = ly + i * rowh;
        int hot = hover(lx, ry, lw - S(10), rowh);
        if (f->selv[idx]) gfx_rrect(lx + S(6), ry, lw - S(20), rowh - S(2), S(6), TH.sel, 255);
        else if (hot) gfx_rrect(lx + S(6), ry, lw - S(20), rowh - S(2), S(6), TH.btn, 160);
        Img *im = img_get(e->dir ? "folder" : (strcasestr(e->name, ".png") || strcasestr(e->name, ".jpg")) ? "image" : "file", S(20)); if (im) img_draw(im, lx + S(14), ry + (rowh - S(20)) / 2, e->name[0] == '.' ? 140 : 255);
        text_draw_clip(f->selv[idx] ? F_BOLD : F_UI, lx + S(48), ry + (rowh - font_line_h(F_UI, px)) / 2, e->name, e->name[0] == '.' ? TH.dim : TH.text, px, lw - S(270));
        char sbuf[32] = ""; if (!e->dir) human_size(e->size, sbuf, sizeof sbuf);
        text_draw(F_UI, lx + lw - S(210), ry + (rowh - font_line_h(F_UI, px - 1)) / 2, e->dir ? "—" : sbuf, TH.dim, px - 1);
        char dbuf[24] = ""; struct tm lt; if (localtime_r(&e->mtime, &lt)) strftime(dbuf, sizeof dbuf, "%d/%m/%y %H:%M", &lt);
        text_draw(F_UI, lx + lw - S(110), ry + (rowh - font_line_h(F_UI, px - 1)) / 2, dbuf, TH.dim, px - 1);
        zone(w, lx, ry, lw - S(10), rowh, A_ROW + idx);
    }
    if (!f->n) text_draw(F_UI, lx + S(20), ly + S(14), f->filter[0] ? "Aucun résultat pour ce filtre" : "Dossier vide", TH.dim, px);
    gfx_unclip();
    ui_scrollbar(lx + lw - S(10), ly, lh, f->n, vis, f->scroll);

    if (prevw) {                                                  /* preview + properties */
        int pxx = x + cw - prevw;
        gfx_vline(pxx, y + tb, ch - tb, TH.line, 255);
        gfx_fill(pxx + 1, y + tb + 1, prevw - 1, ch - tb - 1, TH.panel, 120);
        int py = y + tb + S(14);
        if (f->prev_name[0] && f->sel >= 0) {
            Ent *e = &f->e[f->sel];
            Img *im = img_get(f->prev_dir ? "folder" : "file", S(32)); if (im) img_draw(im, pxx + S(16), py, 255);
            text_draw_clip(F_BOLD, pxx + S(60), py + S(2), f->prev_name, TH.text, S(15), prevw - S(76));
            char sb[32] = "Dossier"; if (!f->prev_dir) human_size(e->size, sb, sizeof sb);
            text_draw(F_UI, pxx + S(60), py + S(22), sb, TH.dim, S(12));
            py += S(48);
            char pm[16], line[120], dd[40] = ""; perm_str(e->mode, pm); struct tm lt; if (localtime_r(&e->mtime, &lt)) strftime(dd, sizeof dd, "%d/%m/%Y %H:%M", &lt);
            snprintf(line, sizeof line, "Droits %s  ·  uid %d", pm, (int)e->uid); text_draw(F_MONO, pxx + S(16), py, line, TH.dim, S(11)); py += S(16);
            snprintf(line, sizeof line, "Modifié le %s", dd); text_draw(F_MONO, pxx + S(16), py, line, TH.dim, S(11)); py += S(26);
            if (f->img) img_draw(f->img, pxx + S(16), py, 255);
            else if (f->prev) {
                gfx_clip(pxx + S(8), py, prevw - S(16), ch - tb - S(130));
                int lh2 = S(16), cy = py, cols = (prevw - S(32)) / font_cell_w(F_MONO, S(12));
                char *p = f->prev;
                while (*p && cy < y + ch) {
                    char lnb[128]; int k = 0;
                    while (*p && *p != '\n' && k < cols && k < 120) { unsigned char c = (unsigned char)*p; lnb[k++] = (c < 32 && c != '\t') ? '.' : (char)c; p++; }
                    lnb[k] = 0; if (*p == '\n') p++;
                    text_draw(F_MONO, pxx + S(16), cy, lnb, TH.text, S(12)); cy += lh2;
                }
                gfx_unclip();
            }
        } else text_draw(F_UI, pxx + S(16), py, "Sélectionnez un fichier", TH.dim, S(13));
    }
    int by = y + ch - bar;
    gfx_hline(x + side + 1, by, cw - side - 1, TH.line, 255);
    if (f->mode) {
        const char *lab = f->mode == 1 ? "Nouveau dossier :" : f->mode == 4 ? "Nouveau fichier :" : f->mode == 2 ? "Renommer en :" : f->mode == 5 ? "Aller à :" : f->mode == 6 ? "Filtrer :" : "Supprimer définitivement ?";
        int lw2 = text_w(F_BOLD, lab, px) + S(10);
        text_draw(F_BOLD, lx + S(10), by + (bar - font_line_h(F_BOLD, px)) / 2, lab, TH.text, px);
        int ox = lx + S(10) + lw2;
        if (f->mode != 3) { ui_field(ox, by + S(6), S(260), bar - S(12), f->input, 1, NULL); ox += S(270); }
        ui_button(w, ox, by + S(6), S(96), bar - S(12), f->mode == 3 ? "Supprimer" : "OK", NULL, A_OK, 1);
        ui_button(w, ox + S(104), by + S(6), S(84), bar - S(12), "Annuler", NULL, A_CANCEL, 0);
    } else {
        char st[260];
        struct statvfs sv; char free_s[32] = ""; if (statvfs(f->cwd, &sv) == 0) human_size((unsigned long long)sv.f_bavail * sv.f_frsize, free_s, sizeof free_s);
        int sc = selcount(f);
        if (f->msg[0]) snprintf(st, sizeof st, "%s", f->msg);
        else if (sc) snprintf(st, sizeof st, "%d sélectionné(s) sur %d  ·  %s libre", sc, f->n, free_s);
        else snprintf(st, sizeof st, "%d élément(s)%s%s  ·  %s libre  ·  Ctrl+C/X/V  ·  / filtrer", f->n, f->hidden ? "  ·  cachés visibles" : "", f->filter[0] ? "  ·  filtré" : "", free_s);
        text_draw_clip(F_UI, lx + S(12), by + (bar - font_line_h(F_UI, px - 1)) / 2, st, f->msg[0] ? TH.warn : TH.dim, px - 1, lw - S(300));
        if (f->in_trash) { ui_button(w, x + cw - prevw - S(250), by + S(6), S(116), bar - S(12), "Restaurer", "undo", A_RESTORE, 1); ui_button(w, x + cw - prevw - S(126), by + S(6), S(116), bar - S(12), "Vider", "trash", A_EMPTY, 0); }
    }
}

App app_files = { "files", "Fichiers", "files", "Parcourir, copier, archiver et ranger", 980, 620, files_create, files_destroy, files_draw, files_action, files_mouse, files_key, NULL, files_open_arg };
