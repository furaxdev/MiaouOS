/* Terminal: a real pseudo-terminal running /bin/sh, with a small VT100/ANSI emulator and scrollback. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <grp.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

#define MAXCOLS 320
#define MAXLINES 1200
typedef struct { u32 cp; unsigned char fg, bg, attr; } Cell;   /* fg/bg: 0..15, 255 = default; attr bit0 bold, bit1 reverse */
typedef struct {
    int master, pid, alive;
    int cols, rows, cw, chh, px;
    Cell **lines; int total;          /* the screen is the last `rows` lines */
    int crow, ccol, voff;
    int state, params[8], np, priv;   /* parser */
    unsigned char fg, bg, attr;
    u32 pending_utf8; int utf8_left;
    int saved_r, saved_c;
} Term;

static const u32 PAL[16] = {
    RGB(40, 40, 43), RGB(229, 110, 110), RGB(120, 200, 140), RGB(224, 176, 90), RGB(110, 150, 220), RGB(190, 130, 200), RGB(100, 190, 200), RGB(210, 210, 214),
    RGB(100, 100, 106), RGB(240, 140, 140), RGB(150, 220, 170), RGB(240, 200, 120), RGB(140, 175, 240), RGB(210, 160, 220), RGB(130, 210, 220), RGB(245, 245, 247) };
#define TBG RGB(15, 15, 16)
#define TFG RGB(224, 224, 226)

static Cell *newline_cells(void) {
    Cell *l = calloc(MAXCOLS, sizeof(Cell));
    for (int i = 0; i < MAXCOLS; i++) { l[i].cp = ' '; l[i].fg = l[i].bg = 255; }
    return l;
}
static Cell *row(Term *t, int r) { return t->lines[t->total - t->rows + r]; }
static void append_line(Term *t) {
    if (t->total >= MAXLINES) { free(t->lines[0]); memmove(t->lines, t->lines + 1, (size_t)(t->total - 1) * sizeof(Cell *)); t->total--; }
    t->lines[t->total++] = newline_cells();
}
static void scroll_up(Term *t) { append_line(t); }
static void clear_cells(Term *t, int r, int c0, int c1) {
    Cell *l = row(t, r);
    for (int c = c0; c < c1 && c < MAXCOLS; c++) { l[c].cp = ' '; l[c].fg = l[c].bg = 255; l[c].attr = 0; }
}
static void do_newline(Term *t) {
    if (t->crow < t->rows - 1) t->crow++; else scroll_up(t);
}
static void put_cp(Term *t, u32 cp) {
    if (t->ccol >= t->cols) { t->ccol = 0; do_newline(t); }
    Cell *c = &row(t, t->crow)[t->ccol];
    c->cp = cp; c->fg = t->fg; c->bg = t->bg; c->attr = t->attr;
    t->ccol++;
}
static int P(Term *t, int i, int def) { return (i < t->np && t->params[i] > 0) ? t->params[i] : def; }
static void reply(Term *t, const char *s) { if (write(t->master, s, strlen(s)) < 0) {} }

