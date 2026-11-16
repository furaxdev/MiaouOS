/* Miaou Web: the MiaouOS browser. HTML + CSS (subset) + JavaScript (ES5, mujs) + forms, rendered by the web_*.c engine.
 * Pages are fetched asynchronously with BusyBox wget; scripts, stylesheets and XHR use a short synchronous fetch.
 * Default search engine: Google. No images (alt text is shown), no Promise/let/const (ES5 only). */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "desk.h"
#include "web.h"

typedef struct {
    Page *pg; char url[600], urlbuf[600]; int editing, scroll, total;
    char *hist[32]; int hpos, hn;
    Job *job; char pending[600], pending_post[1200]; char status[200]; int loading;
    long fetch_budget; Win *win;
} Br;
enum { B_BACK = 1, B_FWD, B_RELOAD, B_HOME, B_URL };

static const char *HOME_HTML =
    "<html><head><title>Miaou Web</title><style>"
    "body{font-family:sans-serif;background:#faf7f2;color:#2b2f36}"
    "#hero{text-align:center;margin-top:36px}"
    "h1{font-size:44px;margin:8px 0 4px;color:#d98a1a}"
    ".tag{color:#6b7280;margin-bottom:22px}"
    "#q{width:420px;font-size:16px}"
    ".go{background:#f2a93b;color:#26323a;font-weight:bold}"
    ".cards{text-align:center;margin-top:26px}"
    ".card{display:inline-block;width:200px;margin:8px;padding:14px;background:#ffffff;border:1px solid #e6dfd3;border-radius:10px;text-align:left}"
    ".card b{display:block;margin-bottom:4px}"
    ".card span{color:#6b7280;font-size:13px}"
    "#foot{text-align:center;color:#8a8f98;font-size:13px;margin-top:30px}"
    "</style></head><body>"
    "<div id=\"hero\"><h1>Miaou Web</h1><div class=\"tag\">Le navigateur des chats : HTML, CSS et JavaScript</div>"
    "<form action=\"https://www.google.com/search\" method=\"get\"><input id=\"q\" name=\"q\" placeholder=\"Rechercher avec Google\"> <input type=\"submit\" class=\"go\" value=\"Rechercher\"></form></div>"
    "<div class=\"cards\">"
    "<a href=\"https://www.google.com\"><div class=\"card\"><b>Google</b><span>Moteur de recherche par défaut</span></div></a>"
    "<a href=\"https://fr.wikipedia.org/wiki/Chat\"><div class=\"card\"><b>Wikipédia</b><span>L'article sur le chat</span></div></a>"
    "<a href=\"http://example.com\"><div class=\"card\"><b>example.com</b><span>Page d'exemple simple</span></div></a>"
    "<a href=\"file:///etc\"><div class=\"card\"><b>Fichiers locaux</b><span>Parcourir /etc</span></div></a></div>"
    "<div id=\"foot\"><span id=\"clock\"></span></div>"
    "<script>var c=document.getElementById('clock');function t(){var d=new Date();c.textContent='Il est '+d.getHours()+'h'+('0'+d.getMinutes()).slice(-2)+' — le JavaScript fonctionne !';}t();setInterval(t,1000);</script>"
    "</body></html>";

static void resolve(const char *base, const char *href, char *out, size_t n) {
    if (!strncmp(href, "http://", 7) || !strncmp(href, "https://", 8) || !strncmp(href, "file://", 7) || !strncmp(href, "about:", 6)) { snprintf(out, n, "%s", href); return; }
    char origin[300] = "", dir[600] = "";
    const char *p = strstr(base, "://");
    if (p) { const char *hostend = strchr(p + 3, '/'); size_t ol = hostend ? (size_t)(hostend - base) : strlen(base); if (ol >= sizeof origin) ol = sizeof origin - 1; memcpy(origin, base, ol); origin[ol] = 0;
             snprintf(dir, sizeof dir, "%s", base); char *q = strrchr(dir, '/'); if (q && q > dir + (p - base) + 2) q[1] = 0; else { size_t l = strlen(dir); if (l + 1 < sizeof dir) { dir[l] = '/'; dir[l + 1] = 0; } } }
    if (href[0] == '#') { snprintf(out, n, "%s", base); return; }
    if (!strncmp(href, "//", 2)) { snprintf(out, n, "%s%s", !strncmp(base, "https", 5) ? "https:" : "http:", href); return; }
    if (href[0] == '/') { snprintf(out, n, "%s%s", origin, href); return; }
    snprintf(out, n, "%s%s", dir, href);
}
static void urlenc(const char *in, char *out, size_t n) {
    size_t k = 0;
    for (const unsigned char *s = (const unsigned char *)in; *s && k + 4 < n; s++) {
        if (isalnum(*s) || *s == '-' || *s == '_' || *s == '.' || *s == '~') out[k++] = (char)*s;
        else if (*s == ' ') out[k++] = '+';
        else k += (size_t)snprintf(out + k, 4, "%%%02X", *s);
    }
    out[k] = 0;
}

