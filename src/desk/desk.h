/* MiaouDesk - shared declarations. Framebuffer desktop for MiaouOS. */
#ifndef MIAOU_DESK_H
#define MIAOU_DESK_H
#include <stdint.h>
#include <stddef.h>

typedef uint32_t u32;
#define RGB(r, g, b) ((u32)(((r) << 16) | ((g) << 8) | (b)))

/* ---- gfx.c ---- */
extern int SW, SH;       /* screen size */
extern int UIP;          /* ui scale in percent (100, 125, 150, 200) */
#define S(n) ((int)(((n) * UIP + 50) / 100))

typedef struct Font Font;
typedef struct Img { int w, h; u32 *px; /* ARGB straight alpha */ int mono; /* white mask tinted at draw time */ } Img;

int  gfx_open(void);                       /* open fb, allocate buffers; 0 = ok */
void gfx_close(void);
void gfx_clip(int x, int y, int w, int h);
void gfx_unclip(void);
void gfx_set_base_clip(int x, int y, int w, int h);   /* partial redraw: clip everything to this rectangle */
void gfx_clear_base_clip(void);
void gfx_fill(int x, int y, int w, int h, u32 c, int a);       /* a: 0..255 */
void gfx_rrect(int x, int y, int w, int h, int r, u32 c, int a);
void gfx_rrect_line(int x, int y, int w, int h, int r, u32 c, int a);
void gfx_shadow(int x, int y, int w, int h, int r, int spread, int maxa);
void gfx_circle(int cx, int cy, int r, u32 c, int a);
void gfx_hline(int x, int y, int w, u32 c, int a);
void gfx_vline(int x, int y, int h, u32 c, int a);
void gfx_blit(const u32 *src, int sw, int sh, int dx, int dy);   /* opaque copy of an RGB buffer */
void gfx_fb_pixel(int x, int y, u32 rgb);
void desk_mode_changed(void);
void gfx_present(void);                    /* scene -> framebuffer (whole) */
void gfx_present_rect(int x, int y, int w, int h);
u32  gfx_mix(u32 a, u32 b, int t256);
typedef struct { u32 *px; int x, y, w, h; } Snap;
Snap gfx_snap(int x, int y, int w, int h);
void gfx_snap_blend(Snap *s, int alpha);
Img *img_scale_smooth(Img *src, int w, int h);
Img *wallpaper_blur(int night);
int  gfx_save_png(const char *path);

Font *font_load(const char *path);
int   text_w(Font *f, const char *s, int px);
int   text_draw(Font *f, int x, int y, const char *s, u32 c, int px);   /* y = top of line box; returns advance */
int   text_draw_clip(Font *f, int x, int y, const char *s, u32 c, int px, int maxw);
int   font_line_h(Font *f, int px);
int   font_cell_w(Font *f, int px);
int   glyph_draw(Font *f, int x, int y, int cp, u32 c, int px);  /* one codepoint, returns advance */
int   utf8_next(const char **s);           /* decode next codepoint, advances */
int   utf8_put(char *out, int cp);         /* encode, returns length */

Img  *img_get(const char *name, int size);      /* assets/icons/name.png scaled to size x size */
Img  *img_load_file(const char *path);
Img  *img_scale(Img *src, int w, int h);
void  img_draw(Img *im, int x, int y, int a);              /* mono icons use the theme text colour */
void  img_draw_tint(Img *im, int x, int y, int a, u32 color);
void  gfx_line(float x0, float y0, float x1, float y1, float width, u32 c, int a);
void  gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c, int a);
void  gfx_rtri(float x0, float y0, float x1, float y1, float x2, float y2, float rho, u32 c, int a);   /* rounded triangle */
void  gfx_logo(int x, int y, int size, u32 color, u32 eye);   /* the MiaouOS cat-head logo */
void  img_free(Img *im);
Img  *gfx_cache_render(int w, int h, void (*draw)(void *ud, int ox, int oy), void *ud);   /* render to a private ARGB image */
extern int icon_ears;                                       /* 0 none, 1 rounded, 2 pointed cat ears on icons */
void  icon_tile(const char *name, int x, int y, int size);   /* colourful rounded app icon */
Img  *wallpaper(int night);                      /* scaled to screen (cover) */

/* ---- theme ---- */
typedef struct Theme {
    u32 bg, panel, win, win_title, win_title_dim, text, dim, accent, accent2, btn, btn_hot, line, field, sel, good, bad, warn;
    int dark;
} Theme;
extern Theme TH;
extern int theme_dark, accent_idx;
void theme_apply(void);

/* ---- input ---- */
#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4
#define MOD_META  8
#define K_UP     0x200001
#define K_DOWN   0x200002
#define K_LEFT   0x200003
#define K_RIGHT  0x200004
#define K_HOME   0x200005
#define K_END    0x200006
#define K_PGUP   0x200007
#define K_PGDN   0x200008
#define K_DELETE 0x200009
#define K_INSERT 0x20000a
#define K_F1     0x200010   /* .. K_F12 = K_F1+11 */
#define K_ESC    27
#define K_TAB    9
#define K_BS     8
#define K_ENTER  10

enum { M_MOVE, M_DOWN, M_UP, M_WHEEL, M_RDOWN };

