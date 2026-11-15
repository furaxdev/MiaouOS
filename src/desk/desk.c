/* MiaouDesk: window manager, shell (panel, dock, menu), input and main loop. */
#define _GNU_SOURCE
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

/* ================================================================== globals */
Font *F_UI, *F_BOLD, *F_MONO;
Theme TH;
int theme_dark = 1, accent_idx = 0;
int mouse_x, mouse_y;
int want_blink;
Win *focus_win;
#define MAXW 16
static Win *wins[MAXW];
static int nwins, win_serial;
static int ttyfd = -1, mousefd = -1, graphics, saved_kbmode = -1, raw_ok;
static struct termios saved_term;
static int need_draw = 1, quit_req;
static int menu_open, menu_sel, ctx_open, ctx_x, ctx_y;
static char menu_filter[48];
static int hot_sig;

static const u32 ACCENTS[4][2] = { {RGB(232, 163, 61), RGB(154, 91, 0)}, {RGB(240, 120, 106), RGB(180, 65, 47)},
                                   {RGB(111, 160, 240), RGB(47, 99, 194)}, {RGB(141, 184, 148), RGB(60, 122, 74)} };

void dirty(void) { need_draw = 1; }

static long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000L + t.tv_nsec / 1000000L; }

static void slog(const char *fmt, ...) {
    char b[160]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b - 2, fmt, ap); va_end(ap);
    strcat(b, "\n");
    int fd = open("/dev/ttyS0", O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd >= 0) { if (write(fd, b, strlen(b)) < 0) {} close(fd); }
}

/* ================================================================== theme */
void theme_apply(void) {
    u32 a = ACCENTS[accent_idx][0], a2 = ACCENTS[accent_idx][1];
    if (theme_dark) {   /* graphite ramp */
        TH = (Theme){ RGB(20, 20, 21), RGB(15, 15, 16), RGB(25, 25, 27), RGB(35, 35, 37), RGB(29, 29, 31), RGB(237, 237, 237), RGB(161, 161, 166),
                      a, a2, RGB(35, 35, 37), RGB(46, 46, 49), RGB(46, 46, 49), RGB(15, 15, 16), 0, RGB(76, 195, 138), RGB(229, 72, 77), RGB(224, 160, 60), 1 };
        TH.sel = gfx_mix(RGB(25, 25, 27), a, 46);
    } else {
        TH = (Theme){ RGB(250, 250, 250), RGB(245, 245, 246), RGB(255, 255, 255), RGB(244, 244, 245), RGB(248, 248, 249), RGB(23, 23, 23), RGB(82, 82, 82),
                      a2, a, RGB(236, 236, 238), RGB(224, 224, 227), RGB(222, 222, 225), RGB(255, 255, 255), 0, RGB(22, 163, 74), RGB(220, 38, 38), RGB(180, 100, 0), 0 };
        TH.sel = gfx_mix(RGB(255, 255, 255), a2, 40);
    }
}

/* ================================================================== config */
static char cfgk[24][24], cfgv[24][64];
static int ncfg;
static void cfg_load(void) {
    FILE *f = fopen("/etc/miaou/desktop.conf", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f) && ncfg < 24) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0; char *v = eq + 1; v[strcspn(v, "\r\n")] = 0;
        snprintf(cfgk[ncfg], sizeof cfgk[0], "%s", line); snprintf(cfgv[ncfg], sizeof cfgv[0], "%s", v); ncfg++;
    }
    fclose(f);
}
const char *cfg_get(const char *k, const char *def) {
    for (int i = 0; i < ncfg; i++) if (!strcmp(cfgk[i], k)) return cfgv[i];
    return def;
}
void cfg_set(const char *k, const char *v) {
    int i;
    for (i = 0; i < ncfg; i++) if (!strcmp(cfgk[i], k)) break;
    if (i == ncfg) { if (ncfg >= 24) return; ncfg++; snprintf(cfgk[i], sizeof cfgk[0], "%s", k); }
    snprintf(cfgv[i], sizeof cfgv[0], "%s", v);
    if (mkdir("/etc/miaou", 0755) < 0 && errno != EEXIST) return;
    FILE *f = fopen("/etc/miaou/desktop.conf", "w");
    if (!f) return;
    for (int j = 0; j < ncfg; j++) fprintf(f, "%s=%s\n", cfgk[j], cfgv[j]);
    fclose(f);
}

/* ================================================================== helpers / widgets */
char *read_file(const char *path, size_t *n, size_t max) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, len = 0;
    char *b = malloc(cap + 1);
    for (;;) {
        size_t r = fread(b + len, 1, cap - len, f);
        len += r;
        if (len < cap) break;
        if (cap >= max) break;
        cap *= 2; if (cap > max) cap = max;
        b = realloc(b, cap + 1);
    }
    fclose(f);
    b[len] = 0;
    if (n) *n = len;
    return b;
}
void human_size(unsigned long long b, char *out, size_t n) {
    const char *u[] = { "o", "Ko", "Mo", "Go" };
    double v = (double)b; int i = 0;
    while (v >= 1024 && i < 3) { v /= 1024; i++; }
    if (i == 0) snprintf(out, n, "%llu o", b); else snprintf(out, n, "%.1f %s", v, u[i]);
}

void zone(Win *w, int x, int y, int cw, int ch, int id) {
    if (w->nz < MAX_ZONES) { w->z[w->nz].x = x; w->z[w->nz].y = y; w->z[w->nz].w = cw; w->z[w->nz].h = ch; w->z[w->nz].id = id; w->nz++; }
}
int hover(int x, int y, int w, int h) { return mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h; }

int ui_button(Win *w, int x, int y, int bw, int bh, const char *label, const char *icon, int id, int primary) {
    int hot = hover(x, y, bw, bh) && w == focus_win;
    if (!w) hot = hover(x, y, bw, bh);
    u32 bg = primary ? (TH.dark ? (hot ? RGB(255, 255, 255) : RGB(237, 237, 237)) : (hot ? RGB(60, 60, 60) : RGB(23, 23, 23))) : (hot ? TH.btn_hot : TH.btn);
    u32 fg = primary ? (TH.dark ? RGB(20, 20, 21) : RGB(255, 255, 255)) : TH.text;
    gfx_rrect(x, y, bw, bh, S(6), bg, 255);
    if (!primary) gfx_rrect_line(x, y, bw, bh, S(6), TH.line, 255);
    int px = S(14), tw = label ? text_w(F_BOLD, label, px) : 0;
    int isz = icon ? S(18) : 0, gap = (icon && label && *label) ? S(6) : 0;
    int cx = x + (bw - (tw + isz + gap)) / 2;
    if (icon) { Img *im = img_get(icon, isz); if (im) img_draw_tint(im, cx, y + (bh - isz) / 2, 255, fg); cx += isz + gap; }
    if (label) text_draw(F_BOLD, cx, y + (bh - font_line_h(F_BOLD, px)) / 2, label, fg, px);
    if (w && id) zone(w, x, y, bw, bh, id);
    return hot;
}
int ui_tab(Win *w, int x, int y, int bw, int bh, const char *label, const char *icon, int id, int selected) {
    int hot = hover(x, y, bw, bh);
    if (selected) { gfx_rrect(x, y, bw, bh, S(6), TH.btn_hot, 255); gfx_fill(x, y + S(8), S(2), bh - S(16), TH.accent, 255); }
    else if (hot) gfx_rrect(x, y, bw, bh, S(6), TH.btn, 255);
    int cx = x + S(10);
    if (icon) { Img *im = img_get(icon, S(20)); if (im) img_draw(im, cx, y + (bh - S(20)) / 2, 255); cx += S(28); }
    int px = S(14);
    text_draw(selected ? F_BOLD : F_UI, cx, y + (bh - font_line_h(F_UI, px)) / 2, label, TH.text, px);
    zone(w, x, y, bw, bh, id);
    return hot;
}
void ui_field(int x, int y, int fw, int fh, const char *text, int focused, const char *placeholder) {
    gfx_rrect(x, y, fw, fh, S(6), TH.field, 255);
    gfx_rrect_line(x, y, fw, fh, S(6), focused ? TH.accent : TH.line, 255);
    int px = S(14), ty = y + (fh - font_line_h(F_UI, px)) / 2;
    gfx_clip(x + S(6), y, fw - S(12), fh);
    int tw = 0;
    if (text && *text) tw = text_draw(F_UI, x + S(10), ty, text, TH.text, px);
    else if (placeholder) text_draw(F_UI, x + S(10), ty, placeholder, TH.dim, px);
    if (focused) want_blink = 1;
    if (focused && (now_ms() / 500) % 2 == 0) gfx_fill(x + S(10) + tw, ty + 1, 2, font_line_h(F_UI, px) - 2, TH.accent, 255);
    gfx_unclip();
}
void ui_scrollbar(int x, int y, int h, int total, int shown, int pos) {
    if (total <= shown || total <= 0) return;
    gfx_rrect(x, y, S(6), h, S(3), TH.line, 120);
    int th = h * shown / total; if (th < S(20)) th = S(20);
    int ty = y + (h - th) * pos / (total - shown > 0 ? total - shown : 1);
    gfx_rrect(x, ty, S(6), th, S(3), TH.dim, 255);
}
void ui_progress(int x, int y, int w, int h, int pct, u32 c) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    gfx_rrect(x, y, w, h, h / 2, TH.line, 255);
    if (pct > 0) gfx_rrect(x, y, w * pct / 100 < h ? h : w * pct / 100, h, h / 2, c, 255);
}