/* synchronous fetch for sub-resources (scripts, stylesheets, XHR) */
static char *fetch_sync(const char *url, size_t *len, void *ctx) {
    Br *b = ctx; char full[700]; resolve(b->pg ? b->pg->base : b->url, url, full, sizeof full);
    *len = 0;
    if (!strncmp(full, "file://", 7)) { size_t n = 0; char *t = read_file(full + 7, &n, 600000); if (t) *len = n; return t; }
    if (strncmp(full, "http://", 7) && strncmp(full, "https://", 8)) return NULL;
    if (b->fetch_budget <= 0) return NULL;
    char q[740]; size_t k = 0; for (const char *s = full; *s && k < 700; s++) { if (*s == '\'') { memcpy(q + k, "%27", 3); k += 3; } else q[k++] = *s; } q[k] = 0;
    char cmd[900]; snprintf(cmd, sizeof cmd, "wget -q -T 4 -U 'Mozilla/5.0 (X11; MiaouOS) MiaouWeb/1.0' -O - '%s' 2>/dev/null", q);
    long t0 = now_ms(); FILE *f = popen(cmd, "r"); if (!f) return NULL;
    char *buf = malloc(600001); size_t n = 0, r;
    while (n < 600000 && (r = fread(buf + n, 1, 600000 - n, f)) > 0) n += r;
    pclose(f); b->fetch_budget -= now_ms() - t0;
    if (!n) { free(buf); return NULL; }
    buf[n] = 0; *len = n; return buf;
}

