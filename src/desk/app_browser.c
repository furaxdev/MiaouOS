/* Miaou Web: a small text browser. Fetches http(s) pages with BusyBox wget (asynchronously), file:// paths
 * and a built-in start page, and renders headings, paragraphs, lists and clickable links.
 * No CSS, no JavaScript, no images (this is not a full web engine). */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "desk.h"

typedef struct { char *s; int link; unsigned char style, brk; } Tok;
typedef struct {
    char url[600], urlbuf[600]; int editing;
    Tok *t; int nt, capt; char **links; int nl, capl;
    int *ls; unsigned char *lgap, *lbul; int nlines, lay_w;
    int scroll;
    char *hist[32]; int hpos, hn;
    Job *job; char pending[600]; char status[200], title[64]; int loading, plain;
} Br;
enum { B_BACK = 1, B_FWD, B_RELOAD, B_HOME, B_URL };

static const char *HOME_HTML =
    "<html><head><title>Miaou Web</title></head><body>"
    "<h1>Miaou Web</h1><p>Bienvenue dans le navigateur texte de MiaouOS. Il charge de vraies pages (http et https) "
    "mais n'exécute ni JavaScript ni CSS et n'affiche pas les images.</p>"
    "<h2>Pour commencer</h2><ul>"
    "<li><a href=\"http://example.com\">example.com</a> — page d'exemple simple</li>"
    "<li><a href=\"http://neverssl.com\">neverssl.com</a> — vérifie votre connexion</li>"
    "<li><a href=\"https://lite.duckduckgo.com/lite/\">DuckDuckGo (version légère)</a> — recherche</li>"
    "<li><a href=\"https://fr.wikipedia.org/wiki/Chat\">Wikipédia : le chat</a></li>"
    "<li><a href=\"file:///etc\">Parcourir /etc</a> — pages locales</li>"
    "<li><a href=\"file:///etc/os-release\">/etc/os-release</a></li></ul>"
    "<h2>Astuce</h2><p>Cliquez sur la barre d'adresse (ou Ctrl+L), tapez une adresse ou des mots à rechercher, puis Entrée. "
    "Les connexions https via BusyBox ne vérifient pas les certificats : évitez d'y saisir des informations sensibles.</p>"
    "</body></html>";

