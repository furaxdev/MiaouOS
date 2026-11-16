/* System apps: Settings, Task Monitor, Network, About. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

static long mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000L + t.tv_nsec / 1000000L; }
static void card(int x, int y, int w, int h) { gfx_rrect(x, y, w, h, S(8), TH.panel, 255); gfx_rrect_line(x, y, w, h, S(8), TH.line, 255); }
static void label(int x, int y, const char *s, u32 c, int px, int bold) { text_draw(bold ? F_BOLD : F_UI, x, y, s, c, px); }

/* ================================================================== Task monitor */
typedef struct { int pid; char name[20]; char state; unsigned long rss_kb, ticks, last_ticks; float cpu; } Proc;
typedef struct {
    int hist[90], nh; unsigned long pt, pi; int cpu_pct;
    unsigned long mt, ma, load_ms; char load[32];
    Proc p[256]; int np, sel_pid, scroll, sortk; unsigned long total_delta;
    long last;
    int view, logscroll; Job *job;
} Mon;
enum { MN_KILL = 1, MN_FORCE, MN_SORT_CPU, MN_SORT_MEM, MN_SORT_PID, MN_TAB0 = 10, MN_LOGREFRESH = 14, MN_ROW = 1000 };
static void *mon_create(Win *w) { (void)w; Mon *m = calloc(1, sizeof *m); m->sortk = 0; return m; }
static void mon_destroy(Win *w) { Mon *m = w->st; job_free(m->job); free(m); }
static int cmp_cpu(const void *a, const void *b) { const Proc *x = a, *y = b; return (y->cpu > x->cpu) - (y->cpu < x->cpu); }
static int cmp_mem(const void *a, const void *b) { const Proc *x = a, *y = b; return (y->rss_kb > x->rss_kb) - (y->rss_kb < x->rss_kb); }
static int cmp_pid(const void *a, const void *b) { const Proc *x = a, *y = b; return x->pid - y->pid; }
static void mon_refresh(Mon *m) {
    FILE *f = fopen("/proc/stat", "r"); unsigned long total = 0, idle = 0;
    if (f) { unsigned long v[8] = { 0 }; if (fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 4) { for (int i = 0; i < 8; i++) total += v[i]; idle = v[3] + v[4]; } fclose(f); }
    unsigned long dt = total - m->pt, di = idle - m->pi;
    m->cpu_pct = dt ? (int)(100 * (dt - di) / dt) : 0; m->total_delta = dt ? dt : 1;
    m->pt = total; m->pi = idle;
    if (m->nh < 90) m->hist[m->nh++] = m->cpu_pct; else { memmove(m->hist, m->hist + 1, 89 * sizeof(int)); m->hist[89] = m->cpu_pct; }
    f = fopen("/proc/meminfo", "r"); if (f) { char l[128]; while (fgets(l, sizeof l, f)) { sscanf(l, "MemTotal: %lu", &m->mt); sscanf(l, "MemAvailable: %lu", &m->ma); } fclose(f); }
    f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%31s", m->load) != 1) strcpy(m->load, "?"); fclose(f); }
    Proc old[256]; int nold = m->np; memcpy(old, m->p, sizeof old);
    m->np = 0;
    DIR *d = opendir("/proc"); struct dirent *de;
    while (d && (de = readdir(d)) && m->np < 256) {
        if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
        char path[64], buf[512]; snprintf(path, sizeof path, "/proc/%s/stat", de->d_name);
        FILE *sf = fopen(path, "r"); if (!sf) continue;
        size_t n = fread(buf, 1, sizeof buf - 1, sf); fclose(sf); buf[n] = 0;
        char *lp = strchr(buf, '('), *rp = strrchr(buf, ')'); if (!lp || !rp) continue;
        Proc *p = &m->p[m->np]; memset(p, 0, sizeof *p);
        p->pid = atoi(de->d_name); *rp = 0; snprintf(p->name, sizeof p->name, "%s", lp + 1);
        unsigned long ut = 0, st = 0; long rss = 0; char state = '?';
        sscanf(rp + 2, "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %*u %ld", &state, &ut, &st, &rss);
        p->state = state; p->ticks = ut + st; p->rss_kb = (unsigned long)rss * 4;
        for (int i = 0; i < nold; i++) if (old[i].pid == p->pid) { p->cpu = 100.0f * (float)(p->ticks - old[i].ticks) / (float)m->total_delta; break; }
        m->np++;
    }
    if (d) closedir(d);
    qsort(m->p, (size_t)m->np, sizeof(Proc), m->sortk == 0 ? cmp_cpu : m->sortk == 1 ? cmp_mem : cmp_pid);
}
static void mon_start_log(Mon *m) { job_free(m->job); m->job = job_start("dmesg 2>&1 | tail -n 250"); m->logscroll = 0; }
static void mon_action(Win *w, int id, int mx, int my, int btn) {
    Mon *m = w->st; (void)mx; (void)my; (void)btn;
    if (id >= MN_TAB0 && id < MN_TAB0 + 3) { m->view = id - MN_TAB0; if (m->view == 2 && !m->job) mon_start_log(m); return; }
    if (id == MN_LOGREFRESH) { mon_start_log(m); return; }
    if (id >= MN_ROW) { m->sel_pid = m->p[id - MN_ROW].pid; return; }
    if (id == MN_SORT_CPU) m->sortk = 0; else if (id == MN_SORT_MEM) m->sortk = 1; else if (id == MN_SORT_PID) m->sortk = 2;
    else if ((id == MN_KILL || id == MN_FORCE) && m->sel_pid) {
        if (m->sel_pid == 1 || m->sel_pid == getpid()) { toast("Ce processus est protégé"); return; }
        if (kill(m->sel_pid, id == MN_KILL ? SIGTERM : SIGKILL) == 0) toast("Signal envoyé au processus %d", m->sel_pid); else toast("Échec : %s", strerror(errno));
        m->last = 0;
    }
}
static void mon_mouse(Win *w, int type, int x, int y, int arg) { Mon *m = w->st; (void)x; (void)y; if (type == M_WHEEL) { if (m->view == 2) { m->logscroll -= arg * 3; if (m->logscroll < 0) m->logscroll = 0; } else { m->scroll -= arg * 2; if (m->scroll < 0) m->scroll = 0; } } }
static int mon_tick(Win *w) {
    Mon *m = w->st; long t = mono_ms();
    if (m->job && job_poll(m->job)) { return 1; }
    if (t - m->last < 1000) return 0;
    m->last = t; mon_refresh(m); return 1;
}
static void mon_extra(Mon *m, Win *w, int x, int y, int cw, int ch, int ty, int pad) {
    int ly = ty + S(46), lh = y + ch - ly - pad;
    card(x + pad, ly - S(6), cw - pad * 2, lh + S(12));
    if (m->view == 1) {                                           /* disks: mounted file systems with usage */
        FILE *f = fopen("/proc/mounts", "r"); char l[300]; int row = 0;
        label(x + pad + S(18), ly, "Point de montage", TH.dim, S(12), 1); label(x + cw - pad - S(300), ly, "Utilisation", TH.dim, S(12), 1);
        while (f && fgets(l, sizeof l, f) && row < 12) {
            char dev[100], mnt[160], fs[40]; if (sscanf(l, "%99s %159s %39s", dev, mnt, fs) != 3) continue;
            if (!strcmp(fs, "proc") || !strcmp(fs, "sysfs") || !strcmp(fs, "devpts") || !strncmp(fs, "cgroup", 6) || !strcmp(fs, "mqueue") || !strcmp(fs, "securityfs") || !strcmp(fs, "debugfs") || !strcmp(fs, "tracefs") || !strcmp(fs, "configfs") || !strcmp(fs, "fusectl") || !strcmp(fs, "bpf")) continue;
            struct statvfs sv; if (statvfs(mnt, &sv) != 0 || !sv.f_blocks) continue;
            unsigned long long tot = (unsigned long long)sv.f_blocks * sv.f_frsize, fr = (unsigned long long)sv.f_bavail * sv.f_frsize, used = tot - fr;
            int ry = ly + S(26) + row * S(48); char a[32], b[32], t[120]; human_size(fr, a, sizeof a); human_size(tot, b, sizeof b);
            label(x + pad + S(18), ry, mnt, TH.text, S(14), 1); snprintf(t, sizeof t, "%s  ·  %s", fs, dev); label(x + pad + S(18), ry + S(20), t, TH.dim, S(11), 0);
            ui_progress(x + cw - pad - S(300), ry + S(6), S(160), S(10), (int)(100 * used / tot), TH.accent);
            snprintf(t, sizeof t, "%s libres sur %s", a, b); label(x + cw - pad - S(300), ry + S(22), t, TH.dim, S(11), 0);
            row++;
        }
        if (f) fclose(f);
        return;
    }
    ui_button(w, x + cw - pad - S(130), ty, S(120), S(32), "Actualiser", "refresh", MN_LOGREFRESH, 0);
    gfx_clip(x + pad + S(8), ly - S(2), cw - pad * 2 - S(16), lh);
    if (m->job) {
        int lines = 0; for (const char *c = m->job->buf; *c; c++) if (*c == '\n') lines++;
        int vis = (lh - S(8)) / S(16); if (m->logscroll > lines - vis) m->logscroll = lines - vis; if (m->logscroll < 0) m->logscroll = 0;
        int start = lines > vis ? lines - vis - m->logscroll : 0, ln = 0, yy = ly;
        const char *p = m->job->buf;
        while (*p && yy < ly + lh) { const char *e = strchr(p, '\n'); size_t k = e ? (size_t)(e - p) : strlen(p); if (ln >= start) { char b[200]; if (k > 190) k = 190; memcpy(b, p, k); b[k] = 0; text_draw(F_MONO, x + pad + S(14), yy, b, TH.text, S(12)); yy += S(16); } ln++; if (!e) break; p = e + 1; }
    }
    gfx_unclip();
}
static void mon_draw(Win *w, int x, int y, int cw, int ch) {
    Mon *m = w->st; int px = S(14), pad = S(16);
    if (!m->last) { m->last = mono_ms(); mon_refresh(m); }
    int half = (cw - pad * 3) / 2, ch1 = S(150);
    card(x + pad, y + pad, half, ch1);
    label(x + pad + S(16), y + pad + S(12), "Processeur", TH.dim, S(12), 1);
    char b[32]; snprintf(b, sizeof b, "%d %%", m->cpu_pct); label(x + pad + S(16), y + pad + S(30), b, TH.text, S(26), 1);
    int gx = x + pad + S(16), gy = y + pad + S(72), gw = half - S(32), gh = ch1 - S(88);
    gfx_rrect(gx, gy, gw, gh, S(6), TH.field, 255);
    for (int i = 0; i < m->nh; i++) {
        int bw = gw / 90 > 0 ? gw / 90 : 1, hh = gh * m->hist[i] / 100; if (hh < 1 && m->hist[i] > 0) hh = 1;
        gfx_fill(gx + i * gw / 90, gy + gh - hh, bw, hh, TH.accent, 255);
    }
    card(x + pad * 2 + half, y + pad, half, ch1);
    label(x + pad * 2 + half + S(16), y + pad + S(12), "Mémoire", TH.dim, S(12), 1);
    unsigned long used = m->mt > m->ma ? m->mt - m->ma : 0; int pct = m->mt ? (int)(100 * used / m->mt) : 0;
    snprintf(b, sizeof b, "%d %%", pct); label(x + pad * 2 + half + S(16), y + pad + S(30), b, TH.text, S(26), 1);
    ui_progress(x + pad * 2 + half + S(16), y + pad + S(76), half - S(32), S(12), pct, TH.accent);
    char mb[96]; snprintf(mb, sizeof mb, "%lu Mo utilisés sur %lu Mo", used / 1024, m->mt / 1024); label(x + pad * 2 + half + S(16), y + pad + S(96), mb, TH.dim, px - 1, 0);
    snprintf(mb, sizeof mb, "Charge moyenne : %s  ·  %d processus", m->load, m->np); label(x + pad * 2 + half + S(16), y + pad + S(118), mb, TH.dim, px - 1, 0);

    int ty = y + pad * 2 + ch1;
    { static const char *VT[3] = { "Processus", "Disques", "Journal" }; for (int i = 0; i < 3; i++) ui_tab(w, x + pad + i * S(118), ty, S(110), S(34), VT[i], i == 0 ? "cpu" : i == 1 ? "drive" : "journal", MN_TAB0 + i, m->view == i); }
    if (m->view != 0) { mon_extra(m, w, x, y, cw, ch, ty, pad); return; }
    ui_button(w, x + cw - pad - S(120) * 2 - S(8) - S(60) * 3 - S(24), ty, S(60), S(32), "CPU", NULL, MN_SORT_CPU, m->sortk == 0);
    ui_button(w, x + cw - pad - S(120) * 2 - S(8) - S(60) * 2 - S(16), ty, S(60), S(32), "Mém.", NULL, MN_SORT_MEM, m->sortk == 1);
    ui_button(w, x + cw - pad - S(120) * 2 - S(8) - S(60) - S(8), ty, S(60), S(32), "PID", NULL, MN_SORT_PID, m->sortk == 2);
    ui_button(w, x + cw - pad - S(120) * 2 - S(8), ty, S(120), S(32), "Terminer", NULL, MN_KILL, 0);
    ui_button(w, x + cw - pad - S(120), ty, S(120), S(32), "Forcer", NULL, MN_FORCE, 0);
    int ly = ty + S(46), rowh = S(28), lh = y + ch - ly - pad, vis = lh / rowh;
    card(x + pad, ly - S(6), cw - pad * 2, lh + S(12));
    label(x + pad + S(18), ly, "PID", TH.dim, S(12), 1); label(x + pad + S(84), ly, "Nom", TH.dim, S(12), 1);
    label(x + cw - pad - S(300), ly, "État", TH.dim, S(12), 1); label(x + cw - pad - S(220), ly, "CPU", TH.dim, S(12), 1); label(x + cw - pad - S(130), ly, "Mémoire", TH.dim, S(12), 1);
    vis--; if (m->scroll > m->np - vis) m->scroll = m->np - vis; if (m->scroll < 0) m->scroll = 0;
    gfx_clip(x + pad, ly + S(20), cw - pad * 2, lh - S(20));
    for (int i = 0; i < vis && m->scroll + i < m->np; i++) {
        Proc *p = &m->p[m->scroll + i]; int ry = ly + S(22) + i * rowh;
        if (p->pid == m->sel_pid) gfx_rrect(x + pad + S(8), ry - S(2), cw - pad * 2 - S(16), rowh - S(2), S(7), TH.sel, 255);
        else if (hover(x + pad, ry, cw - pad * 2, rowh)) gfx_rrect(x + pad + S(8), ry - S(2), cw - pad * 2 - S(16), rowh - S(2), S(7), TH.btn, 160);
        char t[32];
        snprintf(t, sizeof t, "%d", p->pid); label(x + pad + S(18), ry + S(3), t, TH.dim, px - 1, 0);
        label(x + pad + S(84), ry + S(3), p->name, TH.text, px - 1, 0);
        snprintf(t, sizeof t, "%c", p->state); label(x + cw - pad - S(300), ry + S(3), t, TH.dim, px - 1, 0);
        snprintf(t, sizeof t, "%.1f %%", p->cpu); label(x + cw - pad - S(220), ry + S(3), t, TH.text, px - 1, 0);
        human_size(p->rss_kb * 1024ULL, t, sizeof t); label(x + cw - pad - S(130), ry + S(3), t, TH.text, px - 1, 0);
        zone(w, x + pad, ry - S(2), cw - pad * 2, rowh, MN_ROW + m->scroll + i);
    }
    gfx_unclip();
    ui_scrollbar(x + cw - pad - S(12), ly + S(22), lh - S(30), m->np, vis, m->scroll);
}

