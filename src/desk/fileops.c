/* File operations shared by the desktop, the file manager and the editor: trash, copy/cut/paste, create. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "desk.h"

static int run_argv(char *const argv[]) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) { int nul = open("/dev/null", 2); if (nul >= 0) { dup2(nul, 1); dup2(nul, 2); } execvp(argv[0], argv); _exit(127); }
    int st; waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static void mkdir_p(const char *path) {
    char tmp[600]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}
void fo_unique(const char *dir, const char *name, char *out, size_t n) {
    snprintf(out, n, "%s/%s", dir, name);
    for (int i = 1; i < 1000 && access(out, F_OK) == 0; i++) {
        const char *dot = strrchr(name, '.');
        if (dot && dot != name) snprintf(out, n, "%s/%.*s (%d)%s", dir, (int)(dot - name), name, i, dot); else snprintf(out, n, "%s/%s (%d)", dir, name, i);
    }
}
int fo_trash(const char *path) {
    char files[400], info[400], base[300], dest[700], tmp[700];
    snprintf(files, sizeof files, "%s/.local/share/Trash/files", cur_home); snprintf(info, sizeof info, "%s/.local/share/Trash/info", cur_home);
    mkdir_p(files); mkdir_p(info);
    snprintf(tmp, sizeof tmp, "%s", path); snprintf(base, sizeof base, "%s", basename(tmp));
    fo_unique(files, base, dest, sizeof dest);
    if (rename(path, dest) != 0) { char *argv[] = { "mv", (char *)path, dest, NULL }; if (run_argv(argv) != 0) return -1; }
    char ip[800]; snprintf(ip, sizeof ip, "%s/%s.trashinfo", info, strrchr(dest, '/') + 1);
    FILE *f = fopen(ip, "w");
    if (f) { time_t t = time(NULL); struct tm lt; char d[32] = ""; if (localtime_r(&t, &lt)) strftime(d, sizeof d, "%Y-%m-%dT%H:%M:%S", &lt); fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", path, d); fclose(f); }
    return 0;
}
int fo_trash_empty(void) {
    char files[400], info[400];
    snprintf(files, sizeof files, "%s/.local/share/Trash/files", cur_home); snprintf(info, sizeof info, "%s/.local/share/Trash/info", cur_home);
    char *a1[] = { "rm", "-rf", files, NULL }, *a2[] = { "rm", "-rf", info, NULL };
    int r = run_argv(a1) | run_argv(a2); mkdir_p(files); mkdir_p(info);
    return r;
}
int fo_restore(const char *trashed_name) {       /* name inside Trash/files; returns 0 on success */
    char ip[800], from[800], line[700], orig[700] = "";
    snprintf(ip, sizeof ip, "%s/.local/share/Trash/info/%s.trashinfo", cur_home, trashed_name);
    snprintf(from, sizeof from, "%s/.local/share/Trash/files/%s", cur_home, trashed_name);
    FILE *f = fopen(ip, "r"); if (!f) return -1;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "Path=", 5)) { snprintf(orig, sizeof orig, "%s", line + 5); orig[strcspn(orig, "\r\n")] = 0; }
    fclose(f);
    if (!orig[0]) return -1;
    char od[700], ob[300], dest[900], tmp[700]; snprintf(tmp, sizeof tmp, "%s", orig); snprintf(od, sizeof od, "%s", dirname(tmp));
    snprintf(tmp, sizeof tmp, "%s", orig); snprintf(ob, sizeof ob, "%s", basename(tmp));
    mkdir_p(od); fo_unique(od, ob, dest, sizeof dest);
    if (rename(from, dest) != 0) { char *argv[] = { "mv", from, dest, NULL }; if (run_argv(argv) != 0) return -1; }
    unlink(ip);
    return 0;
}
static char fclip[32][512]; static int nfclip, fcut;
void fo_clip_set(char paths[][512], int n, int cut) { if (n > 32) n = 32; for (int i = 0; i < n; i++) snprintf(fclip[i], 512, "%s", paths[i]); nfclip = n; fcut = cut; }
int fo_clip_count(void) { return nfclip; }
int fo_clip_cut(void) { return fcut; }
int fo_paste(const char *destdir) {
    int ok = 0;
    for (int i = 0; i < nfclip; i++) {
        char tmp[600], dest[900]; snprintf(tmp, sizeof tmp, "%s", fclip[i]);
        fo_unique(destdir, basename(tmp), dest, sizeof dest);
        if (fcut) { if (rename(fclip[i], dest) == 0) ok++; else { char *argv[] = { "mv", fclip[i], dest, NULL }; if (run_argv(argv) == 0) ok++; } }
        else { char *argv[] = { "cp", "-a", fclip[i], dest, NULL }; if (run_argv(argv) == 0) ok++; }
    }
    if (fcut) nfclip = 0;
    return ok;
}
int fo_mkdir(const char *path) { return mkdir(path, 0755); }
int fo_touch(const char *path) { FILE *f = fopen(path, "ab"); if (!f) return -1; fclose(f); return 0; }
int fo_run(char *const argv[]) { return run_argv(argv); }
void fo_mkdir_p(const char *path) { mkdir_p(path); }