static void tok_add(Br *b, const char *w, size_t n, int link, int style, int brk) {
    if (b->nt >= b->capt) { b->capt = b->capt ? b->capt * 2 : 1024; b->t = realloc(b->t, (size_t)b->capt * sizeof(Tok)); }
    Tok *t = &b->t[b->nt++]; t->s = malloc(n + 1); memcpy(t->s, w, n); t->s[n] = 0; t->link = link; t->style = (unsigned char)style; t->brk = (unsigned char)brk;
}
static void clear_page(Br *b) {
    for (int i = 0; i < b->nt; i++) free(b->t[i].s);
    b->nt = 0;
    for (int i = 0; i < b->nl; i++) free(b->links[i]);
    b->nl = 0; free(b->ls); b->ls = NULL; free(b->lgap); b->lgap = NULL; free(b->lbul); b->lbul = NULL; b->nlines = 0; b->lay_w = 0; b->scroll = 0; b->title[0] = 0;
}
static int link_add(Br *b, const char *u) {
    if (b->nl >= b->capl) { b->capl = b->capl ? b->capl * 2 : 64; b->links = realloc(b->links, (size_t)b->capl * sizeof(char *)); }
    b->links[b->nl] = strdup(u); return b->nl++;
}
static void resolve(const char *base, const char *href, char *out, size_t n) {
    if (!strncmp(href, "http://", 7) || !strncmp(href, "https://", 8) || !strncmp(href, "file://", 7) || !strncmp(href, "about:", 6)) { snprintf(out, n, "%s", href); return; }
    char origin[300] = "", dir[600] = "";
    const char *p = strstr(base, "://");
    if (p) { const char *hostend = strchr(p + 3, '/'); size_t ol = hostend ? (size_t)(hostend - base) : strlen(base); if (ol >= sizeof origin) ol = sizeof origin - 1; memcpy(origin, base, ol); origin[ol] = 0;
             snprintf(dir, sizeof dir, "%s", base); char *q = strrchr(dir, '/'); if (q && q > dir + (p - base) + 2) q[1] = 0; else { size_t l = strlen(dir); if (l + 1 < sizeof dir) { dir[l] = '/'; dir[l + 1] = 0; } } }
    if (href[0] == '#') { snprintf(out, n, "%s", base); return; }
    if (!strncmp(href, "//", 2)) { char sch[10] = "http:"; if (!strncmp(base, "https", 5)) strcpy(sch, "https:"); snprintf(out, n, "%s%s", sch, href); return; }
    if (href[0] == '/') { snprintf(out, n, "%s%s", origin, href); return; }
    snprintf(out, n, "%s%s", dir, href);
}
static int decode_entity(const char *s, int *adv) {
    static const struct { const char *n; int cp; } E[] = { { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' }, { "eacute", 0xe9 }, { "egrave", 0xe8 }, { "agrave", 0xe0 },
        { "ecirc", 0xea }, { "ccedil", 0xe7 }, { "ucirc", 0xfb }, { "ocirc", 0xf4 }, { "icirc", 0xee }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "laquo", 0xab }, { "raquo", 0xbb }, { "copy", 0xa9 }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 } };
    const char *e = strchr(s, ';');
    if (!e || e - s > 10) { *adv = 1; return '&'; }
    *adv = (int)(e - s) + 1;
    if (s[1] == '#') { int cp = (s[2] == 'x' || s[2] == 'X') ? (int)strtol(s + 3, NULL, 16) : atoi(s + 2); return cp > 0 ? cp : '?'; }
    for (size_t i = 0; i < sizeof E / sizeof E[0]; i++) if (!strncmp(s + 1, E[i].n, strlen(E[i].n)) && strlen(E[i].n) == (size_t)(e - s - 1)) return E[i].cp;
    return '?';
}
static void parse_html(Br *b, const char *h) {
    int link = -1, style = 0, brk = 0, in_title = 0, bullet_pending = 0, head = 0;
    char word[256]; size_t wl = 0; char title[64] = ""; size_t tl = 0;
    int plain = !strchr(h, '<');
    b->plain = plain;
    #define FLUSH() do { if (wl) { word[wl] = 0; tok_add(b, word, wl, link, style, bullet_pending ? 3 : brk); brk = 0; bullet_pending = 0; wl = 0; } } while (0)
    for (const char *p = h; *p; ) {
        if (!plain && *p == '<') {
            FLUSH();
            const char *e = strchr(p, '>'); if (!e) break;
            if (!strncmp(p, "<!--", 4)) { const char *c = strstr(p, "-->"); p = c ? c + 3 : p + strlen(p); continue; }
            char name[16] = ""; const char *q = p + 1; int closing = 0; if (*q == '/') { closing = 1; q++; }
            size_t k = 0; while (q < e && (isalnum((unsigned char)*q)) && k < 15) name[k++] = (char)tolower((unsigned char)*q++); name[k] = 0;
            if (!closing && (!strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "head") || !strcmp(name, "svg"))) {
                if (!strcmp(name, "head")) { in_title = 0; }
                char endtag[16]; snprintf(endtag, sizeof endtag, "</%s", name);
                const char *c = p; while (*c) { if (!strncasecmp(c, endtag, strlen(endtag))) break; c++; }
                if (!strcmp(name, "head")) { /* still read the <title> inside */ const char *ti = strcasestr(p, "<title"); if (ti && ti < c) { ti = strchr(ti, '>'); if (ti) { ti++; while (*ti && *ti != '<' && tl < 62) title[tl++] = *ti++; title[tl] = 0; } } }
                const char *ce = *c ? strchr(c, '>') : NULL; p = ce ? ce + 1 : p + strlen(p); continue;
            }
            if (!strcmp(name, "title")) in_title = !closing;
            else if (!strcmp(name, "a")) {
                if (closing) link = -1;
                else { const char *hp = strcasestr(p, "href="); if (hp && hp < e) { hp += 5; char qc = *hp == '"' || *hp == '\'' ? *hp++ : 0; char u[500]; size_t ul = 0; while (*hp && ul < 499 && (qc ? *hp != qc : (*hp != ' ' && *hp != '>'))) u[ul++] = *hp++; u[ul] = 0;
                        char full[600]; resolve(b->url, u, full, sizeof full); if (strncmp(full, "javascript:", 11) && strncmp(full, "mailto:", 7)) link = link_add(b, full); } }
            }
            else if (!strcmp(name, "b") || !strcmp(name, "strong") || !strcmp(name, "em") || !strcmp(name, "i")) style = closing ? head : 1;
            else if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) { head = closing ? 0 : 2; style = head; brk = 2; }
            else if (!strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "tr") || !strcmp(name, "table") || !strcmp(name, "ul") || !strcmp(name, "ol") || !strcmp(name, "blockquote") || !strcmp(name, "section") || !strcmp(name, "form")) brk = brk < 2 ? 2 : brk;
            else if (!strcmp(name, "br")) brk = brk < 1 ? 1 : brk;
            else if (!strcmp(name, "li") && !closing) { bullet_pending = 1; }
            p = e + 1; continue;
        }
        if (*p == '&') { int adv; int cp = decode_entity(p, &adv); p += adv; char u[5]; int n = utf8_put(u, cp); if (cp == ' ') { FLUSH(); continue; } if (wl + (size_t)n < 250) { memcpy(word + wl, u, (size_t)n); wl += (size_t)n; } continue; }
        if (isspace((unsigned char)*p)) { FLUSH(); if (plain && *p == '\n') brk = 1; p++; continue; }
        if (in_title) { if (tl < 62) title[tl++] = *p; title[tl] = 0; p++; continue; }
        if (wl < 250) word[wl++] = *p; else FLUSH();
        p++;
    }
    FLUSH();
    #undef FLUSH
    snprintf(b->title, sizeof b->title, "%s", title);
}