static void csi(Term *t, int fin) {
    int n;
    switch (fin) {
    case 'A': t->crow -= P(t, 0, 1); if (t->crow < 0) t->crow = 0; break;
    case 'B': t->crow += P(t, 0, 1); if (t->crow >= t->rows) t->crow = t->rows - 1; break;
    case 'C': t->ccol += P(t, 0, 1); if (t->ccol >= t->cols) t->ccol = t->cols - 1; break;
    case 'D': t->ccol -= P(t, 0, 1); if (t->ccol < 0) t->ccol = 0; break;
    case 'E': t->crow += P(t, 0, 1); if (t->crow >= t->rows) t->crow = t->rows - 1; t->ccol = 0; break;
    case 'F': t->crow -= P(t, 0, 1); if (t->crow < 0) t->crow = 0; t->ccol = 0; break;
    case 'G': t->ccol = P(t, 0, 1) - 1; if (t->ccol >= t->cols) t->ccol = t->cols - 1; break;
    case 'd': t->crow = P(t, 0, 1) - 1; if (t->crow >= t->rows) t->crow = t->rows - 1; break;
    case 'H': case 'f':
        t->crow = P(t, 0, 1) - 1; t->ccol = P(t, 1, 1) - 1;
        if (t->crow >= t->rows) t->crow = t->rows - 1; if (t->ccol >= t->cols) t->ccol = t->cols - 1; break;
    case 'J':
        n = t->np ? t->params[0] : 0;
        if (n == 0) { clear_cells(t, t->crow, t->ccol, MAXCOLS); for (int r = t->crow + 1; r < t->rows; r++) clear_cells(t, r, 0, MAXCOLS); }
        else if (n == 1) { for (int r = 0; r < t->crow; r++) clear_cells(t, r, 0, MAXCOLS); clear_cells(t, t->crow, 0, t->ccol + 1); }
        else { for (int r = 0; r < t->rows; r++) clear_cells(t, r, 0, MAXCOLS); if (n == 3) {} }
        break;
    case 'K':
        n = t->np ? t->params[0] : 0;
        if (n == 0) clear_cells(t, t->crow, t->ccol, MAXCOLS); else if (n == 1) clear_cells(t, t->crow, 0, t->ccol + 1); else clear_cells(t, t->crow, 0, MAXCOLS);
        break;
    case 'L': n = P(t, 0, 1); for (int i = 0; i < n; i++) { Cell *last = row(t, t->rows - 1); for (int r = t->rows - 1; r > t->crow; r--) t->lines[t->total - t->rows + r] = t->lines[t->total - t->rows + r - 1]; t->lines[t->total - t->rows + t->crow] = last; clear_cells(t, t->crow, 0, MAXCOLS); } break;
    case 'M': n = P(t, 0, 1); for (int i = 0; i < n; i++) { Cell *first = row(t, t->crow); for (int r = t->crow; r < t->rows - 1; r++) t->lines[t->total - t->rows + r] = t->lines[t->total - t->rows + r + 1]; t->lines[t->total - 1] = first; clear_cells(t, t->rows - 1, 0, MAXCOLS); } break;
    case 'P': n = P(t, 0, 1); { Cell *l = row(t, t->crow); for (int c = t->ccol; c < MAXCOLS; c++) l[c] = c + n < MAXCOLS ? l[c + n] : (Cell){ ' ', 255, 255, 0 }; } break;
    case '@': n = P(t, 0, 1); { Cell *l = row(t, t->crow); for (int c = MAXCOLS - 1; c >= t->ccol; c--) l[c] = c - n >= t->ccol ? l[c - n] : (Cell){ ' ', 255, 255, 0 }; } break;
    case 'X': n = P(t, 0, 1); clear_cells(t, t->crow, t->ccol, t->ccol + n); break;
    case 's': t->saved_r = t->crow; t->saved_c = t->ccol; break;
    case 'u': t->crow = t->saved_r; t->ccol = t->saved_c; break;
    case 'n': if (!t->priv && t->np && t->params[0] == 6) { char b[32]; snprintf(b, sizeof b, "\033[%d;%dR", t->crow + 1, t->ccol + 1); reply(t, b); } break;
    case 'm':
        if (!t->np) { t->fg = t->bg = 255; t->attr = 0; break; }
        for (int i = 0; i < t->np; i++) {
            int p = t->params[i];
            if (p == 0) { t->fg = t->bg = 255; t->attr = 0; }
            else if (p == 1) t->attr |= 1; else if (p == 22) t->attr &= (unsigned char)~1;
            else if (p == 7) t->attr |= 2; else if (p == 27) t->attr &= (unsigned char)~2;
            else if (p >= 30 && p <= 37) t->fg = (unsigned char)(p - 30);
            else if (p >= 40 && p <= 47) t->bg = (unsigned char)(p - 40);
            else if (p >= 90 && p <= 97) t->fg = (unsigned char)(p - 90 + 8);
            else if (p >= 100 && p <= 107) t->bg = (unsigned char)(p - 100 + 8);
            else if (p == 39) t->fg = 255; else if (p == 49) t->bg = 255;
            else if ((p == 38 || p == 48) && i + 1 < t->np) { i += t->params[i + 1] == 5 ? 2 : (t->params[i + 1] == 2 ? 4 : 1); }
        }
        break;
    default: break;
    }
}