static void collect_css(Br *b, Page *pg, Node *n, int *links) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type) continue;
        if (!strcmp(c->tag, "style")) { if (c->first && c->first->text) css_add_sheet(pg, c->first->text); }
        else if (!strcmp(c->tag, "link")) {
            const char *rel = wn_attr(c, "rel"), *href = wn_attr(c, "href"), *media = wn_attr(c, "media");
            if (rel && href && strcasestr(rel, "stylesheet") && !(media && strcasestr(media, "print")) && *links < 4) {
                (*links)++; size_t len; char *css = fetch_sync(href, &len, b); if (css) { css_add_sheet(pg, css); free(css); }
            }
        }
        else if (strcmp(c->tag, "script") && strcmp(c->tag, "noscript")) collect_css(b, pg, c, links);
    }
}
static void relayout(Br *b, int width) {
    Page *pg = b->pg; if (!pg) return;
    css_cascade(pg); lay_page(pg, width); pg->dirty = 0; b->total = pg->height;
}
static int content_w(Win *w) { return w->cw - S(14); }
static void load_done(Br *b, const char *body, const char *url) {
    page_free(b->pg); b->pg = page_new(); Page *pg = b->pg;
    snprintf(b->url, sizeof b->url, "%s", url); snprintf(pg->url, sizeof pg->url, "%s", url); snprintf(pg->base, sizeof pg->base, "%s", url);
    pg->fetch = fetch_sync; pg->fctx = b; b->fetch_budget = 12000; b->scroll = 0;
    if (!strchr(body, '<')) {                       /* plain text file: show it as preformatted text */
        Node *pre = wn_new(pg, 0, "pre"); Node *t = wn_new(pg, 1, NULL); t->text = strdup(body); wn_append(pre, t); wn_append(pg->doc, pre);
    } else wn_parse(pg, body, pg->doc);
    if (!wn_find_tag(pg->doc, "body")) {            /* fragments get the implicit <html><body> (and its 8px margin) like a real browser */
        Node *html = wn_new(pg, 0, "html"), *body = wn_new(pg, 0, "body");
        while (pg->doc->first) wn_append(body, pg->doc->first);
        wn_append(html, body); wn_append(pg->doc, html);
    }
    Node *base = wn_find_tag(pg->doc, "base"); if (base && wn_attr(base, "href")) { char full[700]; resolve(url, wn_attr(base, "href"), full, sizeof full); snprintf(pg->base, sizeof pg->base, "%s", full); }
    int links = 0; collect_css(b, pg, pg->doc, &links);
    js_page_init(pg); js_run_scripts(pg);
    Node *t = wn_find_tag(pg->doc, "title"); if (t) { char *x = wn_text(t); snprintf(pg->title, sizeof pg->title, "%s", x); free(x); }
    if (!pg->title[0]) snprintf(pg->title, sizeof pg->title, "%s", url);
    pg->dirty = 1;
    if (b->win) { char tt[96]; snprintf(tt, sizeof tt, "%.50s — Miaou Web", pg->title); set_title(b->win, tt); }
}
static void navigate(Br *b, const char *url, int push);
static void navigate_post(Br *b, const char *url, const char *body) {
    snprintf(b->pending_post, sizeof b->pending_post, "%s", body); navigate(b, url, 1);
}
static void navigate(Br *b, const char *url, int push) {
    char u[700]; snprintf(u, sizeof u, "%s", url);
    char *hash = strchr(u, '#'); if (hash && strncmp(u, "about:", 6)) *hash = 0;
    if (push) { if (b->hn > b->hpos + 1) { for (int i = b->hpos + 1; i < b->hn; i++) free(b->hist[i]); b->hn = b->hpos + 1; } if (b->hn == 32) { free(b->hist[0]); memmove(b->hist, b->hist + 1, 31 * sizeof(char *)); b->hn--; } b->hist[b->hn++] = strdup(u); b->hpos = b->hn - 1; }
    if (b->job) { job_free(b->job); b->job = NULL; }
    b->loading = 0; b->status[0] = 0;
    if (!strcmp(u, "about:miaou") || !u[0]) { b->pending_post[0] = 0; load_done(b, HOME_HTML, "about:miaou"); return; }
    if (!strncmp(u, "file://", 7)) {
        const char *path = u + 7; struct stat st; b->pending_post[0] = 0;
        if (stat(path, &st) != 0) { load_done(b, "<h1>Fichier introuvable</h1><p>Impossible d'ouvrir ce chemin.</p>", u); return; }
        if (S_ISDIR(st.st_mode)) {
            char *html = malloc(65536); size_t l = (size_t)snprintf(html, 65536, "<h1>Index de %s</h1><ul>", path);
            DIR *d = opendir(path); struct dirent *de;
            while (d && (de = readdir(d)) && l < 60000) { if (!strcmp(de->d_name, ".")) continue; l += (size_t)snprintf(html + l, 65536 - l, "<li><a href=\"file://%s%s%s\">%s</a></li>", path, path[strlen(path) - 1] == '/' ? "" : "/", de->d_name, de->d_name); }
            if (d) closedir(d); load_done(b, html, u); free(html); return;
        }
        char *txt = read_file(path, NULL, 600000); if (!txt) { load_done(b, "<h1>Lecture impossible</h1>", u); return; }
        load_done(b, txt, u); free(txt); return;
    }
    if (strncmp(u, "http://", 7) && strncmp(u, "https://", 8)) { b->pending_post[0] = 0; load_done(b, "<h1>Adresse non prise en charge</h1><p>Utilisez http://, https:// ou file://.</p>", u); return; }
    char q[800]; size_t k = 0;
    for (const char *s = u; *s && k < 740; s++) { if (*s == '\'') { memcpy(q + k, "%27", 3); k += 3; } else q[k++] = *s; } q[k] = 0;
    char post[300] = "";
    if (b->pending_post[0]) { char pq[1300]; size_t pk = 0; for (const char *s = b->pending_post; *s && pk < 1200; s++) { if (*s == '\'') { memcpy(pq + pk, "%27", 3); pk += 3; } else pq[pk++] = *s; } pq[pk] = 0; snprintf(post, sizeof post, "--post-data '%.280s' ", pq); }
    char cmd[2200]; snprintf(cmd, sizeof cmd, "wget -q -T 15 -U 'Mozilla/5.0 (X11; MiaouOS) MiaouWeb/1.0' %s-O - '%s' 2>/dev/null", post, q);
    b->pending_post[0] = 0;
    b->job = job_start(cmd); b->loading = 1; snprintf(b->pending, sizeof b->pending, "%s", u);
    snprintf(b->status, sizeof b->status, "Chargement de %s…", u);
}
static void go_input(Br *b) {
    char u[900]; const char *in = b->urlbuf;
    if (!*in) return;
    if (strstr(in, "://") || !strncmp(in, "about:", 6)) snprintf(u, sizeof u, "%s", in);
    else if (strchr(in, '.') && !strchr(in, ' ') && strlen(in) > 3) snprintf(u, sizeof u, "https://%s", in);
    else { char enc[500]; urlenc(in, enc, sizeof enc); snprintf(u, sizeof u, "https://www.google.com/search?q=%s&hl=fr", enc); }   /* Google is the default search engine */
    b->editing = 0; navigate(b, u, 1);
}
static void *br_create(Win *w) { Br *b = calloc(1, sizeof *b); b->win = w; navigate(b, "about:miaou", 1); return b; }
static void br_destroy(Win *w) { Br *b = w->st; page_free(b->pg); for (int i = 0; i < b->hn; i++) free(b->hist[i]); job_free(b->job); free(b); }
static void history_go(Br *b, int d) { int n = b->hpos + d; if (n < 0 || n >= b->hn) return; b->hpos = n; navigate(b, b->hist[n], 0); }