static void layout(Br *b, int width, int px) {
    free(b->ls); free(b->lgap); free(b->lbul);
    b->ls = malloc((size_t)(b->nt + 2) * sizeof(int)); b->lgap = calloc((size_t)b->nt + 2, 1); b->lbul = calloc((size_t)b->nt + 2, 1);
    b->nlines = 0; b->lay_w = width;
    int x = 0, sp = text_w(F_UI, " ", px);
    for (int i = 0; i < b->nt; i++) {
        Tok *t = &b->t[i];
        int tpx = t->style == 2 ? px + S(5) : px;
        int w = text_w(t->style ? F_BOLD : F_UI, t->s, tpx);
        int newline = (i == 0) || t->brk >= 1 || x + w > width;
        if (newline) { b->ls[b->nlines] = i; b->lgap[b->nlines] = (t->brk >= 2 && i > 0) ? 1 : 0; b->lbul[b->nlines] = t->brk == 3; b->nlines++; x = t->brk == 3 ? S(22) : 0; }
        x += w + sp;
    }
    b->ls[b->nlines] = b->nt;
}

static void navigate(Br *b, const char *url, int push);
static void load_done(Br *b, const char *body, const char *url) {
    clear_page(b);
    snprintf(b->url, sizeof b->url, "%s", url);
    parse_html(b, body);
    if (!b->title[0]) snprintf(b->title, sizeof b->title, "%s", url);
}
static void navigate(Br *b, const char *url, int push) {
    char u[600]; snprintf(u, sizeof u, "%s", url);
    if (push) { if (b->hn > b->hpos + 1) { for (int i = b->hpos + 1; i < b->hn; i++) free(b->hist[i]); b->hn = b->hpos + 1; } if (b->hn == 32) { free(b->hist[0]); memmove(b->hist, b->hist + 1, 31 * sizeof(char *)); b->hn--; } b->hist[b->hn++] = strdup(u); b->hpos = b->hn - 1; }
    if (b->job) { job_free(b->job); b->job = NULL; }
    b->loading = 0; b->status[0] = 0;
    if (!strcmp(u, "about:miaou") || !u[0]) { load_done(b, HOME_HTML, "about:miaou"); return; }
    if (!strncmp(u, "file://", 7)) {
        const char *path = u + 7; struct stat st;
        if (stat(path, &st) != 0) { load_done(b, "<h1>Fichier introuvable</h1><p>Impossible d'ouvrir ce chemin.</p>", u); return; }
        if (S_ISDIR(st.st_mode)) {
            char *html = malloc(65536); size_t l = (size_t)snprintf(html, 65536, "<h1>Index de %s</h1>", path);
            DIR *d = opendir(path); struct dirent *de;
            while (d && (de = readdir(d)) && l < 60000) { if (!strcmp(de->d_name, ".")) continue; l += (size_t)snprintf(html + l, 65536 - l, "<li><a href=\"file://%s%s%s\">%s</a></li>", path, path[strlen(path) - 1] == '/' ? "" : "/", de->d_name, de->d_name); }
            if (d) closedir(d); load_done(b, html, u); free(html); return;
        }
        char *txt = read_file(path, NULL, 400000); if (!txt) { load_done(b, "<h1>Lecture impossible</h1>", u); return; }
        load_done(b, txt, u); free(txt); return;
    }
    if (strncmp(u, "http://", 7) && strncmp(u, "https://", 8)) { load_done(b, "<h1>Adresse non prise en charge</h1><p>Utilisez http://, https:// ou file://.</p>", u); return; }
    char q[700]; size_t k = 0;
    for (const char *s = u; *s && k < 640; s++) { if (*s == '\'') { memcpy(q + k, "%27", 3); k += 3; } else q[k++] = *s; } q[k] = 0;
    char cmd[900]; snprintf(cmd, sizeof cmd, "wget -q -T 15 -U 'Mozilla/5.0 (X11; MiaouOS) MiaouWeb/0.2' -O - '%s' 2>/dev/null", q);
    b->job = job_start(cmd); b->loading = 1; snprintf(b->pending, sizeof b->pending, "%s", u);
    snprintf(b->status, sizeof b->status, "Chargement de %s…", u);
}
static void go_input(Br *b) {
    char u[700]; const char *in = b->urlbuf;
    if (!*in) return;
    if (strstr(in, "://") || !strncmp(in, "about:", 6)) snprintf(u, sizeof u, "%s", in);
    else if (strchr(in, '.') && !strchr(in, ' ')) snprintf(u, sizeof u, "http://%s", in);
    else { char enc[400]; size_t k = 0; for (const char *s = in; *s && k < 380; s++) { if (*s == ' ') enc[k++] = '+'; else if (isalnum((unsigned char)*s)) enc[k++] = *s; else k += (size_t)snprintf(enc + k, 8, "%%%02X", (unsigned char)*s); } enc[k] = 0; snprintf(u, sizeof u, "https://lite.duckduckgo.com/lite/?q=%s", enc); }
    b->editing = 0; navigate(b, u, 1);
}
static void *br_create(Win *w) { (void)w; Br *b = calloc(1, sizeof *b); navigate(b, "about:miaou", 1); return b; }
static void br_destroy(Win *w) { Br *b = w->st; clear_page(b); free(b->t); free(b->links); for (int i = 0; i < b->hn; i++) free(b->hist[i]); job_free(b->job); free(b); }
static void history_go(Br *b, int d) { int n = b->hpos + d; if (n < 0 || n >= b->hn) return; b->hpos = n; navigate(b, b->hist[n], 0); }