/* ================================================================== jobs */
Job *job_start(const char *cmd) {
    int p[2];
    if (pipe(p) < 0) return NULL;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return NULL; }
    if (pid == 0) {
        setsid();
        int nul = open("/dev/null", O_RDONLY);
        dup2(nul, 0); dup2(p[1], 1); dup2(p[1], 2);
        close(p[0]); close(p[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    Job *j = calloc(1, sizeof *j);
    j->pid = pid; j->fd = p[0]; j->cap = 4096; j->buf = calloc(1, j->cap + 1);
    return j;
}
int job_poll(Job *j) {
    if (!j || j->done) return 0;
    int changed = 0;
    for (;;) {
        if (j->len + 1024 > j->cap) { if (j->cap >= (1u << 20)) { j->len = 0; } else { j->cap *= 2; j->buf = realloc(j->buf, j->cap + 1); } }
        ssize_t r = read(j->fd, j->buf + j->len, j->cap - j->len);
        if (r > 0) { j->len += (size_t)r; j->buf[j->len] = 0; changed = 1; continue; }
        if (r == 0) { int st; waitpid(j->pid, &st, 0); j->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128; j->done = 1; changed = 1; }
        break;
    }
    return changed;
}
void job_kill(Job *j) { if (j && !j->done) { kill(-j->pid, SIGKILL); kill(j->pid, SIGKILL); } }
void job_free(Job *j) {
    if (!j) return;
    if (!j->done) { job_kill(j); waitpid(j->pid, NULL, 0); }
    if (j->fd >= 0) close(j->fd);
    free(j->buf); free(j);
}

/* ================================================================== toasts */
static struct { char msg[96]; long until; } toasts[4];
void toast(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    for (int i = 3; i > 0; i--) toasts[i] = toasts[i - 1];
    vsnprintf(toasts[0].msg, sizeof toasts[0].msg, fmt, ap); va_end(ap);
    toasts[0].until = now_ms() + 3500;
    dirty();
}

/* ================================================================== keyboard layouts */
static u32 kmap[2][4][128];         /* layout(0=us,1=fr) x state(norm,shift,altgr,shift+altgr) x keycode */
static int layout = 1;
static void setrow(int lay, int st, int start, const char *s) {
    for (int c = start; *s; c++) { int cp = utf8_next(&s); if (cp != '.' || 0) kmap[lay][st][c] = (u32)cp; }
}
static void setrow_raw(int lay, int st, int start, const char *s) {   /* '.' is literal here too; use '\x01' for none */
    for (int c = start; *s; c++) { int cp = utf8_next(&s); kmap[lay][st][c] = cp == 1 ? 0 : (u32)cp; }
}
static void kbd_init(void) {
    /* US */
    setrow_raw(0, 0, 2, "1234567890-="); setrow_raw(0, 1, 2, "!@#$%^&*()_+");
    setrow_raw(0, 0, 16, "qwertyuiop[]"); setrow_raw(0, 1, 16, "QWERTYUIOP{}");
    setrow_raw(0, 0, 30, "asdfghjkl;'`"); setrow_raw(0, 1, 30, "ASDFGHJKL:\"~");
    setrow_raw(0, 0, 43, "\\zxcvbnm,./"); setrow_raw(0, 1, 43, "|ZXCVBNM<>?");
    kmap[0][0][86] = '<'; kmap[0][1][86] = '>';
    /* FR (AZERTY) */
    setrow_raw(1, 0, 2, "&é\"'(-è_çà)="); setrow_raw(1, 1, 2, "1234567890°+");
    setrow_raw(1, 2, 2, "~#{[|`\\^@]}");
    setrow_raw(1, 0, 16, "azertyuiop^$"); setrow_raw(1, 1, 16, "AZERTYUIOP¨£");
    setrow_raw(1, 0, 30, "qsdfghjklmù²"); setrow_raw(1, 1, 30, "QSDFGHJKLM%\x01");
    setrow_raw(1, 0, 43, "*wxcvbn,;:!"); setrow_raw(1, 1, 43, "µWXCVBN?./§");
    kmap[1][0][86] = '<'; kmap[1][1][86] = '>';
    for (int l = 0; l < 2; l++) { kmap[l][0][57] = kmap[l][1][57] = kmap[l][2][57] = ' '; kmap[l][3][57] = ' '; }
    (void)setrow;
    /* keypad */
    static const char kp[] = "789-456+1230."; static const int kc[] = { 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83 };
    for (int i = 0; i < 13; i++) for (int l = 0; l < 2; l++) for (int s = 0; s < 4; s++) kmap[l][s][kc[i]] = (u32)kp[i];
    for (int l = 0; l < 2; l++) { kmap[l][0][55] = '*'; kmap[l][0][98] = '/'; }
}
int kbd_layout(void) { return layout; }
void kbd_set_layout(int fr) { layout = fr ? 1 : 0; cfg_set("layout", fr ? "fr" : "us"); }

/* ================================================================== window manager */
static int panel_h(void) { return S(44); }          /* Plasma-style panel at the bottom */
static void work_area(int *x, int *y, int *w, int *h) {
    *x = 0; *y = 0; *w = SW; *h = SH - panel_h();
}
static int title_h(void) { return S(36); }

static int win_index(Win *w) { for (int i = 0; i < nwins; i++) if (wins[i] == w) return i; return -1; }
static void raise_win(Win *w) {
    int i = win_index(w);
    if (i < 0) return;
    for (; i < nwins - 1; i++) wins[i] = wins[i + 1];
    wins[nwins - 1] = w;
    focus_win = w; w->minimized = 0;
    dirty();
}
void set_title(Win *w, const char *t) { snprintf(w->title, sizeof w->title, "%s", t); }

Win *open_app(const char *id) {
    App *app = NULL;
    for (int i = 0; i < NAPPS; i++) if (!strcmp(APPS[i]->id, id)) app = APPS[i];
    if (!app) return NULL;
    if (!strcmp(id, "terminal") == 0)
        for (int i = 0; i < nwins; i++) if (wins[i]->app == app) { raise_win(wins[i]); menu_open = 0; return wins[i]; }
    if (nwins >= MAXW) { toast("Trop de fenêtres ouvertes"); return NULL; }
    Win *w = calloc(1, sizeof *w);
    w->app = app; w->serial = (unsigned)++win_serial;
    int ax, ay, aw, ah; work_area(&ax, &ay, &aw, &ah);
    w->w = S(app->w); w->h = S(app->h);
    if (w->w > aw - S(20)) w->w = aw - S(20);
    if (w->h > ah - S(10)) w->h = ah - S(10);
    int off = (nwins % 6) * S(28);
    w->x = ax + (aw - w->w) / 2 + off - S(40); w->y = ay + (ah - w->h) / 2 + off - S(20);
    if (w->x < 0) w->x = 0;
    if (w->y < ay) w->y = ay;
    set_title(w, app->title);
    w->cw = w->w; w->ch = w->h;
    wins[nwins++] = w;
    if (app->create) w->st = app->create(w);
    raise_win(w);
    menu_open = 0; ctx_open = 0;
    slog("MIAOUDESK_APP_OPEN %s", id);
    return w;
}
void close_win(Win *w) {
    int i = win_index(w);
    if (i < 0) return;
    if (w->app->destroy) w->app->destroy(w);
    for (; i < nwins - 1; i++) wins[i] = wins[i + 1];
    nwins--;
    slog("MIAOUDESK_APP_CLOSE %s", w->app->id);
    free(w);
    focus_win = NULL;
    for (int k = nwins - 1; k >= 0; k--) if (!wins[k]->minimized) { focus_win = wins[k]; break; }
    dirty();
}
static void win_maximize(Win *w, int on) {
    int ax, ay, aw, ah; work_area(&ax, &ay, &aw, &ah);
    if (on && !w->maximized) { if (!w->snapped) { w->ox = w->x; w->oy = w->y; w->ow = w->w; w->oh = w->h; } w->x = ax; w->y = ay; w->w = aw; w->h = ah; w->maximized = 1; }
    else if (!on && (w->maximized || w->snapped)) { w->x = w->ox; w->y = w->oy; w->w = w->ow; w->h = w->oh; w->maximized = 0; w->snapped = 0; }
    dirty();
}
static void win_snap(Win *w, int side) {      /* -1 left, +1 right */
    int ax, ay, aw, ah; work_area(&ax, &ay, &aw, &ah);
    if (!w->maximized && !w->snapped) { w->ox = w->x; w->oy = w->y; w->ow = w->w; w->oh = w->h; }
    w->maximized = 0; w->snapped = side;
    w->x = side < 0 ? ax : ax + aw / 2; w->y = ay; w->w = aw / 2; w->h = ah;
    dirty();
}
static void cycle_focus(int dir) {
    if (!nwins) return;
    int i = focus_win ? win_index(focus_win) : nwins - 1;
    for (int k = 0; k < nwins; k++) {
        i = (i + dir + nwins) % nwins;
        Win *w = wins[i];
        if (w != focus_win || nwins == 1) {
            /* cycle order: bring that window to front */
            raise_win(w);
            toast("%s", w->title);
            return;
        }
    }
}
static void show_desktop(void) {
    int any = 0; for (int i = 0; i < nwins; i++) if (!wins[i]->minimized) any = 1;
    for (int i = 0; i < nwins; i++) wins[i]->minimized = any;
    if (any) focus_win = NULL; else if (nwins) raise_win(wins[nwins - 1]);
    dirty();
}

/* frame geometry helpers */
#define EDGE (S(5))
static int in_win(Win *w, int x, int y, int m) { return x >= w->x - m && x < w->x + w->w + m && y >= w->y - m && y < w->y + w->h + m; }
static Win *win_at(int x, int y) {
    for (int i = nwins - 1; i >= 0; i--) {
        Win *w = wins[i];
        if (w->minimized) continue;
        if (in_win(w, x, y, w->maximized ? 0 : S(4))) return w;
    }
    return NULL;
}
static void ctrl_center(Win *w, int i, int *cx, int *cy) { *cx = w->x + w->w - S(24) - i * S(32); *cy = w->y + title_h() / 2; }   /* 0 close, 1 maximise, 2 minimise */
static int ctrl_hit(Win *w, int x, int y) {      /* returns 1 close, 2 minimise, 3 maximise */
    static const int ret[3] = { 1, 3, 2 };
    for (int i = 0; i < 3; i++) { int cx, cy; ctrl_center(w, i, &cx, &cy); if (abs(x - cx) <= S(14) && abs(y - cy) <= S(14)) return ret[i]; }
    return 0;
}

/* ================================================================== network summary for the panel */
static char net_text[40] = "hors ligne";
static int net_up;
static void net_refresh(void) {
    char ip[32] = "";
    FILE *f = fopen("/proc/net/fib_trie", "r");
    if (f) {
        char line[160], last[32] = "";
        while (fgets(line, sizeof line, f)) {
            char *p = strstr(line, "|--");
            if (p) { p += 4; p[strcspn(p, "\r\n")] = 0; snprintf(last, sizeof last, "%s", p); }
            else if (strstr(line, "/32 host LOCAL") && last[0] && strncmp(last, "127.", 4) && strcmp(last, "0.0.0.0")) { snprintf(ip, sizeof ip, "%s", last); break; }
        }
        fclose(f);
    }
    int up = ip[0] != 0;
    if (up != net_up) dirty();
    net_up = up;
    snprintf(net_text, sizeof net_text, "%s", up ? ip : "hors ligne");
}

/* ================================================================== drawing */
static struct { int x, y, w, h, id; } sz[96];
static int nsz;
enum { Z_MENU = 1, Z_THEME, Z_LAYOUT, Z_POWER, Z_NET, Z_DOCK = 100, Z_MI = 200, Z_PWC = 300, Z_PWR, Z_PWO, Z_CTX = 400, Z_SEARCH = 500 };
static void szone(int x, int y, int w, int h, int id) { if (nsz < 96) { sz[nsz].x = x; sz[nsz].y = y; sz[nsz].w = w; sz[nsz].h = h; sz[nsz].id = id; nsz++; } }

static void draw_window(Win *w) {
    if (w->minimized) return;
    int th = title_h(), focus = (w == focus_win), r = w->maximized ? 0 : S(8);
    if (!w->maximized) gfx_shadow(w->x, w->y, w->w, w->h, r, S(20), focus ? 110 : 60);
    gfx_rrect(w->x, w->y, w->w, w->h, r, TH.win, 255);
    gfx_clip(w->x, w->y, w->w, th);
    gfx_rrect(w->x, w->y, w->w, th + r + 2, r, focus ? TH.win_title : TH.win_title_dim, 255);
    gfx_unclip();
    gfx_hline(w->x + 1, w->y + th - 1, w->w - 2, TH.line, 255);
    gfx_rrect_line(w->x, w->y, w->w, w->h, r, TH.line, 255);
    /* title: centred, with the application icon */
    int px = S(14), tw = text_w(F_BOLD, w->title, px), isz = S(16);
    Img *ic = img_get(w->app->icon, isz);
    int total = tw + (ic ? isz + S(8) : 0), tx = w->x + (w->w - total) / 2;
    if (tx < w->x + S(12)) tx = w->x + S(12);
    gfx_clip(w->x + S(8), w->y, w->w - S(112), th);
    u32 tc = focus ? TH.text : TH.dim;
    if (ic) { img_draw_tint(ic, tx, w->y + (th - isz) / 2, 255, tc); tx += isz + S(8); }
    text_draw(F_BOLD, tx, w->y + (th - font_line_h(F_BOLD, px)) / 2, w->title, tc, px);
    gfx_unclip();
    /* buttons on the right: minimise, maximise, close */
    for (int i = 0; i < 3; i++) {
        int cx, cy; ctrl_center(w, i, &cx, &cy);
        int hot = hover(cx - S(14), cy - S(14), S(28), S(28));
        u32 gl = tc;
        if (hot) { gfx_rrect(cx - S(14), cy - S(14), S(28), S(28), S(6), i == 0 ? TH.bad : TH.btn_hot, 255); if (i == 0) gl = RGB(255, 255, 255); else gl = TH.text; }
        float k = (float)S(5), lw = 1.5f * (float)UIP / 100.0f;
        if (i == 0) { gfx_line((float)cx - k, (float)cy - k, (float)cx + k, (float)cy + k, lw, gl, 255); gfx_line((float)cx - k, (float)cy + k, (float)cx + k, (float)cy - k, lw, gl, 255); }
        else if (i == 1) {
            if (w->maximized || w->snapped) { gfx_rrect_line(cx - S(3), cy - S(6), S(9), S(9), 0, gl, 255); gfx_rrect_line(cx - S(6), cy - S(3), S(9), S(9), 0, gl, 255); }
            else gfx_rrect_line(cx - S(5), cy - S(5), S(10), S(10), 0, gl, 255);
        } else gfx_line((float)cx - k, (float)cy, (float)cx + k, (float)cy, lw, gl, 255);
    }
    /* content */
    w->cx = w->x + 1; w->cy = w->y + th; w->cw = w->w - 2; w->ch = w->h - th - 1;
    w->nz = 0;
    gfx_clip(w->cx, w->cy, w->cw, w->ch);
    if (w->app->draw) w->app->draw(w, w->cx, w->cy, w->cw, w->ch);
    gfx_unclip();
}

static const char *DAYS[] = { "dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam." };
static const char *MONTHS[] = { "janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc." };

static int menu_items[16], menu_n;
static void menu_rebuild(void) {
    menu_n = 0;
    for (int i = 0; i < NAPPS && menu_n < 16; i++) {
        if (menu_filter[0]) {
            char a[64], b[64]; int k = 0;
            for (const char *s = APPS[i]->title; *s && k < 62; s++) a[k++] = (char)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s); a[k] = 0;
            k = 0; for (const char *s = menu_filter; *s && k < 62; s++) b[k++] = (char)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s); b[k] = 0;
            if (!strstr(a, b)) continue;
        }
        menu_items[menu_n++] = i;
    }
    if (menu_sel >= menu_n) menu_sel = menu_n ? menu_n - 1 : 0;
}