static const unsigned char LATIN1_FOLD[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
static void term_byte(Term *t, unsigned char b) {
    switch (t->state) {
    case 1:   /* after ESC */
        if (b == '[') { t->state = 2; t->np = 0; t->priv = 0; memset(t->params, 0, sizeof t->params); }
        else if (b == ']') t->state = 3;
        else if (b == '(' || b == ')' || b == '#') t->state = 4;
        else { if (b == 'c') { t->fg = t->bg = 255; t->attr = 0; for (int r = 0; r < t->rows; r++) clear_cells(t, r, 0, MAXCOLS); t->crow = t->ccol = 0; } if (b == 'M') { if (t->crow > 0) t->crow--; } if (b == '7') { t->saved_r = t->crow; t->saved_c = t->ccol; } if (b == '8') { t->crow = t->saved_r; t->ccol = t->saved_c; } t->state = 0; }
        return;
    case 2:   /* CSI */
        if (b >= '0' && b <= '9') { if (!t->np) t->np = 1; t->params[t->np - 1] = t->params[t->np - 1] * 10 + (b - '0'); }
        else if (b == ';') { if (!t->np) t->np = 1; if (t->np < 8) t->np++; }
        else if (b == '?' || b == '>' || b == '!') t->priv = 1;
        else if (b >= 0x40 && b <= 0x7e) { csi(t, b); t->state = 0; }
        return;
    case 3: if (b == 7 || b == 0x9c || b == 27) t->state = b == 27 ? 5 : 0; return;   /* OSC: skip */
    case 4: t->state = 0; return;
    case 5: t->state = 0; return;
    default: break;
    }
    if (t->utf8_left) {
        if ((b & 0xc0) == 0x80) { t->pending_utf8 = (t->pending_utf8 << 6) | (b & 0x3f); if (--t->utf8_left == 0) put_cp(t, t->pending_utf8); return; }
        t->utf8_left = 0;
    }
    if (b >= 0xc0) { t->utf8_left = b >= 0xf0 ? 3 : b >= 0xe0 ? 2 : 1; t->pending_utf8 = b & (b >= 0xf0 ? 7 : b >= 0xe0 ? 15 : 31); return; }
    switch (b) {
    case 27: t->state = 1; return;
    case '\n': case 11: case 12: do_newline(t); return;
    case '\r': t->ccol = 0; return;
    case 8: if (t->ccol > 0) t->ccol--; return;
    case 9: t->ccol = (t->ccol / 8 + 1) * 8; if (t->ccol >= t->cols) t->ccol = t->cols - 1; return;
    case 7: return;
    default: if (b >= 32 && b != 127) put_cp(t, b); (void)LATIN1_FOLD; return;
    }
}

static void term_resize(Win *w, Term *t) {
    int cols = (w->cw - S(20)) / t->cw, rows = (w->ch - S(14)) / t->chh;
    if (cols < 20) cols = 20; if (rows < 5) rows = 5;
    if (cols > MAXCOLS) cols = MAXCOLS; if (rows > 200) rows = 200;
    if (cols == t->cols && rows == t->rows) return;
    while (t->total < rows) append_line(t);
    if (rows > t->rows && t->total > t->rows) { /* growing: keep cursor row relative to the screen */ t->crow += rows - t->rows; if (t->crow >= rows) t->crow = rows - 1; }
    else if (rows < t->rows) { t->crow -= t->rows - rows; if (t->crow < 0) t->crow = 0; }
    t->cols = cols; t->rows = rows;
    if (t->ccol >= cols) t->ccol = cols - 1;
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    if (t->master >= 0) ioctl(t->master, TIOCSWINSZ, &ws);
}

static void *term_create(Win *w) {
    Term *t = calloc(1, sizeof *t);
    t->lines = calloc(MAXLINES, sizeof(Cell *));
    t->px = S(15); t->cw = font_cell_w(F_MONO, t->px); t->chh = (int)(t->px * 1.32f);
    t->fg = t->bg = 255;
    t->master = posix_openpt(O_RDWR | O_NOCTTY);
    t->cols = 80; t->rows = 24;
    while (t->total < t->rows) append_line(t);
    if (t->master >= 0 && grantpt(t->master) == 0 && unlockpt(t->master) == 0) {
        char sn[64];
        if (ptsname_r(t->master, sn, sizeof sn) == 0) {
            fcntl(t->master, F_SETFL, O_NONBLOCK); fcntl(t->master, F_SETFD, FD_CLOEXEC);
            term_resize(w, t);
            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                int s = open(sn, O_RDWR);
                if (s < 0) _exit(127);
                ioctl(s, TIOCSCTTY, 0);
                struct winsize ws = { (unsigned short)t->rows, (unsigned short)t->cols, 0, 0 };
                ioctl(s, TIOCSWINSZ, &ws);
                dup2(s, 0); dup2(s, 1); dup2(s, 2);
                if (s > 2) close(s);
                for (int fd = 3; fd < 64; fd++) close(fd);
                const char *un = cur_user ? cur_user->name : "root";
                char ps1[300]; snprintf(ps1, sizeof ps1, "\\[\\033[1;35m\\]%s@miaouos\\[\\033[0m\\]:\\[\\033[1;36m\\]\\w\\[\\033[0m\\]\\$ ", un);
                if (cur_user && !cur_user->admin) { if (setgroups(0, NULL) < 0 || setgid((gid_t)cur_user->gid) < 0 || setuid((uid_t)cur_user->uid) < 0) _exit(1); }
                setenv("TERM", "linux", 1); setenv("HOME", cur_home, 1); setenv("USER", un, 1); setenv("LOGNAME", un, 1);
                setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/opt/miaou/bin", 1);
                setenv("PS1", ps1, 1);
                if (chdir(cur_home) < 0) { if (chdir("/") < 0) {} }
                execl("/bin/sh", "-sh", (char *)NULL);
                _exit(127);
            }
            t->pid = pid; t->alive = pid > 0;
        }
    }
    if (!t->alive) { const char *m = "Impossible de lancer le shell (pty indisponible).\r\n"; for (const char *p = m; *p; p++) term_byte(t, (unsigned char)*p); }
    return t;
}
static void term_destroy(Win *w) {
    Term *t = w->st;
    if (t->alive) { kill(t->pid, SIGHUP); kill(t->pid, SIGKILL); waitpid(t->pid, NULL, 0); }
    if (t->master >= 0) close(t->master);
    for (int i = 0; i < t->total; i++) free(t->lines[i]);
    free(t->lines); free(t);
}

