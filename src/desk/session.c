/* Users, password hashing (SHA-256 with salt), the login / lock screen, and session start-up. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

/* ---------------------------------------------------------------- SHA-256 */
static const uint32_t K256[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_hex(const unsigned char *msg, size_t len, char out[65]) {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    size_t total = ((len + 9 + 63) / 64) * 64;
    unsigned char *buf = calloc(total, 1);
    memcpy(buf, msg, len); buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8; for (int i = 0; i < 8; i++) buf[total - 1 - (size_t)i] = (unsigned char)(bits >> (8 * i));
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)buf[off + (size_t)i * 4] << 24 | (uint32_t)buf[off + (size_t)i * 4 + 1] << 16 | (uint32_t)buf[off + (size_t)i * 4 + 2] << 8 | buf[off + (size_t)i * 4 + 3];
        for (int i = 16; i < 64; i++) { uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K256[i] + w[i];
            uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    free(buf);
    for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
}
static void hash_pw(const char *salt, const char *pw, char out[65]) {
    char tmp[200]; int n = snprintf(tmp, sizeof tmp, "%s%s", salt, pw);
    sha256_hex((const unsigned char *)tmp, (size_t)n, out);
}

static long t_enter, t_shake, t_unlock;
static long t_enter, t_shake, t_unlock;
static long t_enter, t_shake, t_unlock;
static void cpy(char *d, size_t n, const char *src) { size_t l = strlen(src); if (l >= n) l = n - 1; memmove(d, src, l); d[l] = 0; }
/* ---------------------------------------------------------------- user database */
User users[16];
int nusers;
User *cur_user;
int locked = 1;
#define USERS_FILE "/etc/miaou/users"

static void new_salt(char *out, size_t n) {
    static const char hex[] = "0123456789abcdef"; unsigned char r[8]; int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, r, sizeof r) != (ssize_t)sizeof r) { for (int i = 0; i < 8; i++) r[i] = (unsigned char)(rand() ^ time(NULL)); }
    if (fd >= 0) close(fd);
    size_t i = 0; for (; i < 8 && i * 2 + 2 < n; i++) { out[i * 2] = hex[r[i] >> 4]; out[i * 2 + 1] = hex[r[i] & 15]; } out[i * 2] = 0;
}
void users_save(void) {
    fo_mkdir_p("/etc/miaou");
    FILE *f = fopen(USERS_FILE, "w");
    if (f) { for (int i = 0; i < nusers; i++) fprintf(f, "%s:%d:%d:%d:%s:%s:%s\n", users[i].name, users[i].uid, users[i].gid, users[i].admin, users[i].salt[0] ? users[i].salt : "-", users[i].hash[0] ? users[i].hash : "-", users[i].display); fclose(f); }
    FILE *pw = fopen("/etc/passwd", "w"), *gr = fopen("/etc/group", "w");
    if (pw) { fprintf(pw, "root:x:0:0:root:/root:/bin/sh\n"); for (int i = 0; i < nusers; i++) fprintf(pw, "%s:x:%d:%d:%s:%s:/bin/sh\n", users[i].name, users[i].uid, users[i].gid, users[i].display, users[i].home); fclose(pw); }
    if (gr) { fprintf(gr, "root:x:0:\n"); for (int i = 0; i < nusers; i++) fprintf(gr, "%s:x:%d:\n", users[i].name, users[i].gid); fclose(gr); }
}
static void user_defaults(void) {
    nusers = 0;
    user_add("miaou", "Miaou", "miaou", 1);
    user_add("invite", "Invité", "", 0);
}
void session_init(void) {
    nusers = 0;
    FILE *f = fopen(USERS_FILE, "r");
    char line[400];
    while (f && fgets(line, sizeof line, f) && nusers < 16) {
        line[strcspn(line, "\r\n")] = 0;
        User *u = &users[nusers]; memset(u, 0, sizeof *u);
        char *tok[7]; int n = 0; char *p = line;
        while (n < 7) { tok[n++] = p; char *c = strchr(p, ':'); if (!c || n == 7) break; *c = 0; p = c + 1; }
        if (n < 7) continue;
        cpy(u->name, sizeof u->name, tok[0]); u->uid = atoi(tok[1]); u->gid = atoi(tok[2]); u->admin = atoi(tok[3]);
        cpy(u->salt, sizeof u->salt, strcmp(tok[4], "-") ? tok[4] : ""); cpy(u->hash, sizeof u->hash, strcmp(tok[5], "-") ? tok[5] : "");
        cpy(u->display, sizeof u->display, tok[6]);
        { char h[100]; snprintf(h, sizeof h, "/home/%.40s", tok[0]); cpy(u->home, sizeof u->home, h); }
        nusers++;
    }
    if (f) fclose(f);
    if (!nusers) { user_defaults(); users_save(); }
    t_enter = now_ms();
}
static User *user_find(const char *name) { for (int i = 0; i < nusers; i++) if (!strcmp(users[i].name, name)) return &users[i]; return NULL; }
int user_add(const char *name, const char *display, const char *pw, int admin) {
    if (nusers >= 16 || !name[0] || strlen(name) > 24 || user_find(name)) return -1;
    for (const char *c = name; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return -2;
    User *u = &users[nusers]; memset(u, 0, sizeof *u);
    cpy(u->name, sizeof u->name, name); cpy(u->display, sizeof u->display, display && *display ? display : name);
    int uid = 1000; for (int i = 0; i < nusers; i++) if (users[i].uid >= uid) uid = users[i].uid + 1;
    u->uid = uid; u->gid = uid; u->admin = admin; { char h[100]; snprintf(h, sizeof h, "/home/%.40s", name); cpy(u->home, sizeof u->home, h); }
    if (pw && *pw) { new_salt(u->salt, sizeof u->salt); hash_pw(u->salt, pw, u->hash); }
    nusers++;
    return 0;
}
int user_setpw(const char *name, const char *pw) {
    User *u = user_find(name); if (!u) return -1;
    if (pw && *pw) { new_salt(u->salt, sizeof u->salt); hash_pw(u->salt, pw, u->hash); } else { u->salt[0] = 0; u->hash[0] = 0; }
    users_save(); return 0;
}
int user_del(const char *name) {
    User *u = user_find(name); if (!u || u == cur_user) return -1;
    int i = (int)(u - users); for (; i < nusers - 1; i++) users[i] = users[i + 1]; nusers--;
    if (cur_user) cur_user = user_find(cur_user->name);
    users_save(); return 0;
}
int session_is_admin(void) { return cur_user && cur_user->admin; }
static int user_check(User *u, const char *pw) {
    if (!u->hash[0]) return 1;
    char h[65]; hash_pw(u->salt, pw, h); return !strcmp(h, u->hash);
}

/* ---------------------------------------------------------------- session start / end */
static void session_enter(User *u) {
    cur_user = u; snprintf(cur_home, sizeof cur_home, "%s", u->home);
    static const char *DIRS[] = { "", "/Bureau", "/Documents", "/Images", "/Téléchargements", "/.config/miaou", "/.local/share/Trash/files", "/.local/share/Trash/info" };
    int fresh = access(cur_home, F_OK) != 0;
    for (size_t i = 0; i < sizeof DIRS / sizeof *DIRS; i++) { char d[300]; snprintf(d, sizeof d, "%s%s", cur_home, DIRS[i]); fo_mkdir_p(d); if (!u->admin) { if (chown(d, (uid_t)u->uid, (gid_t)u->gid) < 0) {} } }
    if (fresh) {                                           /* new home: copy the skeleton (sample desktop content) */
        char *argv[] = { "cp", "-a", "/usr/share/miaou/skel/.", cur_home, NULL }; fo_run(argv);
        if (!u->admin) { char own[32]; snprintf(own, sizeof own, "%d:%d", u->uid, u->gid); char *a2[] = { "chown", "-R", own, cur_home, NULL }; fo_run(a2); }
    }
    setenv("HOME", cur_home, 1); setenv("USER", u->name, 1); setenv("LOGNAME", u->name, 1);
    /* file access follows the user: administrators keep root rights, others get their own uid/gid for every file operation */
    if (u->admin) { setfsuid(0); setfsgid(0); } else { setfsgid((gid_t)u->gid); setfsuid((uid_t)u->uid); }
    char cfg[200]; snprintf(cfg, sizeof cfg, "%s/.config/miaou/desktop.conf", cur_home);
    cfg_reload(cfg); apply_config();
}
int session_autologin(const char *name) {
    User *u = user_find(name); if (!u) return -1;
    session_enter(u); locked = 0; return 0;
}
void session_logout(void) {
    char guest[200] = ""; if (cur_user && !cur_user->hash[0] && !cur_user->admin) snprintf(guest, sizeof guest, "%s", cur_user->home);
    if (guest[0]) { char cmd[260]; snprintf(cmd, sizeof cmd, "%s/.", guest); (void)cmd; char *argv[] = { "sh", "-c", "rm -rf \"$1\"/* \"$1\"/.[!.]* 2>/dev/null", "sh", guest, NULL }; fo_run(argv); }
    slog_marker("MIAOUDESK_LOGOUT");
    cur_user = NULL; snprintf(cur_home, sizeof cur_home, "/root");
    setfsuid(0); setfsgid(0);
    locked = 1; cfg_reload("/etc/miaou/desktop.conf"); apply_config();
}

/* ---------------------------------------------------------------- login screen */
static int sel_user, show_pw;
static char pw[64], err[80];
static struct { int x, y, w, h, id; } lz[32];
static int nlz;
static void lzone(int x, int y, int w, int h, int id) { if (nlz < 32) { lz[nlz].x = x; lz[nlz].y = y; lz[nlz].w = w; lz[nlz].h = h; lz[nlz].id = id; nlz++; } }

void session_lock(void) {
    if (locked) return;
    locked = 1; pw[0] = 0; err[0] = 0; show_pw = 0; t_enter = now_ms();
    sel_user = 0; if (cur_user) for (int i = 0; i < nusers; i++) if (&users[i] == cur_user) sel_user = i;
    slog_marker("MIAOUDESK_LOCKED");
}
static void attempt(void) {
    if (sel_user < 0 || sel_user >= nusers) return;
    User *u = &users[sel_user];
    if (cur_user && cur_user != u) { desk_close_all(); cur_user = NULL; }       /* switching user ends the previous session */
    if (!user_check(u, pw)) { snprintf(err, sizeof err, "Mot de passe incorrect"); pw[0] = 0; t_shake = now_ms(); slog_marker("MIAOUDESK_LOGIN_FAIL"); return; }
    pw[0] = 0; err[0] = 0;
    if (cur_user != u) session_enter(u);
    locked = 0; t_unlock = now_ms();
    slog_marker2("MIAOUDESK_LOGIN_OK", u->name);
    toast("Bienvenue, %s", u->display);
}
int login_fading(void) { return !locked && t_unlock && now_ms() - t_unlock < 320; }
int login_tick(void) { long t = now_ms(); return (locked && (t - t_enter < 400 || t - t_shake < 450)) || login_fading(); }

static const char *LDAYS[] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
static const char *LMONTHS[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre" };
static void avatar(int cx, int cy, int r, const User *u, int ring) {
    gfx_circle(cx, cy + S(3), r + S(2), RGB(0, 0, 0), 70);                              /* soft drop shadow */
    if (ring) gfx_circle(cx, cy, r + S(2), RGB(235, 235, 238), 255);
    gfx_circle(cx, cy, r, RGB(52, 52, 58), 255);
    char ini[8] = { 0 }; const char *d = u->display[0] ? u->display : u->name; int n = utf8_put(ini, utf8_next(&d) & 0x1fffff); ini[n] = 0;
    for (char *c = ini; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    int px = (int)(r * 1.0f); int tw = text_w(F_BOLD, ini, px); text_draw(F_BOLD, cx - tw / 2, cy - (int)(px * 0.62f), ini, RGB(240, 240, 243), px);
}
void login_draw(void) {
    nlz = 0; long t = now_ms();
    Img *bg = wallpaper_blur(theme_dark);
    if (bg) gfx_blit(bg->px, bg->w, bg->h, 0, 0); else gfx_fill(0, 0, SW, SH, TH.bg, 255);
    for (int y = 0; y < SH; y += 2) gfx_fill(0, y, SW, 2, RGB(6, 6, 8), 70 + 90 * y / SH);       /* neutral vignette, darker towards the bottom */
    u32 white = RGB(246, 246, 248), soft = RGB(196, 196, 204), dim = RGB(150, 150, 158);
    /* brand, top left */
    gfx_logo(S(28), S(24), S(26), white, RGB(40, 40, 46)); text_draw(F_BOLD, S(64), S(25), "MiaouOS", white, S(16)); text_draw(F_UI, S(64), S(46), "Version 0.3", dim, S(11));
    /* clock and date */
    time_t now = time(NULL); struct tm lt; char hm[16] = "--:--", date[64] = "";
    if (localtime_r(&now, &lt)) { strftime(hm, sizeof hm, "%H:%M", &lt); snprintf(date, sizeof date, "%s %d %s", LDAYS[lt.tm_wday], lt.tm_mday, LMONTHS[lt.tm_mon]); }
    int cx = SW / 2, big = S(112);
    int tw = text_w(F_UI, hm, big); text_draw(F_UI, cx - tw / 2, SH / 11, hm, white, big);
    tw = text_w(F_UI, date, S(22)); text_draw(F_UI, cx - tw / 2, SH / 11 + (int)(big * 1.22f), date, soft, S(22));
    if (nusers <= 0) return;
    if (sel_user >= nusers) sel_user = 0;
    User *u = &users[sel_user];
    int shake = 0; if (t - t_shake < 450) shake = (int)(sinf((float)(t - t_shake) * 0.06f) * (1.0f - (float)(t - t_shake) / 450.0f) * (float)S(16));
    int cy = SH * 53 / 100;
    avatar(cx + shake, cy, S(48), u, 0);
    const char *nm = u->display[0] ? u->display : u->name; tw = text_w(F_BOLD, nm, S(24)); text_draw(F_BOLD, cx + shake - tw / 2, cy + S(66), nm, white, S(24));
    int fw = S(320), fh = S(48), fx = cx - fw / 2 + shake, fy = cy + S(112);
    int has_pw = u->hash[0] != 0;
    gfx_rrect(fx, fy, fw, fh, fh / 2, RGB(255, 255, 255), 34);
    gfx_rrect_line(fx, fy, fw, fh, fh / 2, err[0] ? RGB(255, 120, 120) : RGB(255, 255, 255), err[0] ? 255 : 90);
    int bx = fx + fw - fh + S(6), bs = fh - S(12);                                              /* round submit button inside the field */
    gfx_circle(bx + bs / 2, fy + fh / 2, bs / 2, RGB(240, 240, 243), 255);
    Img *ar = img_get("forward", S(18)); if (ar) img_draw_tint(ar, bx + (bs - S(18)) / 2, fy + (fh - S(18)) / 2, 255, RGB(24, 24, 26));
    lzone(bx, fy, bs, fh, 1);
    if (has_pw) {
        size_t n = strlen(pw); int ty = fy + (fh - font_line_h(F_UI, S(16))) / 2;
        if (!n) text_draw(F_UI, fx + S(22), ty, "Mot de passe", RGB(190, 190, 198), S(16));
        else if (show_pw) text_draw_clip(F_UI, fx + S(22), ty, pw, white, S(16), fw - S(110));
        else for (size_t i = 0; i < n && i < 16; i++) gfx_circle(fx + S(28) + (int)i * S(15), fy + fh / 2, S(4), white, 255);
        if ((t / 500) % 2 == 0) { int off = show_pw ? text_w(F_UI, pw, S(16)) : (int)(n > 16 ? 16 : n) * S(15) - S(3); gfx_fill(fx + S(22) + (show_pw ? off : off + S(3)), fy + S(13), 2, fh - S(26), white, 255); }
        Img *ei = img_get("eye", S(18)); if (ei) img_draw_tint(ei, bx - S(34), fy + (fh - S(18)) / 2, 255, show_pw ? white : dim); lzone(bx - S(42), fy, S(40), fh, 2);
    } else { const char *m = "Entrée pour se connecter"; text_draw(F_UI, fx + S(22), fy + (fh - font_line_h(F_UI, S(16))) / 2, m, RGB(210, 210, 216), S(16)); }
    if (err[0]) { tw = text_w(F_UI, err, S(14)); text_draw(F_UI, cx + shake - tw / 2, fy + fh + S(16), err, RGB(255, 150, 150), S(14)); }
    else if (kbd_caps()) { const char *m = "Verrouillage majuscules activé"; tw = text_w(F_UI, m, S(13)); text_draw(F_UI, cx - tw / 2, fy + fh + S(16), m, RGB(240, 196, 120), S(13)); }
    /* users, bottom left (as on a workstation login) */
    if (nusers > 1) {
        int rows = nusers, y0 = SH - S(28) - rows * S(52);
        for (int i = 0; i < rows; i++) {
            int ry = y0 + i * S(52), sel = i == sel_user, hot = hover(S(20), ry, S(220), S(46));
            if (sel || hot) gfx_rrect(S(20), ry, S(220), S(46), S(10), RGB(255, 255, 255), sel ? 40 : 22);
            avatar(S(20) + S(24), ry + S(23), S(16), &users[i], 0);
            const char *d = users[i].display[0] ? users[i].display : users[i].name;
            text_draw(sel ? F_BOLD : F_UI, S(20) + S(52), ry + (S(46) - font_line_h(F_UI, S(14))) / 2, d, sel || hot ? white : soft, S(14));
            lzone(S(20), ry, S(220), S(46), 10 + i);
        }
    }
    /* status and power, bottom right */
    int up = 0; const char *ip = net_summary(&up);
    const char *lay = kbd_layout() ? "FR" : "US"; int lw = text_w(F_BOLD, lay, S(13)) + S(24), ih = S(40), by = SH - S(28) - ih, x = SW - S(24);
    for (int i = 1; i >= 0; i--) {
        static const char *ic[2] = { "reboot", "power" }; x -= ih; int hot = hover(x, by, ih, ih);
        gfx_rrect(x, by, ih, ih, ih / 2, RGB(255, 255, 255), hot ? 56 : 28);
        Img *im = img_get(ic[i], S(18)); if (im) img_draw_tint(im, x + (ih - S(18)) / 2, by + (ih - S(18)) / 2, 255, white);
        lzone(x, by, ih, ih, 31 + i); x -= S(10);
    }
    x -= S(10); x -= lw; { int hot = hover(x, by, lw, ih); gfx_rrect(x, by, lw, ih, ih / 2, RGB(255, 255, 255), hot ? 56 : 28); text_draw(F_BOLD, x + S(12), by + (ih - font_line_h(F_BOLD, S(13))) / 2, lay, white, S(13)); lzone(x, by, lw, ih, 30); }
    { int nw = S(40) + text_w(F_UI, ip, S(13)); x -= S(10) + nw; gfx_rrect(x, by, nw, ih, ih / 2, RGB(255, 255, 255), 28);
      Img *ni = img_get(up ? "signal" : "plug", S(18)); if (ni) img_draw_tint(ni, x + S(12), by + (ih - S(18)) / 2, 255, white); text_draw(F_UI, x + S(36), by + (ih - font_line_h(F_UI, S(13))) / 2, ip, soft, S(13)); }
    /* fade in */
    if (t - t_enter < 450) { float k = (float)(t - t_enter) / 450.0f; gfx_fill(0, 0, SW, SH, RGB(8, 8, 10), (int)((1.0f - k) * (1.0f - k) * 255)); }
}
void login_key(int key, int mods) {
    (void)mods;
    if (nusers <= 0) return;
    if (key == K_ENTER) { attempt(); return; }
    if (key == K_BS) { size_t n = strlen(pw); if (n) { while (n > 0 && (pw[n - 1] & 0xc0) == 0x80) n--; if (n) n--; pw[n] = 0; } err[0] = 0; return; }
    if (key == K_ESC) { pw[0] = 0; err[0] = 0; return; }
    if (key == K_TAB || key == K_DOWN || key == K_RIGHT) { sel_user = (sel_user + 1) % nusers; pw[0] = 0; err[0] = 0; return; }
    if (key == K_UP || key == K_LEFT) { sel_user = (sel_user + nusers - 1) % nusers; pw[0] = 0; err[0] = 0; return; }
    if (key >= 32 && key < 0x200000) { size_t n = strlen(pw); if (n < 56) { n += (size_t)utf8_put(pw + n, key); pw[n] = 0; err[0] = 0; } }
}
void login_click(int x, int y) {
    for (int i = nlz - 1; i >= 0; i--) if (x >= lz[i].x && x < lz[i].x + lz[i].w && y >= lz[i].y && y < lz[i].y + lz[i].h) {
        int id = lz[i].id;
        if (id == 1) attempt(); else if (id == 2) show_pw = !show_pw;
        else if (id >= 10 && id < 26) { sel_user = id - 10; pw[0] = 0; err[0] = 0; }
        else if (id == 30) kbd_set_layout(!kbd_layout());
        else if (id == 31) power_action(1); else if (id == 32) power_action(0);
        return;
    }
}
int login_fade_alpha(void) { long d = now_ms() - t_unlock; return d >= 320 ? 255 : (int)(d * 255 / 320); }