/* ---- Plasma-style bottom panel: launcher, task icons, tray, clock, show-desktop ---- */
static int task_w(void) { return S(48); }
static int task_x0(void) { return S(48) + S(4); }
enum { Z_SHOWDESK = 7 };
static void draw_panel(void) {
    int ph = panel_h(), py = SH - ph;
    gfx_fill(0, py, SW, ph, TH.panel, 250);
    gfx_hline(0, py, SW, TH.line, 255);
    /* launcher: the flat logo, nothing else */
    int lw = S(48);
    if (menu_open) gfx_fill(0, py, lw, ph, TH.btn, 255); else if (hover(0, py, lw, ph)) gfx_fill(0, py, lw, ph, TH.btn, 200);
    gfx_logo(S(12), py + (ph - S(24)) / 2, S(24), TH.text, menu_open ? TH.btn : (hover(0, py, lw, ph) ? TH.btn : TH.panel));
    szone(0, py, lw, ph, Z_MENU);
    /* task icons: line icons in the text colour; a short accent bar marks running apps */
    int n = NAPPS - 1, tw = task_w(), isz = S(20), hovered = -1;
    for (int i = 0; i < n; i++) {
        int x = task_x0() + i * tw, running = 0, active = 0;
        for (int k = 0; k < nwins; k++) if (wins[k]->app == APPS[i]) { running = 1; if (wins[k] == focus_win && !wins[k]->minimized) active = 1; }
        int hot = hover(x, py, tw, ph); if (hot) hovered = i;
        if (active) gfx_rrect(x + S(2), py + S(5), tw - S(4), ph - S(10), S(6), TH.btn, 255); else if (hot) gfx_rrect(x + S(2), py + S(5), tw - S(4), ph - S(10), S(6), TH.btn, 160);
        Img *im = img_get(APPS[i]->icon, isz); if (im) img_draw_tint(im, x + (tw - isz) / 2, py + (ph - isz) / 2 - S(1), 255, running || hot ? TH.text : TH.dim);
        if (running) gfx_rrect(x + tw / 2 - S(6), py + ph - S(4), S(12), S(2), S(1), active ? TH.accent : TH.dim, 255);
        szone(x, py, tw, ph, Z_DOCK + i);
    }
    /* right side: show-desktop strip, clock, tray */
    int sd = S(8);
    gfx_fill(SW - sd, py + S(8), 1, ph - S(16), TH.line, 255);
    if (hover(SW - sd, py, sd, ph)) gfx_fill(SW - sd, py, sd, ph, TH.btn_hot, 255);
    szone(SW - sd, py, sd, ph, Z_SHOWDESK);
    time_t now = time(NULL); struct tm lt; char hm[16] = "--:--", date[40] = "";
    if (localtime_r(&now, &lt)) { strftime(hm, sizeof hm, "%H:%M", &lt); snprintf(date, sizeof date, "%s %d %s", DAYS[lt.tm_wday], lt.tm_mday, MONTHS[lt.tm_mon]); }
    int px = S(14), cw1 = text_w(F_BOLD, hm, px), cw2 = text_w(F_UI, date, S(12)), cw = cw1 > cw2 ? cw1 : cw2;
    int cx = SW - sd - S(12) - cw;
    text_draw(F_BOLD, cx + (cw - cw1) / 2, py + S(5), hm, TH.text, px);
    text_draw(F_UI, cx + (cw - cw2) / 2, py + S(23), date, TH.dim, S(12));
    int x = cx - S(8), ih = ph - S(12), ty = py + S(6);
    /* tray items right-to-left: theme, keyboard layout, network */
    x -= S(36); if (hover(x, ty, S(36), ih)) gfx_rrect(x, ty, S(36), ih, S(6), TH.btn, 255);
    { Img *ti = img_get(TH.dark ? "moon" : "sun", S(18)); if (ti) img_draw_tint(ti, x + S(9), ty + (ih - S(18)) / 2, 255, TH.text); }
    szone(x, ty, S(36), ih, Z_THEME);
    const char *lay = layout ? "FR" : "US"; int lwid = text_w(F_BOLD, lay, S(12)) + S(20);
    x -= lwid; if (hover(x, ty, lwid, ih)) gfx_rrect(x, ty, lwid, ih, S(6), TH.btn, 255);
    text_draw(F_BOLD, x + S(10), ty + (ih - font_line_h(F_BOLD, S(12))) / 2, lay, TH.text, S(12)); szone(x, ty, lwid, ih, Z_LAYOUT);
    int nw = S(34) + text_w(F_UI, net_text, S(12)) + S(10);
    x -= nw; if (hover(x, ty, nw, ih)) gfx_rrect(x, ty, nw, ih, S(6), TH.btn, 255);
    Img *ni = img_get(net_up ? "signal" : "plug", S(18)); if (ni) img_draw_tint(ni, x + S(8), ty + (ih - S(18)) / 2, 255, net_up ? TH.text : TH.dim);
    text_draw(F_UI, x + S(32), ty + (ih - font_line_h(F_UI, S(12))) / 2, net_text, net_up ? TH.text : TH.dim, S(12)); szone(x, ty, nw, ih, Z_NET);
    /* tooltip for the hovered task icon */
    if (hovered >= 0) {
        const char *t = APPS[hovered]->title; int tpx = S(12), ttw = text_w(F_UI, t, tpx) + S(20);
        int ix = task_x0() + hovered * tw + tw / 2, tx = ix - ttw / 2, tyy = py - S(36);
        gfx_shadow(tx, tyy, ttw, S(28), S(6), S(10), 90);
        gfx_rrect(tx, tyy, ttw, S(28), S(6), TH.win_title, 255); gfx_rrect_line(tx, tyy, ttw, S(28), S(6), TH.line, 255);
        text_draw(F_UI, tx + S(10), tyy + (S(28) - font_line_h(F_UI, tpx)) / 2, t, TH.text, tpx);
    }
}