/* ---- forms ---- */
static void form_collect(Node *n, Node *submitter, char *q, size_t cap) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type) continue;
        const char *name = wn_attr(c, "name");
        if (name && *name && !wn_attr(c, "disabled")) {
            char val[400] = ""; int use = 0;
            if (!strcmp(c->tag, "input")) {
                const char *ty = wn_attr(c, "type"); ty = ty ? ty : "text";
                if (!strcasecmp(ty, "checkbox") || !strcasecmp(ty, "radio")) { use = wn_attr(c, "checked") != NULL; const char *v = wn_attr(c, "value"); snprintf(val, sizeof val, "%s", v ? v : "on"); }
                else if (!strcasecmp(ty, "submit") || !strcasecmp(ty, "button") || !strcasecmp(ty, "image")) { use = c == submitter; const char *v = wn_attr(c, "value"); snprintf(val, sizeof val, "%s", v ? v : ""); }
                else if (strcasecmp(ty, "reset") && strcasecmp(ty, "file")) { use = 1; const char *v = wn_attr(c, "value"); snprintf(val, sizeof val, "%s", v ? v : ""); }
            } else if (!strcmp(c->tag, "textarea")) { use = 1; char *t = wn_text(c); snprintf(val, sizeof val, "%s", t); free(t); }
            else if (!strcmp(c->tag, "select")) { use = 1; Node *sel = NULL; for (Node *o = c->first; o; o = o->next) if (!o->type && !strcmp(o->tag, "option")) { if (!sel || wn_attr(o, "selected")) sel = o; if (wn_attr(o, "selected")) break; }
                if (sel) { const char *v = wn_attr(sel, "value"); if (v) snprintf(val, sizeof val, "%s", v); else { char *t = wn_text(sel); snprintf(val, sizeof val, "%s", t); free(t); } } }
            else if (!strcmp(c->tag, "button")) { use = c == submitter; const char *v = wn_attr(c, "value"); snprintf(val, sizeof val, "%s", v ? v : ""); }
            if (use) { char en[200], ev[900]; urlenc(name, en, sizeof en); urlenc(val, ev, sizeof ev); size_t l = strlen(q); if (l + strlen(en) + strlen(ev) + 3 < cap) snprintf(q + l, cap - l, "%s%s=%s", l ? "&" : "", en, ev); }
        }
        form_collect(c, submitter, q, cap);
    }
}
static void form_submit(Br *b, Node *form, Node *submitter) {
    char q[1500] = ""; form_collect(form, submitter, q, sizeof q);
    const char *act = wn_attr(form, "action"), *method = wn_attr(form, "method");
    char target[800]; resolve(b->pg->base, act && *act ? act : b->pg->url, target, sizeof target);
    if (method && !strcasecmp(method, "post")) { navigate_post(b, target, q); return; }
    char *qm = strchr(target, '?'); if (qm) *qm = 0; char *h = strchr(target, '#'); if (h) *h = 0;
    char full[2400]; snprintf(full, sizeof full, "%s?%s", target, q); navigate(b, full, 1);
}
static Node *up_to(Node *n, const char *tag) { for (; n; n = n->parent) if (!n->type && !strcmp(n->tag, tag)) return n; return NULL; }

