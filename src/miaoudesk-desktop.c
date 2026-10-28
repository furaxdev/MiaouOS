#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

struct fb_var_screeninfo var;
struct fb_fix_screeninfo fix;
static unsigned char *fb;
static unsigned char *drawbuf;
static size_t fb_size;
static int fbfd = -1, ttyfd = -1, mousefd = -1, graphics = 0, raw = 0;
static struct termios saved_term;
static int mouse_x, mouse_y, previous_left;
static int scale = 2;
static unsigned int screen_w, screen_h;

struct button { int x, y, w, h; const char *label; const char *command; };
static struct button buttons[8];

static void restore_terminal(void) {
    if (raw) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_term); raw = 0; }
    if (graphics && ttyfd >= 0) { ioctl(ttyfd, KDSETMODE, KD_TEXT); graphics = 0; }
}
static void enter_graphics_mode(void) {
    if (ioctl(ttyfd, KDSETMODE, KD_GRAPHICS) == 0) graphics = 1;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_term) == 0) {
        struct termios t = saved_term;
        t.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        t.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) raw = 1;
    }
}
static void launch_console(void) {
    restore_terminal();
    if (drawbuf) { free(drawbuf); drawbuf = NULL; }
    if (fb && fb != MAP_FAILED) { munmap(fb, fb_size); fb = NULL; }
    if (fbfd >= 0) { close(fbfd); fbfd = -1; }
    if (mousefd >= 0) { close(mousefd); mousefd = -1; }
    if (ttyfd >= 0) { close(ttyfd); ttyfd = -1; }
    execl("/bin/miaoudesk", "miaoudesk", (char *)NULL);
    _exit(127);
}
static void cleanup(int sig) {
    restore_terminal();
    if (drawbuf) free(drawbuf);
    if (fb && fb != MAP_FAILED) munmap(fb, fb_size);
    if (fbfd >= 0) close(fbfd);
    if (mousefd >= 0) close(mousefd);
    if (ttyfd >= 0) close(ttyfd);
    if (sig) _exit(128 + sig);
}
static uint32_t component(unsigned char value, struct fb_bitfield field) {
    if (!field.length) return 0;
    uint64_t max = field.length >= 32 ? 0xffffffffULL : ((1ULL << field.length) - 1);
    return (uint32_t)(((uint64_t)value * max / 255ULL) << field.offset);
}
static uint32_t color(unsigned char r, unsigned char g, unsigned char b) {
    return component(r, var.red) | component(g, var.green) | component(b, var.blue);
}
static void pixel(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || (unsigned)x >= screen_w || (unsigned)y >= screen_h) return;
    unsigned char *p = drawbuf + (size_t)y * fix.line_length + (size_t)x * (var.bits_per_pixel / 8);
    if (var.bits_per_pixel == 16) { uint16_t v = (uint16_t)c; memcpy(p, &v, 2); }
    else if (var.bits_per_pixel == 24) { p[0] = c & 255; p[1] = (c >> 8) & 255; p[2] = (c >> 16) & 255; }
    else if (var.bits_per_pixel == 32) memcpy(p, &c, 4);
}
static void rect(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)screen_w?(int)screen_w:x+w, y1=y+h>(int)screen_h?(int)screen_h:y+h;
    for (int yy=y0; yy<y1; yy++) for (int xx=x0; xx<x1; xx++) pixel(xx,yy,c);
}
static void border(int x,int y,int w,int h,uint32_t c,int t) {
    rect(x,y,w,t,c); rect(x,y+h-t,w,t,c); rect(x,y,t,h,c); rect(x+w-t,y,t,h,c);
}
#define G(ch,a,b,c,d,e,f,g) case ch: { static const unsigned char p[7]={a,b,c,d,e,f,g}; return p; }
static const unsigned char *glyph(char ch) {
    switch ((unsigned char)toupper((unsigned char)ch)) {
    G('A',14,17,17,31,17,17,17) G('B',30,17,17,30,17,17,30)
    G('C',14,17,16,16,16,17,14) G('D',30,17,17,17,17,17,30)
    G('E',31,16,16,30,16,16,31) G('F',31,16,16,30,16,16,16)
    G('G',14,17,16,23,17,17,15) G('H',17,17,17,31,17,17,17)
    G('I',14,4,4,4,4,4,14) G('J',7,2,2,2,18,18,12)
    G('K',17,18,20,24,20,18,17) G('L',16,16,16,16,16,16,31)
    G('M',17,27,21,21,17,17,17) G('N',17,25,21,19,17,17,17)
    G('O',14,17,17,17,17,17,14) G('P',30,17,17,30,16,16,16)
    G('Q',14,17,17,17,21,18,13) G('R',30,17,17,30,20,18,17)
    G('S',15,16,16,14,1,1,30) G('T',31,4,4,4,4,4,4)
    G('U',17,17,17,17,17,17,14) G('V',17,17,17,17,17,10,4)
    G('W',17,17,17,21,21,21,10) G('X',17,17,10,4,10,17,17)
    G('Y',17,17,10,4,4,4,4) G('Z',31,1,2,4,8,16,31)
    G('0',14,17,19,21,25,17,14) G('1',4,12,4,4,4,4,14)
    G('2',14,17,1,2,4,8,31) G('3',30,1,1,14,1,1,30)
    G('4',2,6,10,18,31,2,2) G('5',31,16,16,30,1,1,30)
    G('6',14,16,16,30,17,17,14) G('7',31,1,2,4,8,8,8)
    G('8',14,17,17,14,17,17,14) G('9',14,17,17,15,1,1,14)
    G('-',0,0,0,31,0,0,0) G('_',0,0,0,0,0,0,31)
    G('.',0,0,0,0,0,12,12) G(':',0,12,12,0,12,12,0)
    G('/',1,2,2,4,8,8,16) G('!',4,4,4,4,4,0,4)
    G('?',14,17,1,2,4,0,4) G('>',16,8,4,2,4,8,16)
    G('<',1,2,4,8,4,2,1) G('(',2,4,8,8,8,4,2)
    G(')',8,4,2,2,2,4,8) G('[',14,8,8,8,8,8,14)
    G(']',14,2,2,2,2,2,14) G('=',0,31,0,31,0,0,0)
    G('+',0,4,4,31,4,4,0)
    G(' ',0,0,0,0,0,0,0)
    default: { static const unsigned char p[7]={31,17,2,4,4,0,4}; return p; }
    }
}
#undef G
static void text_draw(int x, int y, const char *s, uint32_t c, int sc) {
    for (; *s; s++, x += 6*sc) {
        const unsigned char *p = glyph(*s);
        for (int yy=0; yy<7; yy++) for (int xx=0; xx<5; xx++)
            if (p[yy] & (1u << (4-xx))) rect(x+xx*sc,y+yy*sc,sc,sc,c);
    }
}
static int text_width(const char *s, int sc) { return (int)strlen(s)*6*sc; }