/* ---- Kickoff-style application launcher ---- */
static void draw_menu(void) {
    menu_rebuild();
    int rowh = S(52), w = S(440), headh = S(64), foot = S(60);
    int h = headh + S(8) + (menu_n ? menu_n : 1) * rowh + foot;
    int x = S(4), y = SH - panel_h() - h - S(4);
    gfx_shadow(x, y, w, h, S(8), S(24), 140);
    gfx_rrect(x, y, w, h, S(8), TH.win, 255);
    gfx_clip(x, y, w, headh); gfx_rrect(x, y, w, headh + S(8), S(8), TH.win_title, 255); gfx_unclip();
    gfx_logo(x + S(16), y + (headh - S(28)) / 2, S(28), TH.text, TH.win_title);
    text_draw(F_BOLD, x + S(56), y + S(14), "MiaouOS", TH.text, S(16));
    text_draw(F_UI, x + S(56), y + S(36), "Le Linux des chats", TH.dim, S(12));
    ui_field(x + S(190), y + (headh - S(36)) / 2, w - S(206), S(36), menu_filter, 1, "Rechercher");
    gfx_hline(x, y + headh, w, TH.line, 255);
    int ry = y + headh + S(8);
    if (!menu_n) text_draw(F_UI, x + S(20), ry + S(14), "Aucune application trouvée", TH.dim, S(14));
    for (int i = 0; i < menu_n; i++) {
        App *a = APPS[menu_items[i]];
        int hot = hover(x + S(8), ry, w - S(16), rowh - S(4));
        if (i == menu_sel || hot) { gfx_rrect(x + S(8), ry, w - S(16), rowh - S(4), S(6), i == menu_sel ? TH.btn_hot : TH.btn, 255); }
        gfx_rrect(x + S(16), ry + S(6), S(36), S(36), S(8), TH.btn, 255); gfx_rrect_line(x + S(16), ry + S(6), S(36), S(36), S(8), TH.line, 255);
        Img *im = img_get(a->icon, S(20)); if (im) img_draw(im, x + S(24), ry + S(14), 255);
        text_draw(F_BOLD, x + S(64), ry + S(7), a->title, TH.text, S(14));
        text_draw(F_UI, x + S(64), ry + S(27), a->desc, TH.dim, S(12));
        szone(x + S(8), ry, w - S(16), rowh - S(4), Z_MI + i);
        ry += rowh;
    }
    if (!menu_n) ry += rowh;
    int fy = y + h - foot;
    gfx_fill(x + 1, fy, w - 2, foot - 1, TH.win_title, 255); gfx_hline(x, fy, w, TH.line, 255);
    int bw = (w - S(28) - S(16)) / 3, by = fy + S(12);
    ui_button(NULL, x + S(14), by, bw, S(36), "Console", "console", 0, 0); szone(x + S(14), by, bw, S(36), Z_PWC);
    ui_button(NULL, x + S(22) + bw, by, bw, S(36), "Redémarrer", "reboot", 0, 0); szone(x + S(22) + bw, by, bw, S(36), Z_PWR);
    ui_button(NULL, x + S(30) + 2 * bw, by, bw, S(36), "Éteindre", "power", 0, 1); szone(x + S(30) + 2 * bw, by, bw, S(36), Z_PWO);
}