static void term_draw(Win *w, int x, int y, int cw, int ch) {
    Term *t = w->st;
    gfx_fill(x, y, cw, ch, TBG, 255);
    term_resize(w, t);
    int ox = x + S(10), oy = y + S(7);
    int first = t->total - t->rows - t->voff; if (first < 0) first = 0;
    for (int r = 0; r < t->rows; r++) {
        int li = first + r; if (li >= t->total) break;
        Cell *l = t->lines[li];
        int py = oy + r * t->chh;
        for (int c = 0; c < t->cols; c++) {
            Cell *cell = &l[c];
            u32 fg = cell->fg == 255 ? TFG : PAL[cell->fg & 15], bg = cell->bg == 255 ? TBG : PAL[cell->bg & 15];
            if ((cell->attr & 1) && cell->fg < 8) fg = PAL[cell->fg + 8];
            if (cell->attr & 2) { u32 tmp = fg; fg = cell->bg == 255 ? TBG : bg; bg = cell->bg == 255 && cell->fg == 255 ? TFG : tmp; if (cell->fg == 255 && cell->bg == 255) { fg = TBG; bg = TFG; } }
            if (cell->bg != 255 || (cell->attr & 2)) gfx_fill(ox + c * t->cw, py, t->cw, t->chh, bg, 255);
            if (cell->cp > ' ') glyph_draw(F_MONO, ox + c * t->cw, py, (int)cell->cp, fg, t->px);
        }
    }
    if (t->voff == 0 && w == focus_win) {
        want_blink = 1;
        long ms = 0; { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); ms = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L; }
        if ((ms / 500) % 2 == 0) {
            int cx = ox + t->ccol * t->cw, cy = oy + t->crow * t->chh;
            gfx_fill(cx, cy, t->cw, t->chh, TH.accent, 255);
            Cell *c = &row(t, t->crow)[t->ccol];
            if (c->cp > ' ') glyph_draw(F_MONO, cx, cy, (int)c->cp, TBG, t->px);
        }
    }
    if (t->voff) {
        char m[48]; snprintf(m, sizeof m, "↑ historique (%d)", t->voff);
        int tw = text_w(F_BOLD, m, S(12)) + S(16);
        gfx_rrect(x + cw - tw - S(14), y + S(10), tw, S(24), S(8), TH.accent, 230);
        text_draw(F_BOLD, x + cw - tw - S(6), y + S(13), m, RGB(30, 10, 40), S(12));
    }
}

