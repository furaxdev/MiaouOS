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
static void cleanup(int sig) {
    restore_terminal();
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
    unsigned char *p = fb + (size_t)y * fix.line_length + (size_t)x * (var.bits_per_pixel / 8);
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
static void setup_buttons(void) {
    int margin=(int)(screen_w*0.045), gap=(int)(screen_w*0.025);
    int top=(int)(screen_h*0.22), bw=(int)((screen_w-2*margin-2*gap)/3), bh=(int)(screen_h*0.17), vgap=(int)(screen_h*0.045);
    const char *labels[8]={"APPS","STORE","FILES","SETTINGS","NETWORK","NEKO","TERMINAL","CONSOLE"};
    const char *cmds[8]={"/usr/bin/miaou-apps","/usr/bin/miaou","/usr/bin/miaou-files","/usr/bin/miaou-settings","/usr/bin/miaou-network","/usr/bin/miaou-neko play","/bin/sh",NULL};
    for(int i=0;i<8;i++){ int row=i/3,col=i%3; buttons[i].x=margin+col*(bw+gap); buttons[i].y=top+row*(bh+vgap); buttons[i].w=bw; buttons[i].h=bh; buttons[i].label=labels[i]; buttons[i].command=cmds[i]; }
}
static void draw_button(struct button *b, int sc) {
    uint32_t panel=color(39,44,63), outline=color(90,98,130), white=color(244,245,255), pink=color(255,130,210);
    rect(b->x,b->y,b->w,b->h,panel); border(b->x,b->y,b->w,b->h,outline,2);
    int tw=text_width(b->label,sc);
    text_draw(b->x+(b->w-tw)/2,b->y+(b->h-7*sc)/2,b->label,pink,sc);
    (void)white;
}
static void draw_mouse(void) {
    uint32_t c=color(255,255,255);
    rect(mouse_x-2,mouse_y-9,3,19,c); rect(mouse_x-9,mouse_y-2,19,3,c);
}
static void draw_ui(void) {
    uint32_t bg=color(19,22,35), top=color(31,35,52), white=color(245,246,255), pink=color(255,130,210), muted=color(170,178,203);
    rect(0,0,screen_w,screen_h,bg);
    rect(0,0,screen_w,(int)(screen_h*0.13),top);
    int sc=scale;
    text_draw((int)(screen_w*0.045),(int)(screen_h*0.045),"MIAOUOS",pink,sc+1);
    text_draw((int)(screen_w*0.30),(int)(screen_h*0.055),"LE LINUX DES CHATS",white,sc);
    text_draw((int)(screen_w*0.045),(int)(screen_h*0.16),"BIENVENUE DANS MIAOUDESK",white,sc);
    text_draw((int)(screen_w*0.045),(int)(screen_h*0.185),"CLIQUEZ SUR UNE CARTE OU UTILISEZ LES TOUCHES 1 A 8",muted,sc>1?1:1);
    for(int i=0;i<8;i++) draw_button(&buttons[i],sc);
    int bar=(int)(screen_h*0.08);
    rect(0,(int)screen_h-bar,screen_w,bar,top);
    text_draw((int)(screen_w*0.045),(int)screen_h-bar/2-4,"MIAOUOS 0.1  |  SOURIS + CLAVIER  |  Q : CONSOLE",muted,1);
    draw_mouse();
}
static void enter_text_mode(void) { if(graphics){ioctl(ttyfd,KDSETMODE,KD_TEXT);graphics=0;} if(raw){tcsetattr(STDIN_FILENO,TCSAFLUSH,&saved_term);raw=0;} }
static void enter_graphics_mode(void) { struct termios t; if(tcgetattr(STDIN_FILENO,&saved_term)==0){t=saved_term; cfmakeraw(&t); tcsetattr(STDIN_FILENO,TCSAFLUSH,&t); raw=1;} if(ioctl(ttyfd,KDSETMODE,KD_GRAPHICS)==0) graphics=1; }
static void launch_console(void) { enter_text_mode(); execl("/bin/miaoudesk","miaoudesk",(char*)NULL); _exit(127); }
static void launch_app(int index) {
    if(index<0 || index>=8) return;
    if(!buttons[index].command) { launch_console(); return; }
    enter_text_mode();
    system(buttons[index].command);
    enter_graphics_mode();
    draw_ui();
}
static void click_at(int x,int y) {
    for(int i=0;i<8;i++) if(x>=buttons[i].x&&x<buttons[i].x+buttons[i].w&&y>=buttons[i].y&&y<buttons[i].y+buttons[i].h){launch_app(i);return;}
}
int main(void) {
    signal(SIGINT,cleanup); signal(SIGTERM,cleanup); signal(SIGHUP,cleanup);
    fbfd=open("/dev/fb0",O_RDWR); if(fbfd<0) return 1;
    ttyfd=open("/dev/tty0",O_RDWR|O_CLOEXEC); if(ttyfd<0) {close(fbfd);return 1;}
    if(ioctl(fbfd,FBIOGET_FSCREENINFO,&fix)<0||ioctl(fbfd,FBIOGET_VSCREENINFO,&var)<0) {close(fbfd);close(ttyfd);return 1;}
    if(var.bits_per_pixel!=16&&var.bits_per_pixel!=24&&var.bits_per_pixel!=32){close(fbfd);close(ttyfd);return 1;}
    screen_w=var.xres; screen_h=var.yres; fb_size=fix.smem_len;
    fb=mmap(NULL,fb_size,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0); if(fb==MAP_FAILED){fb=NULL;close(fbfd);close(ttyfd);return 1;}
    scale=(screen_w<800||screen_h<600)?1:2;
    setup_buttons();
    mouse_x=(int)screen_w/2; mouse_y=(int)screen_h/2;
    mousefd=open("/dev/input/mice",O_RDONLY|O_NONBLOCK);
    enter_graphics_mode(); draw_ui();
    { int serial = open("/dev/ttyS0", O_WRONLY | O_CLOEXEC); if (serial >= 0) { const char marker[] = "MIAOUDESK_GUI_OK\n"; (void)write(serial, marker, sizeof(marker)-1); close(serial); } }
    unsigned char packet[3]; int packet_n=0; unsigned char buf[64];
    for(;;){
        struct pollfd fds[2]={{STDIN_FILENO,POLLIN,0},{mousefd,POLLIN,0}};
        int nf=mousefd>=0?2:1; int pr=poll(fds,nf,100);
        if(pr<0&&errno==EINTR) continue;
        if(pr>0&&(fds[0].revents&POLLIN)){ unsigned char key; if(read(STDIN_FILENO,&key,1)==1){if(key=='q'||key=='Q')launch_console(); if(key>='1'&&key<='8')launch_app(key-'1');}}
        if(mousefd>=0&&pr>0&&(fds[1].revents&POLLIN)){
            ssize_t n=read(mousefd,buf,sizeof(buf));
            for(ssize_t j=0;j<n;j++){
                unsigned char v=buf[j];
                if(packet_n==0&&!(v&0x08)) continue;
                packet[packet_n++]=v;
                if(packet_n==3){
                    mouse_x+=(int8_t)packet[1]; mouse_y-=(int8_t)packet[2];
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_y < 0) mouse_y = 0;
                    if(mouse_x>=(int)screen_w)mouse_x=(int)screen_w-1;
                    if(mouse_y>=(int)screen_h)mouse_y=(int)screen_h-1;
                    int left=packet[0]&1;
                    if(left&&!previous_left) click_at(mouse_x,mouse_y);
                    previous_left=left; packet_n=0; draw_ui();
                }
            }
        }
    }
    return 0;
}