static const char *CTX_ITEMS[] = { "Nouveau terminal", "Fichiers", "Navigateur", "Changer de thème", "Paramètres", "Afficher le bureau" };
static void draw_ctx(void) {
    int n = 6, rowh = S(32), w = S(232), h = n * rowh + S(12);
    int x = ctx_x, y = ctx_y;
    if (x + w > SW) x = SW - w - S(4);
    if (y + h > SH - panel_h()) y = SH - panel_h() - h - S(4);
    ctx_x = x; ctx_y = y;
    gfx_shadow(x, y, w, h, S(8), S(16), 130);
    gfx_rrect(x, y, w, h, S(8), TH.win_title, 255);
    gfx_rrect_line(x, y, w, h, S(8), TH.line, 255);
    for (int i = 0; i < n; i++) {
        int ry = y + S(6) + i * rowh, hot = hover(x + S(4), ry, w - S(8), rowh);
        if (hot) gfx_rrect(x + S(4), ry, w - S(8), rowh, S(4), TH.btn_hot, 255);
        text_draw(F_UI, x + S(16), ry + (rowh - font_line_h(F_UI, S(14))) / 2, CTX_ITEMS[i], TH.text, S(14));
        szone(x + S(4), ry, w - S(8), rowh, Z_CTX + i);
    }
}

/* desktop icons (double-click to open), like a folder view */
static const struct { int app; const char *label; } DESK[] = { { 1, "Accueil" }, { 0, "Terminal" }, { 2, "Navigateur" }, { 3, "Boutique" }, { 6, "Paramètres" } };
static int desk_sel = -1;
static void desk_icon_rect(int k, int *x, int *y, int *w, int *h) { *x = S(16); *y = S(16) + k * S(96); *w = S(88); *h = S(88); }
static int desk_icon_hit(int mx, int my) {
    for (int k = 0; k < 5; k++) { int x, y, w, h; desk_icon_rect(k, &x, &y, &w, &h); if (mx >= x && mx < x + w && my >= y && my < y + h) return k; }
    return -1;
}
static void draw_desktop_icons(void) {
    for (int k = 0; k < 5; k++) {
        int x, y, w, h; desk_icon_rect(k, &x, &y, &w, &h);
        if (y + h > SH - panel_h()) break;
        int hot = hover(x, y, w, h);
        if (k == desk_sel) gfx_rrect(x, y, w, h, S(8), RGB(0, 0, 0), 120); else if (hot) gfx_rrect(x, y, w, h, S(8), RGB(0, 0, 0), 70);
        int tile = S(48); int tx0 = x + (w - tile) / 2, ty0 = y + S(8);
        gfx_rrect(tx0, ty0, tile, tile, S(12), TH.win, 235); gfx_rrect_line(tx0, ty0, tile, tile, S(12), TH.line, 255);
        Img *im = img_get(APPS[DESK[k].app]->icon, S(24)); if (im) img_draw(im, tx0 + (tile - S(24)) / 2, ty0 + (tile - S(24)) / 2, 255);
        int tw = text_w(F_UI, DESK[k].label, S(12)), tx = x + (w - tw) / 2, ty = y + S(8) + tile + S(8);
        text_draw(F_UI, tx + 1, ty + 1, DESK[k].label, RGB(0, 0, 0), S(12));
        text_draw(F_UI, tx, ty, DESK[k].label, RGB(255, 255, 255), S(12));
    }
}

static void draw_toasts(void) {
    long t = now_ms(); int y = SH - panel_h() - S(16);
    for (int i = 0; i < 4; i++) {
        if (toasts[i].until < t) continue;
        int px = S(14), tw = text_w(F_UI, toasts[i].msg, px) + S(40), th = S(44), x = SW - tw - S(16);
        y -= th;
        gfx_shadow(x, y, tw, th, S(8), S(16), 120);
        gfx_rrect(x, y, tw, th, S(8), TH.win_title, 255);
        gfx_rrect_line(x, y, tw, th, S(8), TH.line, 255);
        text_draw(F_UI, x + S(20), y + (th - font_line_h(F_UI, px)) / 2, toasts[i].msg, TH.text, px);
        y -= S(8);
    }
}

static void draw_scene(void) {
    nsz = 0; want_blink = 0;
    Img *wp = wallpaper(theme_dark);
    if (wp) gfx_blit(wp->px, wp->w, wp->h, 0, 0); else gfx_fill(0, 0, SW, SH, TH.bg, 255);
    draw_desktop_icons();
    for (int i = 0; i < nwins; i++) draw_window(wins[i]);
    draw_panel();
    if (menu_open) draw_menu();
    if (ctx_open) draw_ctx();
    draw_toasts();
}

/* software cursor drawn straight into the framebuffer (the scene stays cursor-free) */
static const char *CUR[] = {
    "#           ", "##          ", "#+#         ", "#++#        ", "#+++#       ", "#++++#      ", "#+++++#     ", "#++++++#    ",
    "#+++++++#   ", "#++++++++#  ", "#+++++#####", "#++#++#     ", "#+# #++#    ", "##  #++#    ", "#    #++#   ", "     #++#   ", "      ##    " };
static int cur_dx, cur_dy;
static void cursor_scale(int *s) { *s = (UIP + 50) / 100; if (*s < 1) *s = 1; }
static void cursor_erase(void) { int s; cursor_scale(&s); gfx_present_rect(cur_dx - 1, cur_dy - 1, 14 * s + 2, 19 * s + 2); }
static void cursor_draw(void) {
    int s; cursor_scale(&s);
    u32 fill = RGB(255, 255, 255), edge = RGB(20, 20, 21);
    for (int r = 0; r < 17; r++)
        for (int c = 0; c < 12; c++) {
            char ch = CUR[r][c];
            if (ch == ' ') continue;
            u32 col = ch == '#' ? edge : fill;
            for (int yy = 0; yy < s; yy++) for (int xx = 0; xx < s; xx++) gfx_fb_pixel(mouse_x + c * s + xx, mouse_y + r * s + yy, col);
        }
    cur_dx = mouse_x; cur_dy = mouse_y;
}
static void present_all(void) { gfx_present(); cursor_draw(); }

/* ================================================================== hover signature */
static int hot_compute(void) {
    for (int i = nsz - 1; i >= 0; i--) if (hover(sz[i].x, sz[i].y, sz[i].w, sz[i].h)) return 1000 + sz[i].id;
    Win *w = win_at(mouse_x, mouse_y);
    if (!w) return 0;
    if (mouse_y < w->y + title_h() && mouse_x > w->x + w->w - S(110)) return 5000 + (int)w->serial;
    for (int i = w->nz - 1; i >= 0; i--) if (hover(w->z[i].x, w->z[i].y, w->z[i].w, w->z[i].h)) return (int)w->serial * 10000 + w->z[i].id + 1;
    return 2;
}

/* ================================================================== power / console */
static void restore_tty(void) {
    if (saved_kbmode >= 0 && ttyfd >= 0) { ioctl(ttyfd, KDSKBMODE, saved_kbmode); saved_kbmode = -1; }
    if (raw_ok) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_term); raw_ok = 0; }
    if (graphics && ttyfd >= 0) { ioctl(ttyfd, KDSETMODE, KD_TEXT); graphics = 0; }
}
void desk_to_console(void) {
    restore_tty(); gfx_close();
    execl("/bin/miaoudesk", "miaoudesk", (char *)NULL);
    _exit(127);
}
void power_action(int reboot_flag) {
    slog("MIAOUDESK_POWER %s", reboot_flag ? "reboot" : "poweroff");
    sync();
    restore_tty();
    reboot(reboot_flag ? RB_AUTOBOOT : RB_POWER_OFF);
}
static void on_signal(int sig) { restore_tty(); _exit(128 + sig); }
/* Crash handler: report the signal and a raw backtrace on the serial console (resolve with addr2line), then leave. */
static void on_crash(int sig) {
    int fd = open("/dev/ttyS0", O_WRONLY | O_NOCTTY);
    if (fd >= 0) {
        char b[64]; int n = snprintf(b, sizeof b, "MIAOUDESK_CRASH signal %d\n", sig); if (write(fd, b, (size_t)n) < 0) {}
        void *bt[24]; int k = backtrace(bt, 24); backtrace_symbols_fd(bt, k, fd);
        close(fd);
    }
    restore_tty();
    _exit(128 + sig);
}

void desk_mode_changed(void) {
    int ax, ay, aw, ah; work_area(&ax, &ay, &aw, &ah);
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        if (w->maximized) { w->x = ax; w->y = ay; w->w = aw; w->h = ah; }
        if (w->w > SW) w->w = SW; if (w->h > SH - panel_h()) w->h = SH - panel_h();
        if (w->x + w->w > SW) w->x = SW - w->w; if (w->x < 0) w->x = 0;
        if (w->y + w->h > SH) w->y = SH - w->h; if (w->y < 0) w->y = 0;
    }
    mouse_x = mouse_x >= SW ? SW - 1 : mouse_x; mouse_y = mouse_y >= SH ? SH - 1 : mouse_y;
    dirty();
}

/* ================================================================== interaction */
static Win *drag_win, *rs_win, *grab_win;
static int drag_dx, drag_dy, rs_edge, rs_ox, rs_oy, rs_ow, rs_oh, rs_mx, rs_my;
static long last_click_t; static int last_cx, last_cy;
static int left_down, mods;