static void term_send(Term *t, const char *s, size_t n) { if (t->alive && write(t->master, s, n) < 0) {} }
static void term_key(Win *w, int key, int mods) {
    Term *t = w->st;
    char b[16]; int n = 0;
    if (mods & MOD_SHIFT && (key == K_PGUP || key == K_PGDN)) { t->voff += key == K_PGUP ? t->rows / 2 : -t->rows / 2; if (t->voff < 0) t->voff = 0; if (t->voff > t->total - t->rows) t->voff = t->total - t->rows; return; }
    t->voff = 0;
    switch (key) {
    case K_ENTER: b[n++] = '\r'; break; case K_BS: b[n++] = 127; break; case K_TAB: b[n++] = '\t'; break; case K_ESC: b[n++] = 27; break;
    case K_UP: memcpy(b, "\033[A", 3); n = 3; break; case K_DOWN: memcpy(b, "\033[B", 3); n = 3; break;
    case K_RIGHT: memcpy(b, "\033[C", 3); n = 3; break; case K_LEFT: memcpy(b, "\033[D", 3); n = 3; break;
    case K_HOME: memcpy(b, "\033[H", 3); n = 3; break; case K_END: memcpy(b, "\033[F", 3); n = 3; break;
    case K_DELETE: memcpy(b, "\033[3~", 4); n = 4; break; case K_PGUP: memcpy(b, "\033[5~", 4); n = 4; break; case K_PGDN: memcpy(b, "\033[6~", 4); n = 4; break;
    default:
        if (key >= 0x200000) return;
        if ((mods & MOD_CTRL) && key >= 'a' && key <= 'z') b[n++] = (char)(key - 'a' + 1);
        else if ((mods & MOD_CTRL) && key >= 'A' && key <= 'Z') b[n++] = (char)(key - 'A' + 1);
        else if ((mods & MOD_CTRL) && key == '[') b[n++] = 27;
        else n = utf8_put(b, key);
    }
    term_send(t, b, (size_t)n);
}
static void term_mouse(Win *w, int type, int x, int y, int arg) {
    (void)x; (void)y; Term *t = w->st;
    if (type == M_WHEEL) { t->voff += -arg * 3; if (t->voff < 0) t->voff = 0; if (t->voff > t->total - t->rows) t->voff = t->total - t->rows; }
}
static int term_tick(Win *w) {
    Term *t = w->st; int ch = 0; unsigned char buf[4096];
    if (t->master >= 0) {
        for (int rounds = 0; rounds < 8; rounds++) {
            ssize_t n = read(t->master, buf, sizeof buf);
            if (n > 0) { for (ssize_t i = 0; i < n; i++) term_byte(t, buf[i]); ch = 1; if (t->voff) t->voff = t->voff; }
            else break;
        }
    }
    if (t->alive) {
        int st;
        if (waitpid(t->pid, &st, WNOHANG) == t->pid) { t->alive = 0; w->closing = 1; ch = 1; }
    }
    return ch;
}

App app_terminal = { "terminal", "Terminal", "terminal", "Un vrai shell dans une fenêtre", 820, 520, term_create, term_destroy, term_draw, NULL, term_mouse, term_key, term_tick };
