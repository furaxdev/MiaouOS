/* Settings: appearance, display, keyboard, date and time, accounts, security, shortcuts, system. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

enum { TABS = 8 };
typedef struct {
    int tab, field, sel_user, new_admin;
    char pw1[48], pw2[48], nname[28], ndisp[40], ntest[64], host[40], msg[140];
    Img *thumb[2]; int thumb_w;
    Job *job; char log[900];
} Set;
enum { A_TAB = 1, A_THEME_D = 20, A_THEME_L, A_ACC = 30, A_AUTO = 40, A_ANIM, A_WALL_DEF, A_WALL_IMG, A_UI = 60, A_KFR = 70, A_KUS, A_TZ = 80, A_NTP = 95,
       A_USER = 100, A_FIELD = 120, A_SETPW = 130, A_ADD = 131, A_DEL = 132, A_ADMIN = 133, A_LOCK = 140, A_LOCKNOW = 150, A_DND = 151, A_HOST = 160 };
static const unsigned ACCENT_RGB[4] = { RGB(232, 163, 61), RGB(240, 120, 106), RGB(111, 160, 240), RGB(141, 184, 148) };
static const struct { const char *name, *tz; } ZONES[] = { { "UTC", "UTC0" }, { "Paris", "CET-1CEST,M3.5.0,M10.5.0/3" }, { "Londres", "GMT0BST,M3.5.0/1,M10.5.0" }, { "New York", "EST5EDT,M3.2.0,M11.1.0" },
    { "Los Angeles", "PST8PDT,M3.2.0,M11.1.0" }, { "Tokyo", "JST-9" }, { "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" }, { "Dubaï", "<+04>-4" } };
static const int LOCKS[] = { 0, 1, 5, 10, 30 };
static const int UIS[] = { 0, 100, 150, 200 };

static void *set_create(Win *w) { (void)w; Set *s = calloc(1, sizeof *s); gethostname(s->host, sizeof s->host - 1); return s; }
static void set_destroy(Win *w) { Set *s = w->st; img_free(s->thumb[0]); img_free(s->thumb[1]); job_free(s->job); free(s); }
static void set_open_arg(Win *w, const char *arg) { Set *s = w->st; if (!strncmp(arg, "tab:", 4)) { int t = atoi(arg + 4); if (t >= 0 && t < TABS) s->tab = t; } }
static void label(int x, int y, const char *t, u32 c, int px, int bold) { text_draw(bold ? F_BOLD : F_UI, x, y, t, c, px); }
static void card(int x, int y, int w, int h) { gfx_rrect(x, y, w, h, S(8), TH.panel, 255); gfx_rrect_line(x, y, w, h, S(8), TH.line, 255); }
static void toggle(Win *w, int x, int y, const char *text, int on, int id) {
    gfx_rrect(x, y, S(40), S(22), S(11), on ? TH.accent : TH.btn_hot, 255); gfx_circle(x + (on ? S(29) : S(11)), y + S(11), S(8), on ? RGB(20, 20, 21) : TH.dim, 255);
    label(x + S(52), y + S(2), text, TH.text, S(14), 0); zone(w, x, y - S(4), S(52) + text_w(F_UI, text, S(14)), S(30), id);
}
static void masked(char *out, size_t n, const char *in) { size_t k = 0; for (size_t i = 0; in[i] && k + 4 < n; i++) { memcpy(out + k, "•", 3); k += 3; } out[k] = 0; }

static void apply_ui_scale(int v) {
    char b[16]; if (v) snprintf(b, sizeof b, "%d", v); else snprintf(b, sizeof b, "auto"); cfg_set("ui", b);
    UIP = v ? v : ui_auto_scale(SW); desk_mode_changed();
}
static void set_action(Win *w, int id, int mx, int my, int btn) {
    Set *s = w->st; (void)mx; (void)my; (void)btn; s->msg[0] = 0;
    if (id >= A_TAB && id < A_TAB + TABS) { s->tab = id - A_TAB; s->field = 0; return; }
    if (id == A_THEME_D || id == A_THEME_L) { int want = id == A_THEME_D; if (want != theme_dark) { theme_dark = want; theme_apply(); cfg_set("theme", want ? "dark" : "light"); toast(want ? "Thème : Nuit féline" : "Thème : Jour pastel"); } }
    else if (id >= A_ACC && id < A_ACC + 4) { accent_idx = id - A_ACC; theme_apply(); char b[8]; snprintf(b, sizeof b, "%d", accent_idx); cfg_set("accent", b); }
    else if (id == A_AUTO) { cfg_set("autotheme", !strcmp(cfg_get("autotheme", "0"), "1") ? "0" : "1"); }
    else if (id == A_ANIM) { anim_enabled = !anim_enabled; cfg_set("anim", anim_enabled ? "1" : "0"); }
    else if (id == A_WALL_DEF) { cfg_set("wallpaper", ""); wallpaper_set_custom(""); toast("Fond d'écran par défaut"); }
    else if (id == A_WALL_IMG) { char d[300]; snprintf(d, sizeof d, "%s/Images", cur_home); open_app_arg("files", d); toast("Ouvrez une image, puis « Fond d'écran »"); }
    else if (id >= A_UI && id < A_UI + 4) apply_ui_scale(UIS[id - A_UI]);
    else if (id == A_KFR) { kbd_set_layout(1); toast("Clavier : Français (AZERTY)"); } else if (id == A_KUS) { kbd_set_layout(0); toast("Clavier : English (QWERTY)"); }
    else if (id >= A_TZ && id < A_TZ + 8) { cfg_set("tz", ZONES[id - A_TZ].tz); setenv("TZ", ZONES[id - A_TZ].tz, 1); tzset(); toast("Fuseau horaire : %s", ZONES[id - A_TZ].name); }
    else if (id == A_NTP) {
        if (!session_is_admin()) { snprintf(s->msg, sizeof s->msg, "Réservé aux administrateurs."); return; }
        if (s->job && !s->job->done) return;
        job_free(s->job); s->job = job_start("ntpd -n -q -p pool.ntp.org 2>&1; date 2>&1"); snprintf(s->log, sizeof s->log, "Synchronisation en cours…\n");
    }
    else if (id >= A_USER && id < A_USER + 16) { s->sel_user = id - A_USER; s->pw1[0] = s->pw2[0] = 0; }
    else if (id >= A_FIELD && id < A_FIELD + 8) s->field = id - A_FIELD + 1;
    else if (id == A_ADMIN) s->new_admin = !s->new_admin;
    else if (id == A_SETPW && s->sel_user < nusers) {
        User *u = &users[s->sel_user];
        if (!session_is_admin() && u != cur_user) snprintf(s->msg, sizeof s->msg, "Vous ne pouvez modifier que votre propre mot de passe.");
        else if (strcmp(s->pw1, s->pw2)) snprintf(s->msg, sizeof s->msg, "Les deux mots de passe sont différents.");
        else { user_setpw(u->name, s->pw1); snprintf(s->msg, sizeof s->msg, s->pw1[0] ? "Mot de passe de %s modifié." : "Mot de passe de %s supprimé (connexion libre).", u->display); s->pw1[0] = s->pw2[0] = 0; }
    }
    else if (id == A_ADD) {
        if (!session_is_admin()) { snprintf(s->msg, sizeof s->msg, "Réservé aux administrateurs."); return; }
        int r = user_add(s->nname, s->ndisp, s->pw1, s->new_admin);
        if (r == 0) { users_save(); snprintf(s->msg, sizeof s->msg, "Utilisateur « %s » créé.", s->nname); s->nname[0] = s->ndisp[0] = s->pw1[0] = s->pw2[0] = 0; }
        else snprintf(s->msg, sizeof s->msg, r == -2 ? "Nom invalide (a-z, 0-9, - et _)." : "Création impossible (nom vide, déjà pris ou limite atteinte).");
    }
    else if (id == A_DEL && s->sel_user < nusers) {
        if (!session_is_admin()) { snprintf(s->msg, sizeof s->msg, "Réservé aux administrateurs."); return; }
        char name[32]; snprintf(name, sizeof name, "%s", users[s->sel_user].name);
        if (user_del(name) == 0) { snprintf(s->msg, sizeof s->msg, "Utilisateur « %s » supprimé.", name); s->sel_user = 0; } else snprintf(s->msg, sizeof s->msg, "Impossible de supprimer la session en cours.");
    }
    else if (id >= A_LOCK && id < A_LOCK + 5) { char b[8]; snprintf(b, sizeof b, "%d", LOCKS[id - A_LOCK]); cfg_set("lock", b); apply_config(); }
    else if (id == A_LOCKNOW) session_lock();
    else if (id == A_DND) { cfg_set("dnd", !strcmp(cfg_get("dnd", "0"), "1") ? "0" : "1"); apply_config(); }
    else if (id == A_HOST) {
        if (!session_is_admin()) { snprintf(s->msg, sizeof s->msg, "Réservé aux administrateurs."); return; }
        if (s->host[0] && sethostname(s->host, strlen(s->host)) == 0) { FILE *f = fopen("/etc/hostname", "w"); if (f) { fprintf(f, "%s\n", s->host); fclose(f); } snprintf(s->msg, sizeof s->msg, "Nom de la machine : %s", s->host); }
        else snprintf(s->msg, sizeof s->msg, "Nom invalide.");
    }
}
static char *field_buf(Set *s, size_t *cap) {
    switch (s->field) {
    case 1: *cap = sizeof s->pw1; return s->pw1; case 2: *cap = sizeof s->pw2; return s->pw2;
    case 3: *cap = sizeof s->nname; return s->nname; case 4: *cap = sizeof s->ndisp; return s->ndisp;
    case 5: *cap = sizeof s->ntest; return s->ntest; case 6: *cap = sizeof s->host; return s->host; default: return NULL; }
}
static void set_key(Win *w, int key, int mods) {
    Set *s = w->st; (void)mods; size_t cap; char *b = field_buf(s, &cap); if (!b) return;
    size_t n = strlen(b);
    if (key == K_TAB) { s->field = s->field % 6 + 1; return; }
    if (key == K_BS) { if (n) { while (n > 0 && (b[n - 1] & 0xc0) == 0x80) n--; if (n) n--; b[n] = 0; } }
    else if (key >= 32 && key < 0x200000 && n + 5 < cap) { n += (size_t)utf8_put(b + n, key); b[n] = 0; }
}
static int set_tick(Win *w) {
    Set *s = w->st; int ch = 0;
    if (s->job) { size_t before = s->job->len; if (job_poll(s->job)) { snprintf(s->log, sizeof s->log, "%s", s->job->buf + (s->job->len > 700 ? s->job->len - 700 : 0)); (void)before; ch = 1; } }
    return ch || s->tab == 3 || s->tab == 7;
}
static void field(Win *w, Set *s, int fid, int x, int y, int fw, const char *text, const char *ph, int pass) {
    char buf[200]; if (pass) masked(buf, sizeof buf, text); else snprintf(buf, sizeof buf, "%s", text);
    ui_field(x, y, fw, S(38), buf, s->field == fid + 1, ph); zone(w, x, y, fw, S(38), A_FIELD + fid);
}
static void set_draw(Win *w, int x, int y, int cw, int ch) {
    Set *s = w->st; int side = S(200), px = S(14);
    gfx_fill(x, y, side, ch, TH.panel, 255); gfx_vline(x + side, y, ch, TH.line, 255);
    static const char *TN[TABS] = { "Apparence", "Affichage", "Clavier", "Date et heure", "Comptes", "Sécurité", "Raccourcis", "Système" };
    static const char *TI[TABS] = { "palette", "display", "keyboard", "clock", "users", "shield", "list", "info" };
    for (int i = 0; i < TABS; i++) ui_tab(w, x + S(8), y + S(10) + i * S(40), side - S(16), S(36), TN[i], TI[i], A_TAB + i, s->tab == i);
    int cx = x + side + S(28), cy = y + S(22), rw = cw - side - S(56);
    label(cx, cy, TN[s->tab], TH.text, S(22), 1);
    if (s->msg[0]) label(cx + S(200), cy + S(6), s->msg, TH.warn, S(13), 0);
    cy += S(44);
    if (s->tab == 0) {
        label(cx, cy, "Ambiance", TH.dim, S(12), 1);
        int tw = S(200), th = S(112);
        for (int i = 0; i < 2; i++) {
            if (!s->thumb[i] || s->thumb_w != SW) { img_free(s->thumb[i]); Img *wp = wallpaper(i == 0); s->thumb[i] = wp ? img_scale(wp, tw, th) : NULL; if (i == 1) s->thumb_w = SW; }
            int tx = cx + i * (tw + S(20)), ty = cy + S(24), sel = (i == 0) == theme_dark;
            gfx_rrect(tx - S(3), ty - S(3), tw + S(6), th + S(46), S(10), sel ? TH.accent : TH.line, 255); gfx_rrect(tx, ty, tw, th + S(40), S(8), TH.win, 255);
            if (s->thumb[i]) { gfx_clip(tx, ty, tw, th); img_draw(s->thumb[i], tx, ty, 255); gfx_unclip(); }
            label(tx + S(12), ty + th + S(10), i == 0 ? "Nuit féline" : "Jour pastel", TH.text, px, 1); zone(w, tx, ty, tw, th + S(40), i == 0 ? A_THEME_D : A_THEME_L);
        }
        int ay = cy + S(24) + th + S(64);
        label(cx, ay, "Couleur d'accent", TH.dim, S(12), 1);
        for (int i = 0; i < 4; i++) { int ax = cx + i * S(56); gfx_circle(ax + S(18), ay + S(40), S(16), ACCENT_RGB[i], 255); if (i == accent_idx) gfx_circle(ax + S(18), ay + S(40), S(6), RGB(20, 20, 21), 255); zone(w, ax, ay + S(20), S(36), S(40), A_ACC + i); }
        int ty2 = ay + S(80);
        toggle(w, cx, ty2, "Thème automatique : jour de 7 h à 19 h", !strcmp(cfg_get("autotheme", "0"), "1"), A_AUTO);
        toggle(w, cx, ty2 + S(36), "Animations", anim_enabled, A_ANIM);
        ui_button(w, cx, ty2 + S(80), S(210), S(36), "Fond d'écran par défaut", NULL, A_WALL_DEF, 0); ui_button(w, cx + S(220), ty2 + S(80), S(210), S(36), "Choisir une image…", "image", A_WALL_IMG, 0);
    } else if (s->tab == 1) {
        char info[140]; snprintf(info, sizeof info, "Écran actuel : %d × %d  ·  échelle de l'interface : %d %%", SW, SH, UIP); label(cx, cy, info, TH.text, px, 0);
        label(cx, cy + S(36), "Résolution", TH.dim, S(12), 1);
        label(cx, cy + S(58), "Fixée au démarrage par le pilote graphique (paramètre noyau video=LARGEUR×HAUTEUR).", TH.dim, S(13), 0);
        label(cx, cy + S(80), "Le changement à chaud n'est pas pris en charge par ce pilote : MiaouOS ne le simule pas.", TH.dim, S(13), 0);
        label(cx, cy + S(124), "Taille de l'interface", TH.dim, S(12), 1);
        const char *un[] = { "Auto", "100 %", "150 %", "200 %" };
        for (int i = 0; i < 4; i++) ui_button(w, cx + i * S(96), cy + S(148), S(88), S(38), un[i], NULL, A_UI + i, 0);
    } else if (s->tab == 2) {
        ui_button(w, cx, cy, S(230), S(44), "Français (AZERTY)", NULL, A_KFR, kbd_layout() == 1); ui_button(w, cx + S(240), cy, S(230), S(44), "English (QWERTY)", NULL, A_KUS, kbd_layout() == 0);
        label(cx, cy + S(72), "Zone de test", TH.dim, S(12), 1);
        ui_field(cx, cy + S(96), rw, S(44), s->ntest, s->field == 5, "Tapez ici pour tester le clavier"); zone(w, cx, cy + S(96), rw, S(44), A_FIELD + 4);
        label(cx, cy + S(164), "La disposition s'applique tout de suite, sans redémarrage. Le voyant Verr. Maj. s'affiche sur l'écran de connexion.", TH.dim, S(13), 0);
    } else if (s->tab == 3) {
        time_t now = time(NULL); struct tm lt; char hm[48] = "", d[64] = "";
        static const char *DJ[] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
        static const char *MJ[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre" };
        if (localtime_r(&now, &lt)) { strftime(hm, sizeof hm, "%H:%M:%S", &lt); snprintf(d, sizeof d, "%s %d %s %d", DJ[lt.tm_wday], lt.tm_mday, MJ[lt.tm_mon], lt.tm_year + 1900); }
        label(cx, cy, hm, TH.text, S(44), 0); label(cx, cy + S(58), d, TH.dim, px, 0);
        label(cx, cy + S(96), "Fuseau horaire", TH.dim, S(12), 1);
        const char *cur = cfg_get("tz", ""); int bx = cx, by = cy + S(120);
        for (int i = 0; i < 8; i++) { if (bx + S(150) > cx + rw) { bx = cx; by += S(44); } ui_button(w, bx, by, S(144), S(36), ZONES[i].name, NULL, A_TZ + i, !strcmp(cur, ZONES[i].tz)); bx += S(152); }
        by += S(64); ui_button(w, cx, by, S(260), S(38), "Synchroniser l'heure (NTP)", "refresh", A_NTP, 1);
        if (s->log[0]) { gfx_rrect(cx, by + S(52), rw, S(90), S(8), RGB(15, 15, 16), 255); gfx_clip(cx + S(8), by + S(56), rw - S(16), S(82)); int ly = by + S(60); const char *p = s->log; while (*p && ly < by + S(130)) { const char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p); char b[160]; if (l > 150) l = 150; memcpy(b, p, l); b[l] = 0; text_draw(F_MONO, cx + S(12), ly, b, RGB(210, 210, 214), S(12)); ly += S(16); if (!e) break; p = e + 1; } gfx_unclip(); }
    } else if (s->tab == 4) {
        int lw = S(240);
        for (int i = 0; i < nusers; i++) {
            int ry = cy + i * S(60), sel = i == s->sel_user;
            gfx_rrect(cx, ry, lw, S(54), S(8), sel ? TH.btn_hot : TH.panel, 255); gfx_rrect_line(cx, ry, lw, S(54), S(8), sel ? TH.accent : TH.line, 255);
            label(cx + S(14), ry + S(8), users[i].display, TH.text, px, 1); char sub[80]; snprintf(sub, sizeof sub, "%s  ·  %s%s", users[i].name, users[i].admin ? "administrateur" : "standard", &users[i] == cur_user ? "  ·  connecté" : "");
            label(cx + S(14), ry + S(29), sub, TH.dim, S(12), 0); zone(w, cx, ry, lw, S(54), A_USER + i);
        }
        int fx = cx + lw + S(24), fw = rw - lw - S(24); if (fw < S(260)) fw = S(260);
        if (s->sel_user >= nusers) s->sel_user = 0;
        label(fx, cy, "Mot de passe de", TH.dim, S(12), 1); label(fx + S(112), cy - S(1), users[s->sel_user].display, TH.text, px, 1);
        field(w, s, 0, fx, cy + S(24), fw, s->pw1, "Nouveau mot de passe (vide : aucun)", 1); field(w, s, 1, fx, cy + S(68), fw, s->pw2, "Confirmer", 1);
        ui_button(w, fx, cy + S(114), S(150), S(36), "Modifier", NULL, A_SETPW, 1); ui_button(w, fx + S(160), cy + S(114), S(150), S(36), "Supprimer le compte", "trash", A_DEL, 0);
        int ay = cy + S(178); label(fx, ay, "Nouvel utilisateur", TH.dim, S(12), 1);
        field(w, s, 2, fx, ay + S(24), fw, s->nname, "Identifiant (a-z, 0-9)", 0); field(w, s, 3, fx, ay + S(68), fw, s->ndisp, "Nom affiché", 0);
        toggle(w, fx, ay + S(116), "Administrateur", s->new_admin, A_ADMIN); ui_button(w, fx + S(190), ay + S(110), S(150), S(36), "Créer", "plus", A_ADD, 1);
        if (nusers * S(60) + S(10) > S(360)) { /* long list: keep the layout */ }
    } else if (s->tab == 5) {
        label(cx, cy, "Verrouillage automatique", TH.dim, S(12), 1);
        int cur = atoi(cfg_get("lock", "10")); const char *LL[] = { "Jamais", "1 min", "5 min", "10 min", "30 min" };
        for (int i = 0; i < 5; i++) ui_button(w, cx + i * S(104), cy + S(24), S(96), S(36), LL[i], NULL, A_LOCK + i, LOCKS[i] == cur);
        ui_button(w, cx, cy + S(84), S(230), S(40), "Verrouiller maintenant", "lock", A_LOCKNOW, 1);
        toggle(w, cx, cy + S(150), "Ne pas déranger (masquer les notifications)", !strcmp(cfg_get("dnd", "0"), "1"), A_DND);
        label(cx, cy + S(204), "Les mots de passe sont stockés sous forme d'empreintes SHA-256 salées.", TH.dim, S(13), 0);
        label(cx, cy + S(226), "Les comptes standard n'ont accès qu'à leurs fichiers ; leur terminal tourne avec leurs droits.", TH.dim, S(13), 0);
    } else if (s->tab == 6) {
        static const char *SC[][2] = { { "Super", "Lanceur d'applications" }, { "Super + Espace  ou  Alt + F2", "Exécuter : applications, calculs, commandes" }, { "Super + L", "Verrouiller la session" }, { "Super + A", "Centre de contrôle (calendrier, notifications)" },
            { "Super + D", "Afficher le bureau" }, { "Super + T / E / B", "Terminal / Fichiers / Navigateur" }, { "Super + flèches directionnelles", "Aimanter, agrandir, réduire la fenêtre" }, { "Alt + Tab", "Changer de fenêtre" }, { "Alt + glisser", "Déplacer une fenêtre depuis n'importe où" },
            { "Ctrl + Alt + gauche / droite", "Changer de bureau virtuel (Maj : emporter la fenêtre)" }, { "Alt + F4", "Fermer la fenêtre" }, { "Impr. écran", "Capture d'écran dans Images" }, { "Ctrl + Alt + T", "Nouveau terminal" }, { "Clic droit sur le bureau", "Nouveau dossier, fichier, coller…" } };
        for (int i = 0; i < 14; i++) { int ry = cy + i * S(32); label(cx, ry, SC[i][0], TH.text, S(13), 1); label(cx + S(270), ry, SC[i][1], TH.dim, S(13), 0); }
    } else {
        struct utsname u; uname(&u);
        char up[64] = "?", mem[96] = "?"; double upt = 0; FILE *f = fopen("/proc/uptime", "r"); if (f) { if (fscanf(f, "%lf", &upt) == 1) snprintf(up, sizeof up, "%d min %02d s", (int)upt / 60, (int)upt % 60); fclose(f); }
        unsigned long mt = 0, ma = 0; f = fopen("/proc/meminfo", "r"); if (f) { char l[128]; while (fgets(l, sizeof l, f)) { sscanf(l, "MemTotal: %lu", &mt); sscanf(l, "MemAvailable: %lu", &ma); } fclose(f); }
        snprintf(mem, sizeof mem, "%lu Mo libres sur %lu Mo", ma / 1024, mt / 1024);
        char sess[80]; snprintf(sess, sizeof sess, "%s (%s)", cur_user ? cur_user->display : "—", session_is_admin() ? "administrateur" : "standard");
        const char *rows[][2] = { { "Système", "MiaouOS 0.3" }, { "Noyau", u.release }, { "Architecture", u.machine }, { "Session", sess }, { "Temps de fonctionnement", up }, { "Mémoire", mem } };
        card(cx, cy - S(6), rw, 6 * S(36) + S(12));
        for (int i = 0; i < 6; i++) { label(cx + S(18), cy + i * S(36) + S(4), rows[i][0], TH.dim, px, 0); label(cx + S(230), cy + i * S(36) + S(4), rows[i][1], TH.text, px, 1); }
        int hy = cy + 6 * S(36) + S(30); label(cx, hy, "Nom de la machine", TH.dim, S(12), 1);
        ui_field(cx, hy + S(24), S(280), S(38), s->host, s->field == 6, "nom-de-machine"); zone(w, cx, hy + S(24), S(280), S(38), A_FIELD + 5); ui_button(w, cx + S(292), hy + S(24), S(120), S(38), "Appliquer", NULL, A_HOST, 0);
    }
}

App app_settings = { "settings", "Paramètres", "settings", "Thème, comptes, date, sécurité, raccourcis", 900, 620, set_create, set_destroy, set_draw, set_action, NULL, set_key, set_tick, set_open_arg };