static void toggle_theme(void) {
    theme_dark = !theme_dark; theme_apply(); cfg_set("theme", theme_dark ? "dark" : "light");
    toast(theme_dark ? "Thème : Nuit féline" : "Thème : Jour pastel");
}
static void menu_launch(int idx) {
    if (idx < 0 || idx >= menu_n) return;
    const char *id = APPS[menu_items[idx]]->id;
    menu_open = 0; menu_filter[0] = 0;
    open_app(id);
}
static void shell_action(int id) {
    if (id == Z_MENU) { menu_open = !menu_open; menu_filter[0] = 0; menu_sel = 0; ctx_open = 0; }
    else if (id == Z_THEME) toggle_theme();
    else if (id == Z_LAYOUT) { kbd_set_layout(!layout); toast("Clavier : %s", layout ? "Français (AZERTY)" : "English (QWERTY)"); }
    else if (id == Z_SHOWDESK) show_desktop();
    else if (id == Z_NET) open_app("network");
    else if (id >= Z_DOCK && id < Z_DOCK + 50) {
        int i = id - Z_DOCK; menu_open = 0;
        Win *found = NULL;
        for (int k = 0; k < nwins; k++) if (wins[k]->app == APPS[i]) found = wins[k];
        if (!found) open_app(APPS[i]->id);
        else if (found == focus_win && !found->minimized) { found->minimized = 1; focus_win = NULL; for (int k = nwins - 1; k >= 0; k--) if (!wins[k]->minimized) { focus_win = wins[k]; break; } }
        else raise_win(found);
    }
    else if (id >= Z_MI && id < Z_MI + 20) menu_launch(id - Z_MI);
    else if (id == Z_PWC) desk_to_console();
    else if (id == Z_PWR) power_action(1);
    else if (id == Z_PWO) power_action(0);
    else if (id >= Z_CTX && id < Z_CTX + 10) {
        ctx_open = 0;
        switch (id - Z_CTX) { case 0: open_app("terminal"); break; case 1: open_app("files"); break; case 2: open_app("browser"); break;
                              case 3: toggle_theme(); break; case 4: open_app("settings"); break; case 5: show_desktop(); break; }
    }
    dirty();
}
static int shell_click(int x, int y) {
    for (int i = nsz - 1; i >= 0; i--)
        if (x >= sz[i].x && x < sz[i].x + sz[i].w && y >= sz[i].y && y < sz[i].y + sz[i].h) { shell_action(sz[i].id); return 1; }
    return 0;
}

static void mouse_left_down(int x, int y) {
    slog("MIAOUDESK_CLICK %d %d", x, y);
    long t = now_ms();
    int dbl = (t - last_click_t < 450 && abs(x - last_cx) < 6 && abs(y - last_cy) < 6);
    last_click_t = dbl ? 0 : t; last_cx = x; last_cy = y;
    if (menu_open || ctx_open) {
        int consumed = shell_click(x, y);
        if (!consumed) { menu_open = 0; ctx_open = 0; if (!win_at(x, y)) { dirty(); return; } } else return;
    }
    if (shell_click(x, y)) return;
    Win *w = win_at(x, y);
    if (!w) {
        int k = desk_icon_hit(x, y);
        if (k >= 0) { desk_sel = k; if (dbl) open_app(APPS[DESK[k].app]->id); } else desk_sel = -1;
        dirty(); return;
    }
    raise_win(w);
    int th = title_h();
    if (!w->maximized && !w->snapped) {
        int e = 0;
        if (x < w->x + EDGE) e |= 1; if (x >= w->x + w->w - EDGE) e |= 2;
        if (y < w->y + EDGE) e |= 4; if (y >= w->y + w->h - EDGE) e |= 8;
        if (e) { rs_win = w; rs_edge = e; rs_ox = w->x; rs_oy = w->y; rs_ow = w->w; rs_oh = w->h; rs_mx = x; rs_my = y; return; }
    }
    if (y < w->y + th) {
        int c = ctrl_hit(w, x, y);
        if (c == 1) { close_win(w); return; }
        if (c == 2) { w->minimized = 1; focus_win = NULL; for (int k = nwins - 1; k >= 0; k--) if (!wins[k]->minimized) { focus_win = wins[k]; break; } dirty(); return; }
        if (c == 3) { win_maximize(w, !w->maximized); return; }
        if (dbl) { win_maximize(w, !w->maximized && !w->snapped); return; }
        drag_win = w; drag_dx = x - w->x; drag_dy = y - w->y;
        return;
    }
    grab_win = w;
    for (int i = w->nz - 1; i >= 0; i--)
        if (x >= w->z[i].x && x < w->z[i].x + w->z[i].w && y >= w->z[i].y && y < w->z[i].y + w->z[i].h) {
            if (w->app->action) w->app->action(w, w->z[i].id, x - w->cx, y - w->cy, 1);
            break;
        }
    if (w->app->mouse) w->app->mouse(w, M_DOWN, x - w->cx, y - w->cy, dbl);
    dirty();
}
static void mouse_right_down(int x, int y) {
    if (menu_open || ctx_open) { menu_open = 0; ctx_open = 0; dirty(); }
    Win *w = win_at(x, y);
    if (!w) { if (y < SH - panel_h()) { ctx_open = 1; ctx_x = x; ctx_y = y; } dirty(); return; }
    raise_win(w);
    if (y < w->y + title_h()) return;
    for (int i = w->nz - 1; i >= 0; i--)
        if (x >= w->z[i].x && x < w->z[i].x + w->z[i].w && y >= w->z[i].y && y < w->z[i].y + w->z[i].h) {
            if (w->app->action) w->app->action(w, w->z[i].id, x - w->cx, y - w->cy, 2);
            break;
        }
    if (w->app->mouse) w->app->mouse(w, M_RDOWN, x - w->cx, y - w->cy, 0);
    dirty();
}
static void mouse_left_up(int x, int y) {
    if (drag_win) {
        Win *w = drag_win; drag_win = NULL;
        if (y <= 2) win_maximize(w, 1);
        else if (x <= 2) win_snap(w, -1);
        else if (x >= SW - 3) win_snap(w, 1);
        dirty();
    }
    rs_win = NULL;
    if (grab_win) { Win *w = grab_win; grab_win = NULL; if (win_index(w) >= 0 && w->app->mouse) w->app->mouse(w, M_UP, x - w->cx, y - w->cy, 0); dirty(); }
}
static void mouse_moved(int x, int y) {
    if (drag_win) {
        Win *w = drag_win;
        if (w->maximized || w->snapped) {         /* un-maximize while dragging */
            int nw = w->ow ? w->ow : S(w->app->w);
            w->maximized = 0; w->snapped = 0;
            drag_dx = nw * (x - w->x) / (w->w ? w->w : 1); w->w = nw; w->h = w->oh ? w->oh : S(w->app->h);
        }
        w->x = x - drag_dx; w->y = y - drag_dy;
        if (w->y < 0) w->y = 0;
        if (w->y > SH - panel_h() - S(40)) w->y = SH - panel_h() - S(40);
        dirty(); return;
    }
    if (rs_win) {
        Win *w = rs_win; int minw = S(360), minh = S(240);
        int dx = x - rs_mx, dy = y - rs_my;
        int nx = rs_ox, ny = rs_oy, nw = rs_ow, nh = rs_oh;
        if (rs_edge & 2) nw = rs_ow + dx;
        if (rs_edge & 8) nh = rs_oh + dy;
        if (rs_edge & 1) { nw = rs_ow - dx; nx = rs_ox + dx; }
        if (rs_edge & 4) { nh = rs_oh - dy; ny = rs_oy + dy; }
        if (nw < minw) { if (rs_edge & 1) nx -= minw - nw; nw = minw; }
        if (nh < minh) { if (rs_edge & 4) ny -= minh - nh; nh = minh; }
        if (ny < 0) { nh += ny; ny = 0; }
        w->x = nx; w->y = ny; w->w = nw; w->h = nh;
        dirty(); return;
    }
    if (grab_win && win_index(grab_win) >= 0 && grab_win->app->mouse) { grab_win->app->mouse(grab_win, M_MOVE, x - grab_win->cx, y - grab_win->cy, left_down); dirty(); return; }
}
static void mouse_wheel(int dir) {
    Win *w = win_at(mouse_x, mouse_y);
    if (w && w->app->mouse && mouse_y > w->y + title_h()) { w->app->mouse(w, M_WHEEL, mouse_x - w->cx, mouse_y - w->cy, dir); dirty(); }
}