static void click_node(Br *b, Node *t) {
    Page *pg = b->pg;
    /* checkbox / radio toggle first (like a real browser, handlers see the new state) */
    if (t && !strcmp(t->tag, "input")) {
        const char *ty = wn_attr(t, "type"); ty = ty ? ty : "text";
        if (!strcasecmp(ty, "checkbox")) { if (wn_attr(t, "checked")) wn_del_attr(t, "checked"); else wn_set_attr(t, "checked", ""); pg->dirty = 1; }
        else if (!strcasecmp(ty, "radio")) {
            Node *form = up_to(t, "form"); const char *nm = wn_attr(t, "name");
            if (nm) for (int i = 0; i < pg->nall; i++) { Node *o = pg->all[i]; if (!o->type && !strcmp(o->tag, "input") && o != t && wn_attr(o, "name") && !strcmp(wn_attr(o, "name"), nm) && up_to(o, "form") == form) wn_del_attr(o, "checked"); }
            wn_set_attr(t, "checked", ""); pg->dirty = 1;
        }
    }
    if (t && (!strcmp(t->tag, "input") || !strcmp(t->tag, "textarea") || !strcmp(t->tag, "select"))) pg->focus = t; else pg->focus = NULL;
    if (t && !strcmp(t->tag, "select")) {      /* cycle through the options (no popup menu) */
        Node *cur = NULL, *first = NULL; for (Node *o = t->first; o; o = o->next) if (!o->type && !strcmp(o->tag, "option")) { if (!first) first = o; if (wn_attr(o, "selected")) cur = o; }
        Node *nx = NULL; if (cur) for (Node *o = cur->next; o; o = o->next) if (!o->type && !strcmp(o->tag, "option")) { nx = o; break; }
        if (!nx) nx = first; if (cur) wn_del_attr(cur, "selected"); if (nx) wn_set_attr(nx, "selected", ""); pg->dirty = 1;
        js_dispatch(pg, t, "change", NULL);
    }
    int prevented = t ? js_dispatch(pg, t, "click", NULL) : 0;
    if (prevented || !t) return;
    Node *a = up_to(t, "a");
    if (a && wn_attr(a, "href")) {
        const char *h = wn_attr(a, "href");
        if (!strncmp(h, "javascript:", 11)) { js_run_inline(pg, a, h + 11); return; }
        if (h[0] == '#') return;
        if (!strncmp(h, "mailto:", 7) || !strncmp(h, "tel:", 4)) return;
        char full[800]; resolve(pg->base, h, full, sizeof full); navigate(b, full, 1); return;
    }
    Node *btn = up_to(t, "button"); if (!btn && !strcmp(t->tag, "input")) { const char *ty = wn_attr(t, "type"); if (ty && (!strcasecmp(ty, "submit") || !strcasecmp(ty, "image"))) btn = t; }
    if (btn) { const char *ty = wn_attr(btn, "type"); if (!ty || !strcasecmp(ty, "submit")) { Node *f = up_to(btn, "form"); if (f && !js_dispatch(pg, f, "submit", NULL)) form_submit(b, f, btn); } }
}