/* ================================================================== Network */
typedef struct { char name[16], state[16], mac[20], ip[20]; unsigned long long rx, tx; } If;
typedef struct { If i[6]; int n; char gw[20], dns[40]; Job *job; char log[2048]; long last; int busy; } Net;
enum { N_DHCP = 1, N_PING, N_DNS, N_REFRESH };
static void net_scan(Net *n) {
    n->n = 0; n->gw[0] = 0; n->dns[0] = 0;
    DIR *d = opendir("/sys/class/net"); struct dirent *de;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    while (d && (de = readdir(d)) && n->n < 6) {
        if (de->d_name[0] == '.' || !strcmp(de->d_name, "sit0") || !strncmp(de->d_name, "tunl", 4) || !strncmp(de->d_name, "ip6tnl", 6) || !strncmp(de->d_name, "gre", 3) || !strncmp(de->d_name, "ip6gre", 6) || !strncmp(de->d_name, "erspan", 6)) continue;
        If *f = &n->i[n->n]; memset(f, 0, sizeof *f);
        snprintf(f->name, sizeof f->name, "%s", de->d_name);
        char p[96], b[64];
        snprintf(p, sizeof p, "/sys/class/net/%s/operstate", de->d_name); char *t = read_file(p, NULL, 63); snprintf(f->state, sizeof f->state, "%s", t ? t : "?"); f->state[strcspn(f->state, "\n")] = 0; free(t);
        snprintf(p, sizeof p, "/sys/class/net/%s/address", de->d_name); t = read_file(p, NULL, 63); snprintf(f->mac, sizeof f->mac, "%s", t ? t : ""); f->mac[strcspn(f->mac, "\n")] = 0; free(t);
        struct ifreq ifr; memset(&ifr, 0, sizeof ifr); snprintf(ifr.ifr_name, IFNAMSIZ, "%s", de->d_name);
        if (s >= 0 && ioctl(s, SIOCGIFADDR, &ifr) == 0) { struct sockaddr_in *sa = (struct sockaddr_in *)&ifr.ifr_addr; inet_ntop(AF_INET, &sa->sin_addr, f->ip, sizeof f->ip); }
        (void)b;
        n->n++;
    }
    if (d) closedir(d);
    if (s >= 0) close(s);
    FILE *f = fopen("/proc/net/dev", "r");
    if (f) { char l[256]; while (fgets(l, sizeof l, f)) { char *c = strchr(l, ':'); if (!c) continue; *c = 0; char *nm = l; while (*nm == ' ') nm++; unsigned long long rx = 0, tx = 0; sscanf(c + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu", &rx, &tx); for (int i = 0; i < n->n; i++) if (!strcmp(n->i[i].name, nm)) { n->i[i].rx = rx; n->i[i].tx = tx; } } fclose(f); }
    f = fopen("/proc/net/route", "r");
    if (f) { char l[256]; if (fgets(l, sizeof l, f)) {} while (fgets(l, sizeof l, f)) { char nm[16]; unsigned dst, gw; if (sscanf(l, "%15s %x %x", nm, &dst, &gw) == 3 && dst == 0 && gw) { struct in_addr a = { gw }; inet_ntop(AF_INET, &a, n->gw, sizeof n->gw); break; } } fclose(f); }
    f = fopen("/etc/resolv.conf", "r");
    if (f) { char l[128]; while (fgets(l, sizeof l, f)) if (!strncmp(l, "nameserver ", 11)) { snprintf(n->dns, sizeof n->dns, "%s", l + 11); n->dns[strcspn(n->dns, "\r\n")] = 0; break; } fclose(f); }
}
static const char *first_iface(Net *n) { for (int i = 0; i < n->n; i++) if (strcmp(n->i[i].name, "lo")) return n->i[i].name; return NULL; }
static void *net_create(Win *w) { (void)w; Net *n = calloc(1, sizeof *n); net_scan(n); n->last = mono_ms(); snprintf(n->log, sizeof n->log, "Prêt. Utilisez les boutons ci-dessus."); return n; }
static void net_destroy(Win *w) { Net *n = w->st; job_free(n->job); free(n); }
static void net_run(Net *n, const char *cmd, const char *title) {
    if (n->job && !n->job->done) { toast("Une commande est déjà en cours"); return; }
    job_free(n->job); n->job = job_start(cmd);
    snprintf(n->log, sizeof n->log, "$ %s\n", title); n->busy = n->job != NULL;
}
static void net_action(Win *w, int id, int mx, int my, int btn) {
    Net *n = w->st; (void)mx; (void)my; (void)btn; char cmd[256];
    const char *ifn = first_iface(n);
    if (id == N_REFRESH) { net_scan(n); return; }
    if (id == N_DHCP) {
        if (!ifn) { toast("Aucune interface réseau"); return; }
        snprintf(cmd, sizeof cmd, "ip link set lo up 2>&1; ip link set %s up 2>&1; udhcpc -i %s -n -q -t 4 -T 2 -s /usr/share/udhcpc/default.script 2>&1", ifn, ifn);
        net_run(n, cmd, "udhcpc (DHCP)");
    } else if (id == N_PING) { snprintf(cmd, sizeof cmd, "ping -c 3 -W 2 %s 2>&1", n->gw[0] ? n->gw : "1.1.1.1"); net_run(n, cmd, cmd); }
    else if (id == N_DNS) net_run(n, "nslookup example.com 2>&1", "nslookup example.com");
}
static int net_tick(Win *w) {
    Net *n = w->st; int ch = 0;
    if (n->job) { char *old = NULL; (void)old; size_t before = n->job->len; if (job_poll(n->job)) { size_t l = strlen(n->log); size_t room = sizeof n->log - 1 - l; size_t add = n->job->len - before; if (add > room) add = room; memcpy(n->log + l, n->job->buf + before, add); n->log[l + add] = 0; ch = 1; if (n->job->done) { n->busy = 0; net_scan(n); snprintf(n->log + strlen(n->log), room > 40 ? 40 : 1, "\n[terminé : code %d]", n->job->status); } } }
    if (mono_ms() - n->last > 2000) { n->last = mono_ms(); net_scan(n); ch = 1; }
    return ch;
}
static void net_draw(Win *w, int x, int y, int cw, int ch) {
    Net *n = w->st; int pad = S(16), px = S(14);
    label(x + pad, y + pad, "Réseau", TH.text, S(22), 1);
    int cy = y + pad + S(44);
    for (int i = 0; i < n->n; i++) {
        If *f = &n->i[i]; int cardh = S(96);
        card(x + pad, cy, cw - pad * 2, cardh);
        int up = f->ip[0] != 0;
        Img *ic = img_get(!strcmp(f->name, "lo") ? "home" : up ? "signal" : "plug", S(40)); if (ic) img_draw(ic, x + pad + S(18), cy + S(18), 255);
        label(x + pad + S(74), cy + S(14), f->name, TH.text, S(17), 1);
        gfx_rrect(x + pad + S(74) + text_w(F_BOLD, f->name, S(17)) + S(12), cy + S(16), S(78), S(22), S(11), up ? TH.good : TH.dim, 60);
        label(x + pad + S(74) + text_w(F_BOLD, f->name, S(17)) + S(24), cy + S(18), up ? "connecté" : "sans IP", up ? TH.good : TH.dim, S(12), 1);
        char l[160]; snprintf(l, sizeof l, "Adresse IPv4 : %s", f->ip[0] ? f->ip : "—"); label(x + pad + S(74), cy + S(44), l, TH.text, px, 0);
        char r[32], t[32]; human_size(f->rx, r, sizeof r); human_size(f->tx, t, sizeof t);
        snprintf(l, sizeof l, "MAC %s  ·  reçu %s  ·  envoyé %s", f->mac, r, t); label(x + pad + S(74), cy + S(68), l, TH.dim, px - 1, 0);
        cy += cardh + S(10);
    }
    char g[120]; snprintf(g, sizeof g, "Passerelle : %s    DNS : %s", n->gw[0] ? n->gw : "—", n->dns[0] ? n->dns : "—");
    label(x + pad, cy + S(2), g, TH.dim, px, 0);
    cy += S(32);
    int bw = S(180);
    ui_button(w, x + pad, cy, bw + S(40), S(38), "Connexion DHCP", "plug", N_DHCP, 1);
    ui_button(w, x + pad + bw + S(52), cy, bw, S(38), "Tester la connexion", "signal", N_PING, 0);
    ui_button(w, x + pad + bw * 2 + S(64), cy, S(140), S(38), "Test DNS", "search", N_DNS, 0);
    ui_button(w, x + pad + bw * 2 + S(216), cy, S(120), S(38), "Actualiser", "reload", N_REFRESH, 0);
    cy += S(52);
    int lh = y + ch - cy - pad;
    gfx_rrect(x + pad, cy, cw - pad * 2, lh, S(10), RGB(17, 13, 32), 255);
    gfx_clip(x + pad + S(8), cy + S(6), cw - pad * 2 - S(16), lh - S(12));
    int ly = cy + S(10), lhh = S(17); const char *p = n->log; int total = 1; for (const char *q = p; *q; q++) if (*q == '\n') total++;
    int vis = (lh - S(16)) / lhh, skip = total > vis ? total - vis : 0, ln = 0;
    while (*p) { const char *e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p); if (ln >= skip) { char buf[200]; if (l > 190) l = 190; memcpy(buf, p, l); buf[l] = 0; text_draw(F_MONO, x + pad + S(14), ly, buf, RGB(210, 205, 235), S(12)); ly += lhh; } ln++; if (!e) break; p = e + 1; }
    gfx_unclip();
}

/* ================================================================== About */
static void about_draw(Win *w, int x, int y, int cw, int ch) {
    (void)w; (void)ch;
    gfx_logo(x + (cw - S(88)) / 2, y + S(32), S(88), TH.text, TH.win);
    const char *t = "MiaouOS"; int tw = text_w(F_BOLD, t, S(32)); text_draw(F_BOLD, x + (cw - tw) / 2, y + S(144), t, TH.text, S(32));
    t = "Version 0.2"; tw = text_w(F_UI, t, S(16)); text_draw(F_UI, x + (cw - tw) / 2, y + S(190), t, TH.dim, S(16));
    const char *lines[] = { "Noyau Linux et BusyBox, bureau MiaouDesk écrit en C",
                            "Polices Nunito et JetBrains Mono (licence OFL)",
                            "Icônes Lucide (licence ISC)",
                            "stb_image et stb_truetype (domaine public)" };
    for (int i = 0; i < 4; i++) { tw = text_w(F_UI, lines[i], S(13)); text_draw(F_UI, x + (cw - tw) / 2, y + S(240) + i * S(24), lines[i], TH.dim, S(13)); }
}

App app_monitor = { "monitor", "Moniteur système", "monitor", "Processus, CPU et mémoire", 860, 600, mon_create, mon_destroy, mon_draw, mon_action, mon_mouse, NULL, mon_tick };
App app_network = { "network", "Réseau", "network", "Interfaces, DHCP et diagnostics", 780, 620, net_create, net_destroy, net_draw, net_action, NULL, NULL, net_tick };
App app_about = { "about", "À propos de MiaouOS", "info", "Version et crédits", 620, 440, NULL, NULL, about_draw, NULL, NULL, NULL, NULL };