static void menu_key(int key) {
    int n = (int)strlen(menu_filter);
    if (key == K_ESC) { menu_open = 0; }
    else if (key == K_UP) { if (menu_sel > 0) menu_sel--; }
    else if (key == K_DOWN) { if (menu_sel < menu_n - 1) menu_sel++; }
    else if (key == K_ENTER) { menu_rebuild(); menu_launch(menu_sel); }
    else if (key == K_BS) { if (n) { while (n > 0 && (menu_filter[n - 1] & 0xc0) == 0x80) n--; if (n) n--; menu_filter[n] = 0; menu_sel = 0; } }
    else if (key >= 32 && key < 0x200000 && n < 40) { n += utf8_put(menu_filter + n, key); menu_filter[n] = 0; menu_sel = 0; }
    dirty();
}

static void dispatch_key(int key, int m) {
    if (key == K_TAB && (m & MOD_ALT)) { cycle_focus((m & MOD_SHIFT) ? -1 : 1); return; }
    if (key == K_F1 + 3 && (m & MOD_ALT)) { if (focus_win) close_win(focus_win); return; }
    if ((m & MOD_CTRL) && (m & MOD_ALT) && (key == 't' || key == 'T')) { open_app("terminal"); return; }
    if (m & MOD_META) {
        switch (key) {
        case 't': case 'T': open_app("terminal"); return;
        case 'e': case 'E': open_app("files"); return;
        case 'b': case 'B': open_app("browser"); return;
        case 'd': case 'D': show_desktop(); return;
        case K_LEFT: if (focus_win) win_snap(focus_win, -1); return;
        case K_RIGHT: if (focus_win) win_snap(focus_win, 1); return;
        case K_UP: if (focus_win) win_maximize(focus_win, !focus_win->maximized); return;
        case K_DOWN: if (focus_win) { if (focus_win->maximized || focus_win->snapped) win_maximize(focus_win, 0); else { focus_win->minimized = 1; focus_win = NULL; for (int k = nwins - 1; k >= 0; k--) if (!wins[k]->minimized) { focus_win = wins[k]; break; } dirty(); } } return;
        default: break;
        }
    }
    if (menu_open) { menu_key(key); return; }
    if (ctx_open) { if (key == K_ESC) { ctx_open = 0; dirty(); } return; }
    if (focus_win && focus_win->app->key) { focus_win->app->key(focus_win, key, m); dirty(); }
}

static int shifts, ctrls, alt_l, altgr, meta_held, meta_solo, caps;
static void scancode(int code, int down) {
    switch (code) {
    case 42: case 54: shifts += down ? 1 : -1; if (shifts < 0) shifts = 0; return;
    case 29: case 97: ctrls += down ? 1 : -1; if (ctrls < 0) ctrls = 0; return;
    case 56: alt_l = down; return;
    case 100: altgr = down; return;
    case 125: case 126:
        if (down) { meta_held = 1; meta_solo = 1; }
        else { meta_held = 0; if (meta_solo) { shell_action(Z_MENU); } meta_solo = 0; }
        return;
    case 58: if (down) caps = !caps; return;
    default: break;
    }
    if (!down) return;
    meta_solo = 0;
    mods = (shifts ? MOD_SHIFT : 0) | (ctrls ? MOD_CTRL : 0) | (alt_l ? MOD_ALT : 0) | (meta_held ? MOD_META : 0);
    int key = 0;
    switch (code) {
    case 1: key = K_ESC; break; case 14: key = K_BS; break; case 15: key = K_TAB; break;
    case 28: case 96: key = K_ENTER; break;
    case 102: key = K_HOME; break; case 103: key = K_UP; break; case 104: key = K_PGUP; break; case 105: key = K_LEFT; break;
    case 106: key = K_RIGHT; break; case 107: key = K_END; break; case 108: key = K_DOWN; break; case 109: key = K_PGDN; break;
    case 110: key = K_INSERT; break; case 111: key = K_DELETE; break;
    case 87: key = K_F1 + 10; break; case 88: key = K_F1 + 11; break;
    default:
        if (code >= 59 && code <= 68) key = K_F1 + (code - 59);
        else if (code < 128) {
            int st = (shifts ? 1 : 0) | (altgr ? 2 : 0);
            u32 cp = kmap[layout][st][code];
            if (caps && !altgr && code < 128) {   /* caps lock affects letters only */
                u32 base = kmap[layout][0][code], up = kmap[layout][1][code];
                if (base >= 'a' && base <= 'z') cp = shifts ? base : up;
            }
            key = (int)cp;
        }
    }
    if (key) dispatch_key(key, mods);
}

/* ================================================================== main */
/* ---- pointer input: evdev (relative mice, absolute tablets / touchscreens) with /dev/input/mice as fallback ---- */
#define MAXEV 6
static struct { int fd, abs, minx, maxx, miny, maxy; int ax, ay, has_abs, rdx, rdy, wheel; } evdevs[MAXEV];
static int nev;
static int p_left, p_right, move_pending;
static void pointer_event(int has_abs, int absx, int absy, int dx, int dy, int nl, int nr, int wheel) {
    int moved = 0;
    if (has_abs) { if (absx != mouse_x || absy != mouse_y) { mouse_x = absx; mouse_y = absy; moved = 1; } }
    else if (dx || dy) { mouse_x += dx; mouse_y += dy; moved = 1; }
    if (mouse_x < 0) mouse_x = 0; if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= SW) mouse_x = SW - 1; if (mouse_y >= SH) mouse_y = SH - 1;
    if (moved) { mouse_moved(mouse_x, mouse_y); move_pending = 1; }
    if (nl && !p_left) { left_down = 1; mouse_left_down(mouse_x, mouse_y); }
    if (!nl && p_left) { left_down = 0; mouse_left_up(mouse_x, mouse_y); }
    if (nr && !p_right) mouse_right_down(mouse_x, mouse_y);
    p_left = nl; p_right = nr;
    if (wheel) mouse_wheel(wheel < 0 ? -1 : 1);
}
#define BITSET(arr, b) ((arr)[(b) / 8] & (1 << ((b) % 8)))
static void evdev_scan(void) {
    for (int i = 0; i < 32 && nev < MAXEV; i++) {
        char path[32]; snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned char ev[(EV_MAX + 8) / 8 + 1] = { 0 }, key[(KEY_MAX + 8) / 8 + 1] = { 0 }, rel[8] = { 0 }, abs[(ABS_MAX + 8) / 8 + 1] = { 0 };
        ioctl(fd, EVIOCGBIT(0, sizeof ev), ev); ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key);
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel); ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
        int btn = BITSET(key, BTN_LEFT) || BITSET(key, BTN_TOUCH);
        int is_rel = BITSET(ev, EV_REL) && BITSET(rel, REL_X) && BITSET(rel, REL_Y);
        int is_abs = BITSET(ev, EV_ABS) && BITSET(abs, ABS_X) && BITSET(abs, ABS_Y);
        if (!btn || !(is_rel || is_abs)) { close(fd); continue; }
        evdevs[nev].fd = fd; evdevs[nev].abs = is_abs && !is_rel;
        if (evdevs[nev].abs) {
            struct input_absinfo ax, ay;
            if (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0 || ax.maximum <= ax.minimum || ay.maximum <= ay.minimum) { close(fd); continue; }
            evdevs[nev].minx = ax.minimum; evdevs[nev].maxx = ax.maximum; evdevs[nev].miny = ay.minimum; evdevs[nev].maxy = ay.maximum;
        }
        char name[64] = "?"; ioctl(fd, EVIOCGNAME(sizeof name), name);
        slog("MIAOUDESK_POINTER %s %s \"%s\"", path, evdevs[nev].abs ? "absolute" : "relative", name);
        nev++;
    }
}
static void evdev_read(int k) {
    struct input_event ie[32];
    ssize_t n = read(evdevs[k].fd, ie, sizeof ie);
    if (n <= 0) return;
    for (int i = 0; i < n / (ssize_t)sizeof ie[0]; i++) {
        if (ie[i].type == EV_ABS) {
            if (ie[i].code == ABS_X) { evdevs[k].ax = ie[i].value; evdevs[k].has_abs = 1; }
            else if (ie[i].code == ABS_Y) { evdevs[k].ay = ie[i].value; evdevs[k].has_abs = 1; }
        } else if (ie[i].type == EV_REL) {
            if (ie[i].code == REL_X) evdevs[k].rdx += ie[i].value; else if (ie[i].code == REL_Y) evdevs[k].rdy += ie[i].value;
            else if (ie[i].code == REL_WHEEL) evdevs[k].wheel += ie[i].value;
        } else if (ie[i].type == EV_KEY) {
            if (ie[i].code == BTN_LEFT || ie[i].code == BTN_TOUCH) p_left = p_left;   /* handled at SYN via state below */
            static int kl, kr;
            if (ie[i].code == BTN_LEFT || ie[i].code == BTN_TOUCH) kl = ie[i].value != 0;
            else if (ie[i].code == BTN_RIGHT) kr = ie[i].value != 0;
            evdevs[k].wheel += 0;
            if (ie[i].code == BTN_LEFT || ie[i].code == BTN_TOUCH || ie[i].code == BTN_RIGHT) {
                /* flush pending motion first so the click lands where the pointer is */
                int ax = mouse_x, ay = mouse_y, ha = evdevs[k].has_abs;
                if (ha) { ax = (int)((long)(evdevs[k].ax - evdevs[k].minx) * (SW - 1) / (evdevs[k].maxx - evdevs[k].minx)); ay = (int)((long)(evdevs[k].ay - evdevs[k].miny) * (SH - 1) / (evdevs[k].maxy - evdevs[k].miny)); }
                pointer_event(ha, ax, ay, evdevs[k].rdx, evdevs[k].rdy, kl, kr, 0);
                evdevs[k].rdx = evdevs[k].rdy = 0;
            }
        } else if (ie[i].type == EV_SYN) {
            int ha = evdevs[k].has_abs, ax = mouse_x, ay = mouse_y;
            if (ha) { ax = (int)((long)(evdevs[k].ax - evdevs[k].minx) * (SW - 1) / (evdevs[k].maxx - evdevs[k].minx)); ay = (int)((long)(evdevs[k].ay - evdevs[k].miny) * (SH - 1) / (evdevs[k].maxy - evdevs[k].miny)); }
            if (ha || evdevs[k].rdx || evdevs[k].rdy || evdevs[k].wheel) pointer_event(ha, ax, ay, evdevs[k].rdx, evdevs[k].rdy, p_left, p_right, evdevs[k].wheel);
            evdevs[k].rdx = evdevs[k].rdy = evdevs[k].wheel = 0;
        }
    }
}