/* ---- windows / apps ---- */
#define MAX_ZONES 160
typedef struct Win Win;
typedef struct App {
    const char *id, *title, *icon, *desc;
    int w, h;                                   /* default size at 100% */
    void *(*create)(Win *w);
    void (*destroy)(Win *w);
    void (*draw)(Win *w, int x, int y, int cw, int ch);
    void (*action)(Win *w, int id, int mx, int my, int btn);  /* clicked zone */
    void (*mouse)(Win *w, int type, int x, int y, int arg);   /* raw mouse in content (move/up/wheel) */
    void (*key)(Win *w, int key, int mods);
    int  (*tick)(Win *w);                       /* returns 1 if redraw needed */
    void (*open_arg)(Win *w, const char *path); /* open a file/folder given by the shell */
    int  (*cursor)(Win *w, int x, int y);       /* optional: mouse cursor kind (CK_*) at a point of the content */
} App;

struct Win {
    App *app; void *st;
    long t_open, t_die, t_min, t_geo; int dying, minimizing, gx, gy, gw, gh, tx, ty, tw, th, ws, top;
    int x, y, w, h, minimized, maximized, ox, oy, ow, oh, closing, snapped;
    char title[64];
    struct { int x, y, w, h, id; } z[MAX_ZONES];
    int nz;
    int cx, cy, cw, ch;                         /* content rect (screen coords) */
    unsigned serial;
};
extern Win *focus_win;
extern int mouse_x, mouse_y;
extern int want_blink;               /* set during draw when a caret is visible (requests periodic redraw) */
int ui_auto_scale(int screen_w);
/* mouse cursor kinds */
enum { CK_ARROW, CK_HAND, CK_IBEAM, CK_MOVE, CK_RSH, CK_RSV, CK_RSD1, CK_RSD2, CK_BUSY, CK_N };
void cursor_hint(int x, int y, int w, int h, int kind);   /* during draw: this rectangle shows another cursor */
void zone(Win *w, int x, int y, int cw, int ch, int id);
int  hover(int x, int y, int w, int h);          /* pointer inside rect? */
void dirty(void);                                  /* redraw the whole screen */
void dirty_rect(int x, int y, int w, int h);        /* redraw only this region (much cheaper on slow CPUs) */
void toast(const char *fmt, ...);
Win *open_app(const char *id);
Win *open_app_arg(const char *id, const char *arg);
void open_path(const char *path);
long now_ms(void);
extern int anim_enabled;
extern char cur_home[96];
void fo_unique(const char *dir, const char *name, char *out, size_t n);
int fo_trash(const char *path);
int fo_trash_empty(void);
int fo_restore(const char *trashed_name);
void fo_clip_set(char paths[][512], int n, int cut);
int fo_clip_count(void);
int fo_clip_cut(void);
int fo_paste(const char *destdir);
int fo_mkdir(const char *path);
int fo_touch(const char *path);
int fo_run(char *const argv[]);
void fo_mkdir_p(const char *path);
void close_win(Win *w);
void set_title(Win *w, const char *t);
extern Font *F_UI, *F_BOLD, *F_MONO;
void power_action(int reboot);
void desk_to_console(void);
const char *cfg_get(const char *key, const char *def);
void cfg_set(const char *key, const char *val);
int  kbd_layout(void);
void kbd_set_layout(int fr);

/* widgets shared by apps (apps.c) */
int  ui_button(Win *w, int x, int y, int bw, int bh, const char *label, const char *icon, int id, int primary);
int  ui_tab(Win *w, int x, int y, int bw, int bh, const char *label, const char *icon, int id, int selected);
void ui_field(int x, int y, int fw, int fh, const char *text, int focused, const char *placeholder);
void ui_scrollbar(int x, int y, int h, int total, int shown, int pos);
void ui_progress(int x, int y, int w, int h, int pct, u32 c);
char *read_file(const char *path, size_t *n, size_t max);
void human_size(unsigned long long b, char *out, size_t n);

/* ---- jobs: async child process with captured output ---- */
typedef struct Job { int pid, fd; char *buf; size_t len, cap; int done, status; } Job;
Job *job_start(const char *cmd);
int  job_poll(Job *j);            /* returns 1 if new data / state change */
void job_free(Job *j);
void job_kill(Job *j);

/* ---- session: users, login screen, lock ---- */
typedef struct User { char name[32], display[48], salt[20], hash[68], home[96]; int uid, gid, admin; } User;
extern User users[16];
extern int nusers;
extern User *cur_user;
extern int locked;
void session_init(void);                           /* load /etc/miaou/users */
int  session_autologin(const char *name);
void session_lock(void);
void session_logout(void);
int  session_is_admin(void);
int  login_tick(void);                             /* returns 1 while the login screen animates */
void login_draw(void);
void login_key(int key, int mods);
void login_click(int x, int y);
int  login_fading(void);
int  user_add(const char *name, const char *display, const char *pw, int admin);
int  user_del(const char *name);
int  user_setpw(const char *name, const char *pw);
void users_save(void);
int  kbd_caps(void);
void cfg_reload(const char *path);
void apply_config(void);
void slog_marker(const char *m);
void slog_marker2(const char *m, const char *arg);
int login_fade_alpha(void);
void desk_close_all(void);

extern App app_terminal, app_files, app_settings, app_monitor, app_network, app_store, app_browser, app_about, app_notes, app_calc, app_viewer, app_clock;
void clip_set(const char *s, size_t n);
const char *clip_get(size_t *n);
void wallpaper_set_custom(const char *path);
void clock_timers_tick(void);
int miaou_calc(const char *src, char *out, size_t n);
int input_mods(void);
const char *net_summary(int *up);
void splash_draw(void);
extern App *APPS[];
extern int NAPPS;
#endif
