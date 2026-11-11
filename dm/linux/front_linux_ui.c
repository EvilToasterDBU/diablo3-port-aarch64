#include "front_linux.h"
#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ROWS 12
#define UI_W 720
#define UI_H 640
#define UI_KH 220

typedef struct { int lang, handheld, aspect, loading, sharp, fps, deadzone, gear, alpha, size, selected; } settings_t;
static settings_t s;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int menu_open;
static int text_keyboard;
static char text_prompt[128];
static char config_path[1024];
static FT_Library font_library;
static FT_Face ui_font;

static const char *path(void) {
    if (!config_path[0]) {
        const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
        if (x && *x) snprintf(config_path, sizeof config_path, "%s/diablo3-port/settings.conf", x);
        else snprintf(config_path, sizeof config_path, "%s/.config/diablo3-port/settings.conf", h ? h : ".");
    }
    return config_path;
}
static void read_value(const char *key, char *out, size_t n) {
    FILE *f = fopen(path(), "r"); if (!f) return;
    char k[32], v[64]; while (fscanf(f, "%31[^=]=%63s\n", k, v) == 2) if (!strcmp(k, key)) { snprintf(out,n,"%s",v); break; }
    fclose(f);
}
static void write_value(const char *key, const char *value) {
    char keys[32][32], vals[32][64], k[32], v[64], temp[1100]; int n=0, found=0;
    FILE *f=fopen(path(),"r"); if(f){while(n<32 && fscanf(f,"%31[^=]=%63s\n",k,v)==2){if(!strcmp(k,key)){snprintf(v,sizeof v,"%s",value);found=1;}snprintf(keys[n],32,"%s",k);snprintf(vals[n++],64,"%s",v);}fclose(f);}
    if(!found&&n<32){snprintf(keys[n],32,"%s",key);snprintf(vals[n++],64,"%s",value);}
    snprintf(temp,sizeof temp,"%s.tmp",path()); char dir[1024];snprintf(dir,sizeof dir,"%s",path());char *slash=strrchr(dir,'/');if(slash){*slash=0;char parent[1024];snprintf(parent,sizeof parent,"%s",dir);char *up=strrchr(parent,'/');if(up){*up=0;mkdir(parent,0700);}mkdir(dir,0700);}
    f=fopen(temp,"w");if(!f)return;for(int i=0;i<n;i++)fprintf(f,"%s=%s\n",keys[i],vals[i]);if(fclose(f)||rename(temp,path()))remove(temp);
}
static int bounded(int v,int min,int max){return v<min?min:v>max?max:v;}
void dm_linux_ui_init(void) {
    const char *font_path=getenv("DM_FONT_FILE");
    if(!font_path||!*font_path)font_path="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    if(FT_Init_FreeType(&font_library)==0&&FT_New_Face(font_library,font_path,0,&ui_font)==0)
        fprintf(stderr,"[dm] UI font loaded: %s\n",font_path);
    else fprintf(stderr,"[dm] UI font unavailable: %s\n",font_path);
    char v[64]; memset(&s,0,sizeof s);
    snprintf(v,sizeof v,"ru-RU");read_value("lang",v,sizeof v);s.lang=!strcmp(v,"en-US")?1:!strcmp(v,"en-GB")?2:0;
    snprintf(v,sizeof v,"0");read_value("handheld",v,sizeof v);s.handheld=atoi(v)!=0;
    snprintf(v,sizeof v,"0");read_value("aspect",v,sizeof v);s.aspect=bounded(atoi(v),0,2);
    snprintf(v,sizeof v,"1");read_value("stretch_loading",v,sizeof v);s.loading=atoi(v)!=0;
    snprintf(v,sizeof v,"0");read_value("sharpen",v,sizeof v);s.sharp=!strcmp(v,"1.0")?3:!strcmp(v,"0.5")?2:!strcmp(v,"0.2")?1:0;
    snprintf(v,sizeof v,"60");read_value("fps",v,sizeof v);s.fps=atoi(v)==30?0:atoi(v)==120?2:1;
    snprintf(v,sizeof v,"5");read_value("deadzone",v,sizeof v);s.deadzone=bounded(atoi(v)/5,0,4);
    snprintf(v,sizeof v,"1");read_value("show_gear",v,sizeof v);s.gear=atoi(v)!=0;
    snprintf(v,sizeof v,"100");read_value("gear_alpha",v,sizeof v);s.alpha=bounded(atoi(v)/25-1,0,3);
    snprintf(v,sizeof v,"100");read_value("gear_scale",v,sizeof v);s.size=bounded((atoi(v)-75)/25,0,3);
}
void dm_linux_ui_toggle(void){pthread_mutex_lock(&mu);menu_open=!menu_open;pthread_mutex_unlock(&mu);}
int dm_linux_ui_is_open(void){pthread_mutex_lock(&mu);int v=menu_open;pthread_mutex_unlock(&mu);return v;}
void dm_linux_ui_text_open(const char *title){pthread_mutex_lock(&mu);text_keyboard=1;int ascii=title&&*title;for(const unsigned char*p=(const unsigned char*)title;ascii&&*p;p++)if(*p<32||*p>126)ascii=0;snprintf(text_prompt,sizeof text_prompt,"%s",ascii?title:"CHARACTER NAME");pthread_mutex_unlock(&mu);}
void dm_linux_ui_text_close(void){pthread_mutex_lock(&mu);text_keyboard=0;pthread_mutex_unlock(&mu);}
int dm_linux_ui_text_active(void){pthread_mutex_lock(&mu);int v=text_keyboard;pthread_mutex_unlock(&mu);return v;}
static void adjust(int dir) {
    const char *key=NULL,*value=NULL;
    if(s.selected==0){s.lang=(s.lang+3+dir)%3;key="lang";value=s.lang==1?"en-US":s.lang==2?"en-GB":"ru-RU";}
    else if(s.selected==1){s.handheld=!s.handheld;key="handheld";value=s.handheld?"1":"0";}
    else if(s.selected==2){s.aspect=(s.aspect+3+dir)%3;key="aspect";value=s.aspect==2?"2":s.aspect==1?"1":"0";}
    else if(s.selected==3){s.loading=!s.loading;key="stretch_loading";value=s.loading?"1":"0";}
    else if(s.selected==4){s.sharp=(s.sharp+4+dir)%4;key="sharpen";value=(const char*[]){"0","0.2","0.5","1.0"}[s.sharp];}
    else if(s.selected==5){s.fps=(s.fps+3+dir)%3;key="fps";value=(const char*[]){"30","60","120"}[s.fps];}
    else if(s.selected==6){s.deadzone=(s.deadzone+5+dir)%5;key="deadzone";value=(const char*[]){"0","5","10","15","20"}[s.deadzone];}
    else if(s.selected==7){s.gear=!s.gear;key="show_gear";value=s.gear?"1":"0";}
    else if(s.selected==8){s.alpha=(s.alpha+4+dir)%4;key="gear_alpha";value=(const char*[]){"25","50","75","100"}[s.alpha];}
    else if(s.selected==9){s.size=(s.size+4+dir)%4;key="gear_scale";value=(const char*[]){"75","100","125","150"}[s.size];}
    if(key)write_value(key,value);
}
void dm_linux_ui_event(const SDL_Event *e, int window_w, int window_h) {
    pthread_mutex_lock(&mu);
    if(e->type==SDL_KEYDOWN&&!e->key.repeat){
        SDL_Keycode k=e->key.keysym.sym;if(k==SDLK_ESCAPE){menu_open=!menu_open;}
        else if(menu_open&&k==SDLK_UP)s.selected=(s.selected+ROWS-1)%ROWS;
        else if(menu_open&&k==SDLK_DOWN)s.selected=(s.selected+1)%ROWS;
        else if(menu_open&&(k==SDLK_LEFT||k==SDLK_RIGHT))adjust(k==SDLK_LEFT?-1:1);
        else if(menu_open&&(k==SDLK_RETURN||k==SDLK_KP_ENTER)){if(s.selected==ROWS-1)menu_open=0;else adjust(1);}
    } else if(e->type==SDL_CONTROLLERBUTTONDOWN){
        if(e->cbutton.button==SDL_CONTROLLER_BUTTON_B&&menu_open)menu_open=0;
        else if(menu_open&&e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP)s.selected=(s.selected+ROWS-1)%ROWS;
        else if(menu_open&&e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)s.selected=(s.selected+1)%ROWS;
        else if(menu_open&&(e->cbutton.button==SDL_CONTROLLER_BUTTON_A||e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {if(s.selected==ROWS-1)menu_open=0;else adjust(1);}
        else if(menu_open&&e->cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)adjust(-1);
    } else if(e->type==SDL_MOUSEBUTTONDOWN||e->type==SDL_FINGERDOWN){
        float fx,fy;
        if(e->type==SDL_MOUSEBUTTONDOWN){fx=window_w?(float)e->button.x/window_w:0;fy=window_h?(float)e->button.y/window_h:0;}
        else {fx=e->tfinger.x;fy=e->tfinger.y;}
        if(menu_open){
            int pw=window_w<UI_W?window_w:UI_W,ph=window_h<UI_H?window_h:UI_H;
            int px=(window_w-pw)/2,py=(window_h-ph)/2;
            int tx=(int)(fx*window_w),ty=(int)(fy*window_h);
            int row_h=(ph-82)/ROWS;if(row_h>42)row_h=42;
            if(tx<px||tx>=px+pw||ty<py||ty>=py+ph){menu_open=0;}
            else {int row=(ty-py-68)/row_h;if(row>=0&&row<ROWS){s.selected=row;if(row==ROWS-1)menu_open=0;else adjust(tx<px+pw/2?-1:1);}}
        } else if(fx>0.82f&&fy<0.20f&&s.gear)menu_open=1;
    }
    pthread_mutex_unlock(&mu);
}

int dm_linux_ui_text_event(const SDL_Event *e,int window_w,int window_h){
    int consumed=0,action=0;
    pthread_mutex_lock(&mu);if(!text_keyboard){pthread_mutex_unlock(&mu);return 0;}
    if(e->type==SDL_CONTROLLERBUTTONDOWN){
        switch(e->cbutton.button){
        case SDL_CONTROLLER_BUTTON_A:case SDL_CONTROLLER_BUTTON_START:action=1;consumed=1;break;
        case SDL_CONTROLLER_BUTTON_B:action=2;consumed=1;break;
        case SDL_CONTROLLER_BUTTON_X:action=3;consumed=1;break;
        default:break;
        }
    }else if(e->type==SDL_KEYDOWN&&!e->key.repeat){
        switch(e->key.keysym.sym){
        case SDLK_RETURN:case SDLK_KP_ENTER:action=1;consumed=1;break;
        case SDLK_ESCAPE:action=2;consumed=1;break;
        default:break;
        }
    }else if(e->type==SDL_MOUSEBUTTONDOWN||e->type==SDL_FINGERDOWN){
        float fx,fy;if(e->type==SDL_MOUSEBUTTONDOWN){fx=window_w?(float)e->button.x/window_w:0;fy=window_h?(float)e->button.y/window_h:0;}else{fx=e->tfinger.x;fy=e->tfinger.y;}
        int pw=window_w<UI_W?window_w:UI_W,ph=window_h<UI_KH?window_h:UI_KH,px=(window_w-pw)/2,py=(window_h-ph)/2;
        int tx=(int)(fx*window_w)-px,ty=(int)(fy*window_h)-py;
        if(tx>=0&&tx<pw&&ty>=160&&ty<208){action=tx<pw/2?2:1;consumed=1;}
    }
    pthread_mutex_unlock(&mu);
    if(action==1||action==2)front_linux_text_finish(action==1);else if(action==3)front_linux_text_backspace();
    return consumed;
}

static void pixel(uint8_t *p,int stride,int x,int y,uint8_t r,uint8_t g,uint8_t b,int bgra){if(bgra){p[y*stride+x*4]=b;p[y*stride+x*4+1]=g;p[y*stride+x*4+2]=r;}else{p[y*stride+x*4]=r;p[y*stride+x*4+1]=g;p[y*stride+x*4+2]=b;}p[y*stride+x*4+3]=255;}
static void rect(uint8_t*p,int stride,int x,int y,int w,int h,uint8_t r,uint8_t g,uint8_t b,int bgra){for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++)pixel(p,stride,x+xx,y+yy,r,g,b,bgra);}
static const uint8_t *glyph(char c){
    static const uint8_t f[36][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
    if(c>='0'&&c<='9')return f[c-'0'];
    if(c>='A'&&c<='Z')return f[10+c-'A'];
    return NULL;
}
static void text(uint8_t*p,int stride,int x,int y,const char*str,int scale,int bgra,uint8_t r,uint8_t g,uint8_t b){for(;*str;str++,x+=6*scale){const uint8_t*q=glyph(*str);if(!q)continue;for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(q[yy]&(1<<(4-xx)))rect(p,stride,x+xx*scale,y+yy*scale,scale,scale,r,g,b,bgra);}}
static uint32_t next_codepoint(const unsigned char **s){
    const unsigned char *p=*s;uint32_t c=*p++;
    if(c<0x80){*s=p;return c;}
    unsigned need=(c&0xe0)==0xc0?1:(c&0xf0)==0xe0?2:(c&0xf8)==0xf0?3:0;
    if(!need){*s=p;return 0xfffd;}
    c&=need==1?0x1f:need==2?0x0f:0x07;
    for(unsigned i=0;i<need;i++){if((p[i]&0xc0)!=0x80){*s=p;return 0xfffd;}c=(c<<6)|(p[i]&0x3f);}
    *s=p+need;return c;
}
static void freetype_text(uint8_t*p,int stride,int x,int y,const char*str,int px,int bgra,uint8_t r,uint8_t g,uint8_t b){
    if(!ui_font){text(p,stride,x,y,str,2,bgra,r,g,b);return;}
    if(FT_Set_Pixel_Sizes(ui_font,0,(FT_UInt)px))return;
    int pen=x,baseline=y+(int)(ui_font->size->metrics.ascender>>6);
    const unsigned char *s=(const unsigned char*)str;
    while(*s){uint32_t cp=next_codepoint(&s);if(FT_Load_Char(ui_font,cp,FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL))continue;
        FT_Bitmap *bm=&ui_font->glyph->bitmap;int left=pen+ui_font->glyph->bitmap_left,top=baseline-ui_font->glyph->bitmap_top;
        for(unsigned yy=0;yy<bm->rows;yy++)for(unsigned xx=0;xx<bm->width;xx++){
            unsigned char a=bm->pixel_mode==FT_PIXEL_MODE_MONO?((bm->buffer[yy*bm->pitch+xx/8]&(0x80>>(xx&7)))?255:0):bm->buffer[yy*bm->pitch+xx];
            if(!a)continue;int dx=left+(int)xx,dy=top+(int)yy;uint8_t*q=p+(size_t)dy*stride+(size_t)dx*4;
            if(bgra){q[0]=(uint8_t)((q[0]*(255-a)+b*a)/255);q[1]=(uint8_t)((q[1]*(255-a)+g*a)/255);q[2]=(uint8_t)((q[2]*(255-a)+r*a)/255);}
            else{q[0]=(uint8_t)((q[0]*(255-a)+r*a)/255);q[1]=(uint8_t)((q[1]*(255-a)+g*a)/255);q[2]=(uint8_t)((q[2]*(255-a)+b*a)/255);}
        }
        pen+=(int)(ui_font->glyph->advance.x>>6);
    }
}
int dm_linux_ui_overlay(int sw,int sh,uint8_t*p,size_t cap,int bgra,int*x,int*y,int*w,int*h){
    pthread_mutex_lock(&mu);int keyboard=text_keyboard,open=menu_open,show=s.gear;int side=72*(s.size*25+75)/100;int pw=sw<UI_W?sw:UI_W,ph=sh<UI_H?sh:UI_H;
    if(keyboard){pw=sw<UI_W?sw:UI_W;ph=sh<UI_KH?sh:UI_KH;*w=pw;*h=ph;*x=(sw-pw)/2;*y=(sh-ph)/2;}
    else if(open){*w=pw;*h=ph;*x=(sw-pw)/2;*y=(sh-ph)/2;}else if(show){*w=side;*h=side;*x=sw-side-12;*y=12;}else{pthread_mutex_unlock(&mu);return 0;}
    if(*w<=0||*h<=0||(size_t)*w**h*4>cap){pthread_mutex_unlock(&mu);return 0;}int stride=*w*4;rect(p,stride,0,0,*w,*h,12,10,9,bgra);
    if(keyboard){
        char current[512];front_linux_text_snapshot(current,sizeof current);
        rect(p,stride,4,4,*w-8,*h-8,31,27,24,bgra);text(p,stride,24,18,text_prompt,2,bgra,224,201,153);
        text(p,stride,24,52,"STEAM+X OPENS KEYBOARD  ENTER TO CONFIRM",1,bgra,180,170,150);rect(p,stride,24,78,*w-48,52,12,10,9,bgra);freetype_text(p,stride,34,88,current,26,bgra,245,234,212);
        int bw=160,bh=40,gap=16,x0=(*w-(2*bw+gap))/2,yy=160;rect(p,stride,x0,yy,bw,bh,54,48,41,bgra);text(p,stride,x0+(bw-36)/2,yy+16,"CANCEL",1,bgra,235,226,211);rect(p,stride,x0+bw+gap,yy,bw,bh,91,61,31,bgra);text(p,stride,x0+bw+gap+(bw-12)/2,yy+16,"OK",1,bgra,235,226,211);
        pthread_mutex_unlock(&mu);return 1;
    }
    if(!open){int cx=side/2,cy=side/2,rad=side*40/100;for(int a=0;a<8;a++){double z=a*0.78539816339;int x1=cx+(int)(rad*.6*cos(z)),y1=cy+(int)(rad*.6*sin(z)),x2=cx+(int)(rad*cos(z)),y2=cy+(int)(rad*sin(z));int dx=abs(x2-x1),sx=x1<x2?1:-1,dy=-abs(y2-y1),sy=y1<y2?1:-1,err=dx+dy;for(;;){pixel(p,stride,x1,y1,221,193,130,bgra);if(x1==x2&&y1==y2)break;int e=2*err;if(e>=dy){err+=dy;x1+=sx;}if(e<=dx){err+=dx;y1+=sy;}}}for(int yy=-rad*55/100;yy<=rad*55/100;yy++)for(int xx=-rad*55/100;xx<=rad*55/100;xx++){int d=xx*xx+yy*yy;if(d<=rad*rad*36/100&&d>=rad*rad*16/100)pixel(p,stride,cx+xx,cy+yy,221,193,130,bgra);if(d<=rad*rad*4/100)pixel(p,stride,cx+xx,cy+yy,221,193,130,bgra);}pthread_mutex_unlock(&mu);return 1;}
    rect(p,stride,4,4,*w-8,*h-8,31,27,24,bgra);text(p,stride,20,18,"DIABLO III SETTINGS",2,bgra,224,201,153);text(p,stride,20,44,"LEFT RIGHT OR A TO CHANGE  ESC OR L3+R3 TO CLOSE",1,bgra,180,170,150);
    const char*labels[ROWS]={"GAME LANGUAGE","RENDER MODE","ASPECT RATIO","STRETCH LOADING","SHARPENING","FPS LIMIT","STICK DEADZONE","SHOW GEAR BUTTON","GEAR OPACITY","GEAR SIZE","VULKAN DRIVER","CLOSE SETTINGS"};
    char vals[ROWS][32];snprintf(vals[0],32,"%s",(const char*[]){"RUSSIAN","ENGLISH US","ENGLISH UK"}[s.lang]);snprintf(vals[1],32,"%s",s.handheld?"HANDHELD":"DOCKED");snprintf(vals[2],32,"%s",(const char*[]){"LETTERBOX","STRETCH","WIDESCREEN"}[s.aspect]);snprintf(vals[3],32,"%s",s.loading?"YES":"NO");snprintf(vals[4],32,"%s",(const char*[]){"OFF","LOW","MEDIUM","HIGH"}[s.sharp]);snprintf(vals[5],32,"%d FPS",(int[]){30,60,120}[s.fps]);snprintf(vals[6],32,"%d%%",s.deadzone*5);snprintf(vals[7],32,"%s",s.gear?"YES":"NO");snprintf(vals[8],32,"%d%%",(s.alpha+1)*25);snprintf(vals[9],32,"%d%%",75+s.size*25);snprintf(vals[10],32,"SYSTEM");vals[11][0]=0;
    int rows_y=68,row_h=(*h-82)/ROWS;if(row_h>42)row_h=42;for(int i=0;i<ROWS;i++){int yy=rows_y+i*row_h;if(yy+row_h>=*h-5)break;if(i==s.selected)rect(p,stride,12,yy,*w-24,row_h-2,91,61,31,bgra);text(p,stride,22,yy+row_h/2-7,labels[i],1,bgra,235,226,211);int vx=*w-230;if(i==10)text(p,stride,vx,yy+row_h/2-7,"ARMADAOS",1,bgra,178,170,153);else text(p,stride,vx,yy+row_h/2-7,vals[i],1,bgra,226,196,132);}
    pthread_mutex_unlock(&mu);return 1;
}