static unsigned char kbuf[256];
static int mouse_pk = 3;

static int tick_all(void) {
    int redraw = 0;
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        if (w->app->tick && w->app->tick(w)) redraw = 1;
    }
    for (int i = nwins - 1; i >= 0; i--) if (wins[i]->closing) { close_win(wins[i]); redraw = 1; }
    return redraw;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal); signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, on_crash); signal(SIGABRT, on_crash); signal(SIGFPE, on_crash); signal(SIGBUS, on_crash);
    kbd_init(); cfg_load();
    if (gfx_open()) { slog("MIAOUDESK_ERR framebuffer unavailable"); return 1; }
    char path[128];
    F_UI = font_load("/usr/share/miaou/fonts/Nunito-Regular.ttf");
    F_BOLD = font_load("/usr/share/miaou/fonts/Nunito-Bold.ttf");
    F_MONO = font_load("/usr/share/miaou/fonts/JetBrainsMono-Regular.ttf");
    if (!F_UI || !F_BOLD || !F_MONO) { slog("MIAOUDESK_ERR fonts missing in /usr/share/miaou/fonts"); return 2; }
    (void)path;
    /* configuration + kernel command line overrides (miaou.kbd=us miaou.theme=light miaou.accent=2) */
    layout = !strcmp(cfg_get("layout", "fr"), "fr");
    theme_dark = strcmp(cfg_get("theme", "dark"), "light") != 0;
    accent_idx = atoi(cfg_get("accent", "0")) & 3;
    char *cl = read_file("/proc/cmdline", NULL, 4096);
    if (cl) {
        char *p;
        if ((p = strstr(cl, "miaou.kbd="))) layout = strncmp(p + 10, "us", 2) != 0;
        if ((p = strstr(cl, "miaou.theme="))) theme_dark = strncmp(p + 12, "light", 5) != 0;
        if ((p = strstr(cl, "miaou.accent="))) accent_idx = atoi(p + 13) & 3;
        free(cl);
    }
    const char *us = cfg_get("ui", "auto");
    if (strcmp(us, "auto")) { int v = atoi(us); if (v >= 100 && v <= 300) UIP = v; }
    else UIP = ui_auto_scale(SW);
    theme_apply();
    ttyfd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (ttyfd < 0) { slog("MIAOUDESK_ERR no /dev/tty0"); return 3; }
    if (ioctl(ttyfd, KDSETMODE, KD_GRAPHICS) == 0) graphics = 1;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_term) == 0) {
        struct termios t = saved_term; cfmakeraw(&t); t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) raw_ok = 1;
    }
    int kb = 0;
    if (ioctl(STDIN_FILENO, KDGKBMODE, &kb) == 0) { saved_kbmode = kb; if (ioctl(STDIN_FILENO, KDSKBMODE, K_MEDIUMRAW) != 0) saved_kbmode = -1; }
    ttyfd = ttyfd;   /* keep for restore */
    int medium = saved_kbmode >= 0;
    evdev_scan();
    if (!nev) {
        mousefd = open("/dev/input/mice", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (mousefd >= 0) {                       /* ask mousedev for ImPS/2 (4-byte packets with wheel) */
            static const unsigned char magic[] = { 0xf3, 0xc8, 0xf3, 0x64, 0xf3, 0x50 };
            if (write(mousefd, magic, sizeof magic) == (ssize_t)sizeof magic) { usleep(20000); unsigned char ack[16]; if (read(mousefd, ack, sizeof ack) < 0) {} mouse_pk = 4; }
            slog("MIAOUDESK_POINTER /dev/input/mice relative (fallback)");
        }
    }
    mouse_x = SW / 2; mouse_y = SH / 2;
    net_refresh();
    toast("Bienvenue sur MiaouOS");
    draw_scene(); present_all();
    slog("MIAOUDESK_GUI_OK %dx%d scale=%d", SW, SH, UIP);

    unsigned char pk[4]; int pn = 0;
    long last_tick = 0, last_draw = 0, last_net = now_ms();
    while (!quit_req) {
        struct pollfd fds[2 + MAXEV]; int nf = 0;
        fds[nf++] = (struct pollfd){ STDIN_FILENO, POLLIN, 0 };
        int mice_idx = -1; if (mousefd >= 0) { mice_idx = nf; fds[nf++] = (struct pollfd){ mousefd, POLLIN, 0 }; }
        int ev_idx = nf; for (int k = 0; k < nev; k++) fds[nf++] = (struct pollfd){ evdevs[k].fd, POLLIN, 0 };
        int pr = poll(fds, (nfds_t)nf, 25);
        if (pr < 0 && errno == EINTR) continue;
        if (pr > 0 && (fds[0].revents & POLLIN)) {
            ssize_t n = read(STDIN_FILENO, kbuf, sizeof kbuf);
            for (ssize_t i = 0; i < n; i++) {
                if (!medium) { int c = kbuf[i]; if (c == 127) c = K_BS; if (c == '\r') c = K_ENTER; dispatch_key(c, 0); continue; }
                int b = kbuf[i];
                if (b == 0) { i += 2; continue; }                 /* extended keycodes (>127): ignored */
                scancode(b & 0x7f, !(b & 0x80));
            }
        }
        for (int k = 0; k < nev; k++) if (pr > 0 && (fds[ev_idx + k].revents & POLLIN)) evdev_read(k);
        if (mice_idx >= 0 && pr > 0 && (fds[mice_idx].revents & POLLIN)) {
            unsigned char mb[128];
            ssize_t n = read(mousefd, mb, sizeof mb);
            for (ssize_t i = 0; i < n; i++) {
                if (pn == 0 && !(mb[i] & 8)) continue;
                pk[pn++] = mb[i];
                if (pn < mouse_pk) continue;
                pn = 0;
                int dx = (pk[0] & 0x10) ? (int)pk[1] - 256 : (int)pk[1];       /* 9-bit signed deltas */
                int dy = (pk[0] & 0x20) ? (int)pk[2] - 256 : (int)pk[2]; dy = -dy;
                int wheel = 0;
                if (mouse_pk == 4) { wheel = pk[3] & 0x0f; if (wheel & 8) wheel -= 16; }
                pointer_event(0, 0, 0, dx, dy, pk[0] & 1, (pk[0] >> 1) & 1, wheel);
            }
        }
        long t = now_ms();
        if (t - last_tick >= 30) {
            last_tick = t;
            if (tick_all()) dirty();
            if (t - last_net > 3000) { last_net = t; net_refresh(); }
            for (int i = 0; i < 4; i++) if (toasts[i].until && toasts[i].until < t + 40 && toasts[i].until > t - 80) dirty();
            static int clk; time_t nw = time(NULL); if ((int)(nw / 60) != clk) { clk = (int)(nw / 60); dirty(); }
            /* caret blink for focused text fields */
            static long blink; if (t / 500 != blink) { blink = t / 500; if (want_blink) dirty(); }
        }
        if (move_pending) {
            move_pending = 0;
            int h = hot_compute();
            if (h != hot_sig) { hot_sig = h; dirty(); }
            if (!need_draw) { cursor_erase(); cursor_draw(); }
        }
        if (need_draw && t - last_draw >= 14) {
            need_draw = 0; last_draw = t;
            draw_scene();
            hot_sig = hot_compute();
            present_all();
        }
    }
    restore_tty();
    return 0;
}
