/* Miaou Store: browse the local package catalogue and install / remove / launch .miaou packages
 * through the real miaou-get engine (output of the command is shown live). */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "desk.h"

typedef struct { char name[48], version[16], desc[120]; int installed; } Pkg;
typedef struct { Pkg p[24]; int n, sel; Job *job; char log[3000]; int scroll; char pending_done; } Store;
enum { ST_INSTALL = 1, ST_REMOVE, ST_RUN, ST_ROW = 100 };
#define REPO "/var/lib/miaou/repository"
#define DB "/var/lib/miaou/installed"

static int valid_name(const char *s) { if (!*s) return 0; for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '.' || *s == '_' || *s == '+' || (*s >= 'A' && *s <= 'Z'))) return 0; return 1; }
static void reload(Store *s) {
    s->n = 0;
    DIR *d = opendir(REPO); struct dirent *de;
    while (d && (de = readdir(d)) && s->n < 24) {
        size_t l = strlen(de->d_name); if (l < 10 || strcmp(de->d_name + l - 9, ".manifest")) continue;
        char path[300]; snprintf(path, sizeof path, REPO "/%s", de->d_name);
        FILE *f = fopen(path, "r"); if (!f) continue;
        Pkg *p = &s->p[s->n]; memset(p, 0, sizeof *p); char line[200];
        while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "name=", 5)) snprintf(p->name, sizeof p->name, "%s", line + 5);
            else if (!strncmp(line, "version=", 8)) snprintf(p->version, sizeof p->version, "%s", line + 8);
            else if (!strncmp(line, "description=", 12)) snprintf(p->desc, sizeof p->desc, "%s", line + 12); }
        fclose(f);
        if (valid_name(p->name)) s->n++;
    }
    if (d) closedir(d);
    FILE *f = fopen(DB, "r");
    if (f) { char line[300]; while (fgets(line, sizeof line, f)) { char *bar = strchr(line, '|'); if (!bar) continue; *bar = 0; for (int i = 0; i < s->n; i++) if (!strcmp(s->p[i].name, line)) s->p[i].installed = 1; } fclose(f); }
}
static void *store_create(Win *w) { (void)w; Store *s = calloc(1, sizeof *s); reload(s); snprintf(s->log, sizeof s->log, "Sélectionnez un paquet pour l'installer.\n"); return s; }
static void store_destroy(Win *w) { Store *s = w->st; job_free(s->job); free(s); }
static void run(Store *s, const char *cmd, const char *title) {
    if (s->job && !s->job->done) { toast("Opération déjà en cours"); return; }
    job_free(s->job); s->job = job_start(cmd); snprintf(s->log, sizeof s->log, "$ %s\n", title);
}
static void store_action(Win *w, int id, int mx, int my, int btn) {
    Store *s = w->st; (void)mx; (void)my; (void)btn; char cmd[320];
    if (id >= ST_ROW) { s->sel = id - ST_ROW; return; }
    if (s->sel < 0 || s->sel >= s->n) return;
    Pkg *p = &s->p[s->sel];
    if ((id == ST_INSTALL || id == ST_REMOVE) && !session_is_admin()) { toast("Réservé aux administrateurs"); return; }
    if (id == ST_INSTALL) { snprintf(cmd, sizeof cmd, "miaou-get install /usr/share/miaou/packages/%s.miaou 2>&1", p->name); run(s, cmd, cmd); }
    else if (id == ST_REMOVE) { snprintf(cmd, sizeof cmd, "miaou-get remove %s 2>&1", p->name); run(s, cmd, cmd); }
    else if (id == ST_RUN) { snprintf(cmd, sizeof cmd, "/opt/miaou/packages/%s/usr/bin/%s 2>&1", p->name, p->name); run(s, cmd, cmd); }
}
static int store_tick(Win *w) {
    Store *s = w->st; if (!s->job) return 0;
    size_t before = s->job->len;
    if (!job_poll(s->job)) return 0;
    size_t l = strlen(s->log), room = sizeof s->log - 1 - l, add = s->job->len - before; if (add > room) add = room;
    memcpy(s->log + l, s->job->buf + before, add); s->log[l + add] = 0;
    if (s->job->done) { snprintf(s->log + strlen(s->log), 40, "\n[terminé : code %d]", s->job->status); reload(s); toast(s->job->status == 0 ? "Opération réussie" : "L'opération a échoué"); }
    return 1;
}
static void store_draw(Win *w, int x, int y, int cw, int ch) {
    Store *s = w->st; int pad = S(18), px = S(14);
    text_draw(F_BOLD, x + pad, y + pad, "Miaou Store", TH.text, S(22));
    text_draw(F_UI, x + pad, y + pad + S(32), "Catalogue local · installation avec miaou-get (isolée dans /opt/miaou/packages)", TH.dim, px - 1);
    int ly = y + pad + S(68), rowh = S(78), listh = ch - S(68) - pad - S(170);
    int vis = listh / rowh;
    gfx_clip(x, ly, cw, listh);
    for (int i = 0; i < s->n && i < vis + 1; i++) {
        Pkg *p = &s->p[i]; int ry = ly + i * rowh, sel = i == s->sel;
        gfx_rrect(x + pad, ry, cw - pad * 2, rowh - S(8), S(12), sel ? TH.sel : TH.panel, 255);
        gfx_rrect_line(x + pad, ry, cw - pad * 2, rowh - S(8), S(12), sel ? TH.accent : TH.line, 255);
        gfx_rrect(x + pad + S(14), ry + S(12), S(44), S(44), S(10), TH.btn, 255); Img *ic = img_get("package", S(24)); if (ic) img_draw(ic, x + pad + S(24), ry + S(22), 255);
        char t[100]; snprintf(t, sizeof t, "%s", p->name); text_draw(F_BOLD, x + pad + S(72), ry + S(10), t, TH.text, S(16));
        text_draw(F_UI, x + pad + S(80) + text_w(F_BOLD, t, S(16)), ry + S(13), p->version, TH.dim, px - 2);
        text_draw_clip(F_UI, x + pad + S(72), ry + S(36), p->desc, TH.dim, px - 1, cw - pad * 2 - S(300));
        const char *st = p->installed ? "Installé" : "Disponible";
        int bw = text_w(F_BOLD, st, S(12)) + S(20), bx = x + cw - pad - S(24) - bw;
        gfx_rrect_line(bx, ry + S(20), bw, S(26), S(13), p->installed ? TH.good : TH.line, 255);
        text_draw(F_BOLD, bx + S(10), ry + S(25), st, p->installed ? TH.good : TH.dim, S(12));
        zone(w, x + pad, ry, cw - pad * 2, rowh - S(8), ST_ROW + i);
    }
    if (!s->n) text_draw(F_UI, x + pad, ly + S(10), "Le catalogue est vide.", TH.dim, px);
    gfx_unclip();
    int by = y + ch - S(170) - S(6) + S(0);
    int busy = s->job && !s->job->done;
    if (s->sel >= 0 && s->sel < s->n) {
        Pkg *p = &s->p[s->sel];
        if (!p->installed) ui_button(w, x + pad, by, S(150), S(38), busy ? "En cours…" : "Installer", "install", ST_INSTALL, 1);
        else { ui_button(w, x + pad, by, S(150), S(38), "Lancer", "run", ST_RUN, 1); ui_button(w, x + pad + S(160), by, S(150), S(38), "Désinstaller", "trash", ST_REMOVE, 0); }
    }
    int lgy = by + S(48), lgh = y + ch - lgy - pad;
    gfx_rrect(x + pad, lgy, cw - pad * 2, lgh, S(10), RGB(17, 13, 32), 255);
    gfx_clip(x + pad + S(8), lgy + S(4), cw - pad * 2 - S(16), lgh - S(8));
    int yy = lgy + S(8), lh = S(16), total = 1; for (const char *q = s->log; *q; q++) if (*q == '\n') total++;
    int v2 = (lgh - S(12)) / lh, skip = total > v2 ? total - v2 : 0, ln = 0;
    const char *q = s->log;
    while (*q) { const char *e = strchr(q, '\n'); size_t l = e ? (size_t)(e - q) : strlen(q); if (ln >= skip) { char b[200]; if (l > 190) l = 190; memcpy(b, q, l); b[l] = 0; text_draw(F_MONO, x + pad + S(14), yy, b, RGB(210, 205, 235), S(12)); yy += lh; } ln++; if (!e) break; q = e + 1; }
    gfx_unclip();
}

App app_store = { "store", "Boutique", "store", "Installer des paquets .miaou", 780, 640, store_create, store_destroy, store_draw, store_action, NULL, NULL, store_tick };