/* ---- widgets ---- */
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
        if (y > top && x >= 0 && b->pg) {
            b->editing = 0;
            Node *t = lay_hit(b->pg, x, y - top + b->scroll);
            click_node(b, t);
        }
    }
}
static void edit_value(Br *b, Node *f, int key) {
    int ta = !strcmp(f->tag, "textarea");
    char *cur = ta ? wn_text(f) : strdup(wn_attr(f, "value") ? wn_attr(f, "value") : ""); size_t n = strlen(cur);
    char *nw = malloc(n + 8); memcpy(nw, cur, n); nw[n] = 0;
    if (key == K_BS) { if (n) { while (n > 0 && (nw[n - 1] & 0xc0) == 0x80) n--; if (n) n--; nw[n] = 0; } }
    else if (key == K_ENTER && ta) { nw[n++] = '\n'; nw[n] = 0; }
    else if (key >= 32 && key < 0x200000 && n < 1000) { n += (size_t)utf8_put(nw + n, key); nw[n] = 0; }
    if (ta) wn_set_text(b->pg, f, nw); else wn_set_attr(f, "value", nw);
    free(cur); free(nw);
}
static void br_key(Win *w, int key, int mods) {
    Br *b = w->st; Page *pg = b->pg;
    if ((mods & MOD_CTRL) && (key == 'l' || key == 'L')) { b->editing = 1; snprintf(b->urlbuf, sizeof b->urlbuf, "%s", strcmp(b->url, "about:miaou") ? b->url : ""); return; }
    if (b->editing) {
        int n = (int)strlen(b->urlbuf);
        if (key == K_ESC) b->editing = 0; else if (key == K_ENTER) go_input(b);
        else if (key == K_BS) { if (n) { while (n > 0 && (b->urlbuf[n - 1] & 0xc0) == 0x80) n--; if (n) n--; b->urlbuf[n] = 0; } }
        else if (key >= 32 && key < 0x200000 && n < 560 && !(mods & MOD_CTRL)) { n += utf8_put(b->urlbuf + n, key); b->urlbuf[n] = 0; }
        return;
    }
    if (pg && pg->focus && strcmp(pg->focus->tag, "select")) {
        Node *f = pg->focus;
        if (key == K_ESC) { pg->focus = NULL; return; }
        if (key == K_ENTER && !strcmp(f->tag, "input")) {
            js_dispatch(pg, f, "keydown", "Enter");
            Node *form = up_to(f, "form"); if (form && !js_dispatch(pg, form, "submit", NULL)) form_submit(b, form, NULL); return;
        }
        if (key == K_TAB) { Node *nx = NULL; int seen = 0; for (int i = 0; i < pg->nall && !nx; i++) { Node *o = pg->all[i]; if (o->type || (strcmp(o->tag, "input") && strcmp(o->tag, "textarea"))) continue; if (seen) nx = o; if (o == f) seen = 1; } pg->focus = nx; return; }
        if (key == K_BS || key == K_ENTER || (key >= 32 && key < 0x200000 && !(mods & MOD_CTRL))) {
            char kn[8] = ""; if (key >= 32 && key < 127) { kn[0] = (char)key; kn[1] = 0; }
            if (!js_dispatch(pg, f, "keydown", kn)) { edit_value(b, f, key); js_dispatch(pg, f, "input", kn); js_dispatch(pg, f, "keyup", kn); }
            return;
        }
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
static int br_cursor(Win *w, int x, int y) {
    Br *b = w->st; int top = S(52);
    if (y < top || !b->pg) return CK_ARROW;
    if (b->loading) return CK_BUSY;
    Node *t = lay_hit(b->pg, x, y - top + b->scroll); if (!t) return CK_ARROW;
    const char *ty = wn_attr(t, "type"); ty = ty ? ty : "text";
    if (!strcmp(t->tag, "textarea") || (!strcmp(t->tag, "input") && strcasecmp(ty, "submit") && strcasecmp(ty, "button") && strcasecmp(ty, "checkbox") && strcasecmp(ty, "radio"))) return CK_IBEAM;
    for (Node *n = t; n; n = n->parent) { if (n->type) continue; if ((!strcmp(n->tag, "a") && wn_attr(n, "href")) || !strcmp(n->tag, "button") || !strcmp(n->tag, "select") || !strcmp(n->tag, "input") || wn_attr(n, "onclick")) return CK_HAND; }
    return CK_ARROW;
}
static void br_open_arg(Win *w, const char *url) { navigate(w->st, url, 1); }
static int br_tick(Win *w) {
    Br *b = w->st; int redraw = 0;
    if (b->job) {
        int ch = job_poll(b->job); redraw = ch;
        if (b->job->done) {
            int st = b->job->status; char u[600]; snprintf(u, sizeof u, "%s", b->pending);
            if (b->job->len > 0) { char *body = strdup(b->job->buf); job_free(b->job); b->job = NULL; load_done(b, body, u); free(body); b->status[0] = 0; }
            else { char html[800]; snprintf(html, sizeof html, "<h1>Page inaccessible</h1><p>Impossible de charger %s (code %d). Vérifiez la connexion dans l'application Réseau (bouton Connexion DHCP).</p>", u, st); job_free(b->job); b->job = NULL; load_done(b, html, u); }
            b->loading = 0; redraw = 1;
        }
    }
    Page *pg = b->pg;
    if (pg) {
        if (pg->ntimers) { js_run_timers(pg); redraw = 1; }
        if (pg->pending_nav) {
            int k = pg->pending_nav; pg->pending_nav = 0;
            if (k == 1) { char full[800]; resolve(pg->base, pg->nav_url, full, sizeof full); navigate(b, full, 1); } else if (k == 2) history_go(b, -1); else if (k == 3) history_go(b, 1); else if (k == 4) navigate(b, b->url, 0);
            redraw = 1;
        }
        if (b->pg && b->pg->ntimers) redraw = 1;
    }
    return redraw || b->loading;
}
static void br_draw(Win *w, int x, int y, int cw, int ch) {
    Br *b = w->st; int top = S(52);
    gfx_fill(x, y, cw, ch, RGB(255, 255, 255), 255);
    Page *pg = b->pg; int cwid = content_w(w), chh = ch - top - S(26);
    u32 pbg = RGB(255, 255, 255);
    if (pg) {
        if (pg->dirty || pg->layout_w != cwid) relayout(b, cwid);
        Node *body = wn_find_tag(pg->doc, "body"), *html = wn_find_tag(pg->doc, "html");
        if (body && body->st.has_bg) pbg = body->st.bg; else if (html && html->st.has_bg) pbg = html->st.bg;
        gfx_fill(x, y + top, cw, chh + S(1), pbg, 255);
        gfx_clip(x, y + top + 1, cw, chh);
        lay_paint(pg, x, y + top, cwid, chh, b->scroll);
        gfx_unclip();
        if (b->scroll > b->total - chh) b->scroll = b->total - chh > 0 ? b->total - chh : 0;
        ui_scrollbar(x + cw - S(10), y + top + S(6), chh, b->total, chh, b->scroll);
    }
    gfx_fill(x, y, cw, top, TH.panel, 255); gfx_hline(x, y + top, cw, TH.line, 255);
    ui_button(w, x + S(10), y + S(8), S(38), S(36), NULL, "back", B_BACK, 0);
    ui_button(w, x + S(54), y + S(8), S(38), S(36), NULL, "forward", B_FWD, 0);
    ui_button(w, x + S(98), y + S(8), S(38), S(36), NULL, b->loading ? "stop" : "reload", B_RELOAD, 0);
    ui_button(w, x + S(142), y + S(8), S(38), S(36), NULL, "home-m", B_HOME, 0);
    int fx = x + S(192), fw = cw - S(204);
    ui_field(fx, y + S(8), fw, S(36), b->editing ? b->urlbuf : b->url, b->editing, "Adresse ou recherche Google");
    zone(w, fx, y + S(8), fw, S(36), B_URL);
    const char *st = b->loading ? b->status : NULL; char hov[300] = "";
    if (!st && pg && hover(x, y + top, cwid, chh)) { Node *t = lay_hit(pg, mouse_x - w->cx, mouse_y - w->cy - top + b->scroll); Node *a = t ? up_to(t, "a") : NULL; if (a && wn_attr(a, "href")) { char full[800]; resolve(pg->base, wn_attr(a, "href"), full, sizeof full); snprintf(hov, sizeof hov, "%s", full); st = hov; } }
    if (!st && pg && pg->ncons) st = pg->console[pg->ncons - 1];
    if (!st) st = pg ? pg->title : "";
    gfx_fill(x, y + ch - S(26), cw, S(26), TH.panel, 255); gfx_hline(x, y + ch - S(26), cw, TH.line, 255);
    text_draw_clip(F_UI, x + S(12), y + ch - S(22), st, TH.dim, S(12), cw - S(24));
}

App app_browser = { "browser", "Navigateur", "web", "Navigateur HTML, CSS et JavaScript (recherche Google)", 980, 640, br_create, br_destroy, br_draw, br_action, br_mouse, br_key, br_tick, br_open_arg, br_cursor };