/* shared walk: draws (draw=1) or hit-tests (px,py) the page; returns link index under (px,py) or -1 */
static int render(Br *b, int x, int y, int w, int h, int draw, int hx, int hy) {
    int px = S(15), lhn = (int)(px * 1.45f), sp = text_w(F_UI, " ", px), hit = -1;
    if (b->lay_w != w) layout(b, w, px);
    int cy = -b->scroll, total = 0;
    for (int l = 0; l < b->nlines; l++) {
        int start = b->ls[l], end = b->ls[l + 1];
        int big = b->t[start].style == 2;
        int lh = big ? lhn + S(8) : lhn;
        if (b->lgap[l]) cy += lhn / 2;
        int ty = y + cy, cx = x + (b->lbul[l] ? S(22) : 0);
        if (ty + lh > y && ty < y + h) {
            if (draw && b->lbul[l]) gfx_circle(x + S(8), ty + lhn / 2, S(3), TH.accent, 255);
            for (int i = start; i < end; i++) {
                Tok *t = &b->t[i]; int tpx = t->style == 2 ? px + S(5) : px; Font *f = t->style ? F_BOLD : F_UI;
                int tw = text_w(f, t->s, tpx);
                int hot = t->link >= 0 && hx >= cx && hx < cx + tw + sp && hy >= ty && hy < ty + lh;
                if (hot) hit = t->link;
                if (draw) {
                    u32 c = t->link >= 0 ? (hot ? TH.text : TH.accent) : t->style == 2 ? TH.accent : TH.text;
                    text_draw(f, cx, ty + (big ? S(4) : 0), t->s, c, tpx);
                    if (t->link >= 0) gfx_hline(cx, ty + (big ? S(4) : 0) + (int)(tpx * 1.12f), tw, c, 200);
                }
                cx += tw + sp;
            }
        }
        cy += lh;
    }
    total = cy + b->scroll;
    return draw ? total : hit;
}