#define MAX_WINDOWS 8
struct window { int x,y,w,h,kind,open,minimized,maximized,oldx,oldy,oldw,oldh; };
static struct window wins[MAX_WINDOWS];
static int active=-1, dragging=-1, drag_dx,drag_dy, resizing=-1, resize_start_x, resize_start_y, resize_start_w, resize_start_h, prev_left, show_apps=0, theme=0;
static const char *app_names[]={"FICHIERS","NAVIGATEUR","MIAOU STORE","PARAMETRES","TERMINAL","RESEAU","NEKO","A PROPOS"};
static uint32_t C(unsigned char r,unsigned char g,unsigned char b){return color(r,g,b);}
static int taskbar_h(void){return screen_h<600?42:52;}
static void draw_wallpaper(void){
  uint32_t a=C(20,25,49), b=C(55,29,70), pink=C(244,105,183), violet=C(106,91,194);
  rect(0,0,screen_w,screen_h,a);
  for(int y=0;y<(int)screen_h-taskbar_h();y++){
    unsigned int t=(unsigned int)y*100/(screen_h?screen_h:1);
    uint32_t c=C(20+t/4,24+t/5,48+t/2); rect(0,y,screen_w,1,c);
  }
  int cx=(int)screen_w*3/4, cy=(int)screen_h*2/5, r=(int)(screen_h<screen_w?screen_h:screen_w)/5;
  for(int y=-r;y<r;y++)for(int x=-r;x<r;x++)if(x*x+y*y<r*r)pixel(cx+x,cy+y,C(43,37,82));
  for(int i=0;i<9;i++){int yy=(int)(screen_h*.16)+i*(int)(screen_h*.065); rect(0,yy,screen_w,1,C(36,40,73));}
  /* Cat-ear silhouette and face as a quiet wallpaper motif. */
  rect(cx-r/2,cy-r/2,r/3,r/2,pink); rect(cx+r/5,cy-r/2,r/3,r/2,pink);
  rect(cx-r/2,cy-r/3,r,r*2/3,violet); rect(cx-r/3,cy-r/10,r/8,r/8,C(255,225,246)); rect(cx+r/5,cy-r/10,r/8,r/8,C(255,225,246));
  rect(cx-r/12,cy+r/8,r/6,r/12,pink);
}
static void label(int x,int y,const char*s,uint32_t c,int sc){text_draw(x,y,s,c,sc);}
static void icon(int x,int y,int n,const char*name){
  uint32_t panel=C(39,44,72), edge=C(147,132,190), pink=C(255,142,211), white=C(248,242,255);
  rect(x,y,48,42,panel); border(x,y,48,42,edge,1);
  if(n==0){rect(x+8,y+10,32,23,C(231,173,104));rect(x+8,y+7,15,6,C(244,199,131));}
  else if(n==1){rect(x+10,y+5,28,32,white);rect(x+13,y+9,22,24,C(116,183,230));}
  else if(n==2){rect(x+7,y+9,34,27,pink);rect(x+12,y+5,24,5,white);}
  else if(n==3){rect(x+13,y+5,22,32,C(155,171,211));rect(x+18,y+11,12,20,panel);}
  else if(n==4){rect(x+6,y+7,36,28,C(15,19,30));label(x+11,y+17,">_",pink,2);}
  else if(n==5){rect(x+22,y+6,4,30,pink);rect(x+9,y+19,30,4,pink);}
  else if(n==6){rect(x+12,y+16,24,20,C(242,188,218));rect(x+11,y+6,8,15,C(242,188,218));rect(x+29,y+6,8,15,C(242,188,218));}
  else {rect(x+8,y+8,32,28,C(101,105,160));}
  int tw=text_width(name,1); label(x+(48-tw)/2,y+46,name,white,1);
}
static int open_window(int kind){
  for(int i=0;i<MAX_WINDOWS;i++) if(wins[i].open&&wins[i].kind==kind&&!wins[i].minimized){active=i;return i;}
  for(int i=0;i<MAX_WINDOWS;i++) if(!wins[i].open||wins[i].minimized){
    wins[i]=(struct window){.x=(int)screen_w/5+(i*23),.y=(int)screen_h/8+(i*17),.w=(int)screen_w*3/5,.h=(int)screen_h*3/5,.kind=kind,.open=1,.minimized=0,.maximized=0};
    if(wins[i].w<300)wins[i].w=(int)screen_w-30;if(wins[i].h<180)wins[i].h=(int)screen_h-taskbar_h()-25;
    active=i;show_apps=0;return i;
  }
  return -1;
}
static void app_action(int n){open_window(n);}
static void draw_window(int i){
  struct window*w=&wins[i];if(!w->open||w->minimized)return;
  uint32_t frame=C(28,30,48), title=C(45,40,69), edge=(i==active?C(255,137,208):C(100,103,145)), white=C(246,242,255), muted=C(185,184,212), pink=C(255,130,207);
  rect(w->x+5,w->y+6,w->w,w->h,C(10,12,24));rect(w->x,w->y,w->w,w->h,frame);border(w->x,w->y,w->w,w->h,edge,2);
  rect(w->x+2,w->y+2,w->w-4,30,title);label(w->x+12,w->y+12,app_names[w->kind],white,1);
  rect(w->x+w->w-72,w->y+7,18,18,C(91,94,133));label(w->x+w->w-67,w->y+12,"_",white,1);
  rect(w->x+w->w-48,w->y+7,18,18,C(91,94,133));border(w->x+w->w-44,w->y+11,10,10,white,1);
  rect(w->x+w->w-24,w->y+7,18,18,C(190,65,120));label(w->x+w->w-20,w->y+12,"X",white,1);
  int x=w->x+18,y=w->y+49;
  if(w->kind==0){
    label(x,y,"GESTIONNAIRE DE FICHIERS",pink,1);y+=22;
    label(x,y,"/  RACINE DU SYSTEME",muted,1);y+=24;
    const char*items[]={"bin/","dev/","etc/","home/","proc/","root/","tmp/","usr/","var/"};
    for(int k=0;k<9;k++){rect(x,y+k*20,22,14,C(228,175,105));label(x+31,y+k*20+3,items[k],white,1);}
  }else if(w->kind==1){
    label(x,y,"MIAOU WEB",pink,2);y+=28;rect(x,y,w->w-38,25,C(42,47,69));label(x+8,y+8,"https://miaou.local",white,1);y+=45;
    label(x,y,"Bienvenue sur le navigateur Miaou.",white,1);label(x,y+24,"Moteur web a integrer dans une prochaine version.",muted,1);
  }else if(w->kind==2){
    label(x,y,"MIAOU STORE",pink,2);y+=35;label(x,y,"CATALOGUE DE PAQUETS",white,1);y+=25;
    label(x,y,"miaou-welcome       INSTALLE",muted,1);label(x,y+24,"miaou-cursors       DISPONIBLE",white,1);label(x,y+48,"miaou-neko          DISPONIBLE",white,1);
  }else if(w->kind==3){
    label(x,y,"PERSONNALISEZ VOTRE BUREAU",pink,1);y+=27;
    label(x,y,"THEME",white,1);label(x+110,y,theme?"CLAIR":"NUIT FELINE  [CLIC ICI]",muted,1);
    label(x,y+32,"DOCK",white,1);label(x+110,y+32,"VISIBLE",muted,1);
    label(x,y+64,"NEKO",white,1);label(x+110,y+64,"COMPAGNON FELIN",muted,1);
    label(x,y+105,"Cliquez sur le theme pour le changer.",muted,1);
  }else if(w->kind==4){
    label(x,y,"TERMINAL MIAOU",pink,2);y+=30;label(x,y,"root@miaouos:~# ",C(130,240,169),1);label(x+102,y,"Bureau graphique actif",white,1);label(x,y+23,"Le terminal interactif complet arrive ensuite.",muted,1);
  }else if(w->kind==5){
    label(x,y,"RESEAU",pink,2);y+=30;label(x,y,"Interface reseau : detection en cours",white,1);label(x,y+25,"La configuration necessite les services reseau.",muted,1);
  }else if(w->kind==6){
    label(x,y,"MIAOU NEKO",pink,2);y+=30;label(x,y,"Votre compagnon de bureau",white,1);
    rect(x+20,y+30,70,65,C(242,190,218));rect(x+18,y+9,22,30,C(242,190,218));rect(x+70,y+9,22,30,C(242,190,218));rect(x+34,y+49,7,7,C(40,36,58));rect(x+68,y+49,7,7,C(40,36,58));rect(x+48,y+65,14,5,C(205,91,155));
  }else{
    label(x,y,"MIAOUOS",pink,2);label(x,y+32,"Le Linux des chats !",white,1);label(x,y+58,"MiaouDesk - session graphique",muted,1);
  }
  rect(w->x+w->w-15,w->y+w->h-15,13,13,edge);
}
static void present(void) {
  if (fb && drawbuf) memcpy(fb, drawbuf, fb_size);
}
static void draw_desktop(void){
  draw_wallpaper();uint32_t white=C(250,244,255), muted=C(218,211,237), bar=C(25,27,43), pink=C(255,130,207);
  int sc=screen_w<800?1:2;
  label(28,20,"MIAOUOS",pink,sc+1);label(28,42,"LE LINUX DES CHATS",white,1);
  const int ys[]={82,164,246,328,410,492,574,656};
  int count=screen_h<700?6:8;
  for(int i=0;i<count;i++){int yy=ys[i];if(yy>(int)screen_h-taskbar_h()-65)continue;icon(28,yy,i,app_names[i]);}
  for(int i=0;i<MAX_WINDOWS;i++)draw_window(i);
  int bh=taskbar_h(), by=(int)screen_h-bh;rect(0,by,screen_w,bh,bar);rect(0,by,screen_w,2,C(116,91,158));
  rect(10,by+7,38,bh-14,C(93,55,111));label(18,by+bh/2-4,"M",white,2);
  int tx=60;
  for(int i=0;i<MAX_WINDOWS;i++)if(wins[i].open){int ww=100;rect(tx,by+7,ww,bh-14,i==active?C(73,52,88):C(39,42,61));label(tx+7,by+bh/2-4,app_names[wins[i].kind],white,1);tx+=ww+5;}
  if(show_apps){rect(10,by-205,235,195,C(34,31,52));border(10,by-205,235,195,C(150,111,176),2);label(24,by-185,"APPLICATIONS",pink,1);for(int i=0;i<8;i++)label(24,by-160+i*19,app_names[i],white,1);}
  label((int)screen_w-88,by+bh/2-4,"19:00",white,1);
  label((int)screen_w-150,by+bh/2-4,"NET  VOL",muted,1);
  (void)muted;
}
static void click_desktop(int x,int y){
 int bh=taskbar_h(), by=(int)screen_h-bh;
 if(y>=by){if(x<52){show_apps=!show_apps;return;}int tx=60;for(int i=0;i<MAX_WINDOWS;i++)if(wins[i].open){if(x>=tx&&x<tx+100){if(wins[i].minimized){wins[i].minimized=0;active=i;}else if(active==i)wins[i].minimized=1;else active=i;return;}tx+=105;}return;}
 if(show_apps&&x<250&&y>=by-205&&y<by-10){int n=(by-10-y)/19-1;if(n>=0&&n<8)app_action(n);show_apps=0;return;}
 for(int i=MAX_WINDOWS-1;i>=0;i--){struct window*w=&wins[i];if(!w->open||w->minimized)continue;
  if(x>=w->x+w->w-25&&x<w->x+w->w&&y>=w->y+4&&y<w->y+28){w->open=0;if(active==i)active=-1;return;}
  if(x>=w->x+w->w-50&&x<w->x+w->w-25&&y>=w->y+4&&y<w->y+28){if(!w->maximized){w->oldx=w->x;w->oldy=w->y;w->oldw=w->w;w->oldh=w->h;w->x=10;w->y=10;w->w=(int)screen_w-20;w->h=(int)screen_h-bh-20;w->maximized=1;}else{w->x=w->oldx;w->y=w->oldy;w->w=w->oldw;w->h=w->oldh;w->maximized=0;}active=i;return;}
  if(x>=w->x+w->w-17&&x<w->x+w->w&&y>=w->y+w->h-17&&y<w->y+w->h){active=i;resizing=i;resize_start_x=x;resize_start_y=y;resize_start_w=w->w;resize_start_h=w->h;return;}
  if(x>=w->x+w->w-74&&x<w->x+w->w-50&&y>=w->y+4&&y<w->y+28){w->minimized=1;if(active==i)active=-1;return;}
  if(x>=w->x&&x<w->x+w->w&&y>=w->y&&y<w->y+32){active=i;dragging=i;drag_dx=x-w->x;drag_dy=y-w->y;return;}
  if(x>=w->x&&x<w->x+w->w&&y>=w->y&&y<w->y+w->h){active=i;if(w->kind==3&&y>w->y+60&&y<w->y+110)theme=!theme;return;}
 }
 for(int i=0;i<8;i++){int yy=82+i*82;if(x>=28&&x<84&&y>=yy&&y<yy+65){app_action(i);return;}}
}
int main(void){
 signal(SIGINT,cleanup);signal(SIGTERM,cleanup);signal(SIGHUP,cleanup);
 fbfd=open("/dev/fb0",O_RDWR);if(fbfd<0)return 1;ttyfd=open("/dev/tty0",O_RDWR|O_CLOEXEC);if(ttyfd<0){close(fbfd);return 1;}
 if(ioctl(fbfd,FBIOGET_FSCREENINFO,&fix)<0||ioctl(fbfd,FBIOGET_VSCREENINFO,&var)<0){close(fbfd);close(ttyfd);return 1;}
 if(var.bits_per_pixel!=16&&var.bits_per_pixel!=24&&var.bits_per_pixel!=32){close(fbfd);close(ttyfd);return 1;}
 screen_w=var.xres;screen_h=var.yres;fb_size=fix.smem_len;fb=mmap(NULL,fb_size,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0);
 if(fb==MAP_FAILED){fb=NULL;close(fbfd);close(ttyfd);return 1;}
 drawbuf=malloc(fb_size);if(!drawbuf){munmap(fb,fb_size);close(fbfd);close(ttyfd);return 1;}memcpy(drawbuf,fb,fb_size);
 scale=screen_w<800?1:2;mouse_x=80;mouse_y=100;mousefd=open("/dev/input/mice",O_RDONLY|O_NONBLOCK);
 enter_graphics_mode();draw_desktop();present();
 int serial=open("/dev/ttyS0",O_WRONLY|O_CLOEXEC);if(graphics&&serial>=0){const char marker[]="MIAOUDESK_GUI_OK\n";(void)write(serial,marker,sizeof(marker)-1);close(serial);}
 unsigned char packet[3],buf[64];int pn=0, escape_pending=0;
 for(;;){
  struct pollfd fds[2]={{STDIN_FILENO,POLLIN,0},{mousefd,POLLIN,0}};int nf=mousefd>=0?2:1;int pr=poll(fds,nf,100);
  if(pr<0&&errno==EINTR)continue;
  if(pr>0&&(fds[0].revents&POLLIN)){unsigned char key;if(read(STDIN_FILENO,&key,1)==1){if(escape_pending==1){if(key=='[')escape_pending=2;else if(key=='\t'){for(int step=1;step<=MAX_WINDOWS;step++){int k=active<0?step-1:(active+step)%MAX_WINDOWS;if(wins[k].open&&!wins[k].minimized){active=k;break;}}escape_pending=0;}else escape_pending=0;}else if(escape_pending==2){if(key=='Z'){for(int step=1;step<=MAX_WINDOWS;step++){int k=active<0?MAX_WINDOWS-step:(active-step+MAX_WINDOWS*2)%MAX_WINDOWS;if(wins[k].open&&!wins[k].minimized){active=k;break;}}}escape_pending=0;}else if(key==27)escape_pending=1;else if(key=='\t'){for(int step=1;step<=MAX_WINDOWS;step++){int k=active<0?step-1:(active+step)%MAX_WINDOWS;if(wins[k].open&&!wins[k].minimized){active=k;break;}}}else if(key=='q'||key=='Q')launch_console();else if(key>='1'&&key<='8')app_action(key-'1');draw_desktop();present();}}
  if(mousefd>=0&&pr>0&&(fds[1].revents&POLLIN)){ssize_t n=read(mousefd,buf,sizeof(buf));for(ssize_t j=0;j<n;j++){unsigned char v=buf[j];if(pn==0&&!(v&8))continue;packet[pn++]=v;if(pn==3){mouse_x+=(int8_t)packet[1];mouse_y-=(int8_t)packet[2];if(mouse_x<0)mouse_x=0;if(mouse_y<0)mouse_y=0;if(mouse_x>=(int)screen_w)mouse_x=(int)screen_w-1;if(mouse_y>=(int)screen_h)mouse_y=(int)screen_h-1;int left=packet[0]&1;if(left&&!prev_left)click_desktop(mouse_x,mouse_y);if(!left&&prev_left){dragging=-1;resizing=-1;}if(left&&dragging>=0){wins[dragging].x=mouse_x-drag_dx;wins[dragging].y=mouse_y-drag_dy;if(wins[dragging].x<0)wins[dragging].x=0;if(wins[dragging].y<0)wins[dragging].y=0;}if(left&&resizing>=0){struct window *rw=&wins[resizing];rw->w=resize_start_w+mouse_x-resize_start_x;rw->h=resize_start_h+mouse_y-resize_start_y;if(rw->w<220)rw->w=220;if(rw->h<120)rw->h=120;if(rw->x+rw->w>(int)screen_w)rw->w=(int)screen_w-rw->x;if(rw->y+rw->h>(int)screen_h-taskbar_h())rw->h=(int)screen_h-taskbar_h()-rw->y;}prev_left=left;pn=0;if(left||dragging>=0||resizing>=0){draw_desktop();present();}}}}
 }
}