static void br_action(Win *w, int id, int mx, int my, int btn) {
    Br *b = w->st; (void)mx; (void)my; (void)btn;
    switch (id) {
    case B_BACK: history_go(b, -1); break; case B_FWD: history_go(b, 1); break;
    case B_RELOAD: navigate(b, b->url, 0); break; case B_HOME: navigate(b, "about:miaou", 1); break;
    case B_URL: b->editing = 1; snprintf(b->urlbuf, sizeof b->urlbuf, "%s", strcmp(b->url, "about:miaou") ? b->url : ""); break;
    }
}
static void br_mouse(Win *w, int type, int x, int y, int arg) {
    Br *b = w->st;
    if (type == M_WHEEL) { b->scroll -= arg * S(60); if (b->scroll < 0) b->scroll = 0; return; }
    if (type == M_DOWN) {
        int top = S(52);
        if (y > top && x >= 0) {
            b->editing = 0;
            int l = render(b, S(24), top + S(14), w->cw - S(48), w->ch - top - S(40), 0, x, y);
            if (l >= 0 && l < b->nl) { char u[600]; snprintf(u, sizeof u, "%s", b->links[l]); navigate(b, u, 1); }
        }
    }
}
static void br_key(Win *w, int key, int mods) {
    Br *b = w->st;
    if ((mods & MOD_CTRL) && (key == 'l' || key == 'L')) { b->editing = 1; snprintf(b->urlbuf, sizeof b->urlbuf, "%s", strcmp(b->url, "about:miaou") ? b->url : ""); return; }
    if (b->editing) {
        int n = (int)strlen(b->urlbuf);
        if (key == K_ESC) b->editing = 0; else if (key == K_ENTER) go_input(b);
        else if (key == K_BS) { if (n) { while (n > 0 && (b->urlbuf[n - 1] & 0xc0) == 0x80) n--; if (n) n--; b->urlbuf[n] = 0; } }
        else if (key >= 32 && key < 0x200000 && n < 560) { n += utf8_put(b->urlbuf + n, key); b->urlbuf[n] = 0; }
        return;
    }
    switch (key) {
    case K_DOWN: b->scroll += S(40); break; case K_UP: b->scroll -= S(40); break;
    case K_PGDN: case ' ': b->scroll += w->ch - S(120); break; case K_PGUP: b->scroll -= w->ch - S(120); break;
    case K_HOME: b->scroll = 0; break; case K_END: b->scroll = 1 << 28; break;
    case K_BS: history_go(b, -1); break; case K_F1 + 4: navigate(b, b->url, 0); break;
    case K_LEFT: if (mods & MOD_ALT) history_go(b, -1); break; case K_RIGHT: if (mods & MOD_ALT) history_go(b, 1); break;
    }
    if (b->scroll < 0) b->scroll = 0;
}
static void br_open_arg(Win *w, const char *url) { navigate(w->st, url, 1); }
static int br_tick(Win *w) {
    Br *b = w->st;
    if (!b->job) return 0;
    int ch = job_poll(b->job);
    if (b->job->done) {
        int st = b->job->status;
        if (b->job->len > 0) { char u[600]; snprintf(u, sizeof u, "%s", b->pending); load_done(b, b->job->buf, u); b->status[0] = 0; }
        else { char u[600]; snprintf(u, sizeof u, "%s", b->pending); char html[700]; snprintf(html, sizeof html, "<h1>Page inaccessible</h1><p>Impossible de charger %s (code %d). Vérifiez la connexion dans l'application Réseau (bouton Connexion DHCP).</p>", u, st); load_done(b, html, u); }
        b->loading = 0; job_free(b->job); b->job = NULL; ch = 1;
        char t[64]; snprintf(t, sizeof t, "%.40s — Miaou Web", b->title); set_title(w, t);
    }
    return ch || b->loading;
}
static void br_draw(Win *w, int x, int y, int cw, int ch) {
    Br *b = w->st; int top = S(52);
    gfx_fill(x, y, cw, ch, TH.dark ? RGB(26, 21, 46) : RGB(255, 255, 255), 255);
    gfx_fill(x, y, cw, top, TH.panel, 255); gfx_hline(x, y + top, cw, TH.line, 255);
    ui_button(w, x + S(10), y + S(8), S(38), S(36), NULL, "back", B_BACK, 0);
    ui_button(w, x + S(54), y + S(8), S(38), S(36), NULL, "forward", B_FWD, 0);
    ui_button(w, x + S(98), y + S(8), S(38), S(36), NULL, b->loading ? "stop" : "reload", B_RELOAD, 0);
    ui_button(w, x + S(142), y + S(8), S(38), S(36), NULL, "home-m", B_HOME, 0);
    int fx = x + S(192), fw = cw - S(204);
    ui_field(fx, y + S(8), fw, S(36), b->editing ? b->urlbuf : b->url, b->editing, "Adresse ou recherche");
    zone(w, fx, y + S(8), fw, S(36), B_URL);
    int cx = x + S(24), cwid = cw - S(48), chh = ch - top - S(30);
    gfx_clip(x, y + top + 1, cw - S(8), chh + S(28));
    int total = render(b, cx, y + top + S(14), cwid, chh, 1, 0, 0);
    gfx_unclip();
    if (b->scroll > total - chh) b->scroll = total - chh > 0 ? total - chh : 0;
    ui_scrollbar(x + cw - S(10), y + top + S(6), chh, total, chh, b->scroll);
    int hl = render(b, S(24), top + S(14), cwid, chh, 0, mouse_x - w->cx, mouse_y - w->cy);
    const char *st = b->loading ? b->status : (hl >= 0 && hl < b->nl) ? b->links[hl] : strncmp(b->url, "https://", 8) == 0 ? "https — certificat non vérifié (BusyBox)" : b->title;
    gfx_fill(x, y + ch - S(26), cw, S(26), TH.panel, 255); gfx_hline(x, y + ch - S(26), cw, TH.line, 255);
    text_draw_clip(F_UI, x + S(12), y + ch - S(22), st, TH.dim, S(12), cw - S(24));
}

App app_browser = { "browser", "Navigateur", "web", "Navigateur texte (http, https, file)", 980, 640, br_create, br_destroy, br_draw, br_action, br_mouse, br_key, br_tick, br_open_arg };
