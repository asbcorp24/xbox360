#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <input/input.h>
#include <xenos/xenos.h>
#include <console/console.h>
#include <usb/usbmain.h>
#include <xenon_sound/sound.h>

#include "fb2d.h"

typedef struct { float x,y,w,h; } rectf_t;
typedef struct { int room; float x,y,w,h; } room_platform_t;
typedef struct { int room; int x,y; int collected; } room_crystal_t;
typedef struct { float x,y,vx,vy; int on_ground; } player_t;

#include "generated_game.h"
#include "generated_assets.h"

static fb2d_t g_fb;
static int g_scale, g_off_x, g_off_y;
static int g_room = PLAYER_START_ROOM;
static unsigned g_frame_counter = 0;

static int iroundf_local(float v){ return (int)(v + (v>=0.0f?0.5f:-0.5f)); }

static int overlaps(float ax,float ay,float aw,float ah,float bx,float by,float bw,float bh){
    return ax < bx+bw && ax+aw > bx && ay < by+bh && ay+ah > by;
}

static void draw_rect_screen(int x,int y,int w,int h,uint32_t color){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>g_fb.width) w=g_fb.width-x;
    if(y+h>g_fb.height) h=g_fb.height-y;
    if(w<=0||h<=0) return;
    for(int yy=y;yy<y+h;++yy){
        uint32_t *row=&g_fb.backbuffer[yy*g_fb.width+x];
        for(int xx=0;xx<w;++xx) row[xx]=color;
    }
}

static void draw_rect(int x,int y,int w,int h,uint32_t color){
    draw_rect_screen(g_off_x+x*g_scale,g_off_y+y*g_scale,w*g_scale,h*g_scale,color);
}

static void draw_pixel(int x,int y,uint32_t color){ draw_rect(x,y,1,1,color); }

static void draw_sprite_frame(int frame,int x,int y,int flip){
    if(frame<0) frame=0;
    if(frame>=PLAYER_FRAME_COUNT) frame=PLAYER_FRAME_COUNT-1;
    int cols=PLAYER_SHEET_W/PLAYER_FRAME_W;
    int fx=(frame%cols)*PLAYER_FRAME_W;
    int fy=(frame/cols)*PLAYER_FRAME_H;
    for(int sy=0;sy<PLAYER_FRAME_H;++sy){
        for(int sx=0;sx<PLAYER_FRAME_W;++sx){
            int srcx=fx+(flip?(PLAYER_FRAME_W-1-sx):sx);
            int srcy=fy+sy;
            int i=(srcy*PLAYER_SHEET_W+srcx)*4;
            unsigned char a=player_sprite_rgba[i+3];
            if(a<16) continue;
            draw_pixel(x+sx,y+sy,fb2d_rgb(player_sprite_rgba[i],player_sprite_rgba[i+1],player_sprite_rgba[i+2]));
        }
    }
}

static void draw_crystal(int x,int y){
    uint32_t d=fb2d_rgb(52,107,123), m=fb2d_rgb(55,215,225), h=fb2d_rgb(194,255,250);
    draw_rect(x+2,y,2,1,h); draw_rect(x+1,y+1,4,1,m); draw_rect(x,y+2,6,3,m);
    draw_rect(x+1,y+5,4,1,d); draw_rect(x+2,y+6,2,1,d); draw_pixel(x+1,y+2,h);
}

static int total_collected(void){
    int n=0; for(int i=0;i<CRYSTAL_COUNT;++i) if(crystals[i].collected) ++n; return n;
}

static void play_pcm(const unsigned char *pcm,int len){
    if(len>0 && xenon_sound_get_free()>=len) xenon_sound_submit((void*)pcm,len);
}
static void play_jump_sound(void){
    if(SOUND_JUMP_LEN>0) play_jump_sound();
    else xenon_tone(620,35,XENON_TONE_AMPLITUDE_25);
}
static void play_collect_sound(void){
    if(SOUND_COLLECT_LEN>0) play_pcm(sound_collect_pcm,SOUND_COLLECT_LEN);
    else xenon_tone(1100,45,XENON_TONE_AMPLITUDE_25);
}

static void draw_exit(int open){
    int x=EXIT_X,y=EXIT_Y;
    uint32_t frame=fb2d_rgb(35,31,57), shut=fb2d_rgb(89,74,120), glow=fb2d_rgb(109,255,156);
    draw_rect(x-2,y-3,EXIT_W+4,EXIT_H+3,frame);
    draw_rect(x,y,EXIT_W,EXIT_H,open?glow:shut);
    if(open) draw_rect(x+3,y+3,EXIT_W-6,EXIT_H-6,fb2d_rgb(213,255,211));
}

static void draw_room(const player_t *p,int facing,int won){
    uint32_t sky=fb2d_rgb(19+g_room*4,19+g_room*3,38+g_room*7);
    fb2d_clear(&g_fb,sky);
    static const uint16_t stars[][2]={{12,18},{39,31},{71,14},{101,38},{139,20},{173,29},{205,12},{238,35},{270,18},{305,42},{24,74},{88,61},{154,56},{257,65},{294,82}};
    for(unsigned i=0;i<sizeof(stars)/sizeof(stars[0]);++i) draw_pixel(stars[i][0],stars[i][1],fb2d_rgb(93,91,128));
    draw_rect(0,145,320,15,fb2d_rgb(27,31,54));
    draw_rect(18+g_room*9,135,22,25,fb2d_rgb(27,31,54));
    draw_rect(79+g_room*7,140,31,20,fb2d_rgb(27,31,54));
    draw_rect(177-g_room*5,137,24,23,fb2d_rgb(27,31,54));

    for(int i=0;i<PLATFORM_COUNT;++i){
        if(platforms[i].room!=g_room) continue;
        draw_rect((int)platforms[i].x,(int)platforms[i].y,(int)platforms[i].w,(int)platforms[i].h,fb2d_rgb(55,52,73));
        draw_rect((int)platforms[i].x,(int)platforms[i].y,(int)platforms[i].w,2,fb2d_rgb(116,97,115));
    }
    for(int i=0;i<CRYSTAL_COUNT;++i)
        if(crystals[i].room==g_room && !crystals[i].collected) draw_crystal(crystals[i].x,crystals[i].y);

    if(g_room==EXIT_ROOM) draw_exit(total_collected()==CRYSTAL_COUNT);

    int frame=0;
    if(!p->on_ground) frame=3;
    else if(p->vx>0.15f || p->vx<-0.15f) frame=1+((g_frame_counter/8)&1);
    draw_sprite_frame(frame,iroundf_local(p->x),iroundf_local(p->y),facing<0);

    draw_rect(6,6,66,11,fb2d_rgb(9,11,24));
    for(int i=0;i<CRYSTAL_COUNT && i<8;++i){
        if(crystals[i].collected) draw_crystal(9+i*7,8);
        else draw_rect(11+i*7,11,2,2,fb2d_rgb(65,65,86));
    }
    for(int i=0;i<ROOM_COUNT;++i)
        draw_rect(286+i*8,8,5,5,i==g_room?fb2d_rgb(109,255,156):fb2d_rgb(65,65,86));

    if(won){
        draw_rect(88,58,144,42,fb2d_rgb(8,10,22));
        draw_rect(92,62,136,34,fb2d_rgb(47,39,72));
        for(int i=0;i<9;++i) draw_rect(102+i*14,75+(i&1)*4,8,8,(i&1)?fb2d_rgb(109,255,156):fb2d_rgb(55,215,225));
    }
}

static void reset_game(player_t *p,int *facing,int *won){
    p->x=PLAYER_START_X; p->y=PLAYER_START_Y; p->vx=0; p->vy=0; p->on_ground=0;
    *facing=1; *won=0; g_room=PLAYER_START_ROOM;
    for(int i=0;i<CRYSTAL_COUNT;++i) crystals[i].collected=0;
}

static void collide_room(player_t *p,float old_y){
    p->on_ground=0;
    for(int i=0;i<PLATFORM_COUNT;++i){
        const room_platform_t *r=&platforms[i];
        if(r->room!=g_room) continue;
        if(!overlaps(p->x,p->y,PLAYER_W,PLAYER_H,r->x,r->y,r->w,r->h)) continue;
        float old_bottom=old_y+PLAYER_H, new_bottom=p->y+PLAYER_H;
        if(p->vy>=0.0f && old_bottom<=r->y+1.0f && new_bottom>=r->y){
            p->y=r->y-PLAYER_H; p->vy=0; p->on_ground=1;
        } else if(p->vy<0.0f && old_y>=r->y+r->h-1.0f){
            p->y=r->y+r->h; p->vy=0;
        }
    }
}

static void update_player(player_t *p){
    p->x+=p->vx;
    if(p->x < -PLAYER_W){
        if(g_room>0){ --g_room; p->x=VW-PLAYER_W-2; }
        else p->x=0;
    } else if(p->x > VW){
        if(g_room<ROOM_COUNT-1){ ++g_room; p->x=2; }
        else p->x=VW-PLAYER_W;
    }

    float old_y=p->y;
    p->vy+=GAME_GRAVITY;
    if(p->vy>GAME_MAX_FALL_SPEED) p->vy=GAME_MAX_FALL_SPEED;
    p->y+=p->vy;
    collide_room(p,old_y);
    if(p->y>VH+30){ p->y=0; p->vy=0; }
}

int main(void){
    xenos_init(VIDEO_MODE_AUTO); console_init(); usb_init(); usb_do_poll(); xenon_sound_init();
    if(fb2d_init(&g_fb)!=0){ printf("fb2d init failed\n"); for(;;) usb_do_poll(); }
    g_scale=g_fb.width/VW; if(g_fb.height/VH<g_scale) g_scale=g_fb.height/VH; if(g_scale<1) g_scale=1;
    g_off_x=(g_fb.width-VW*g_scale)/2; g_off_y=(g_fb.height-VH*g_scale)/2;
    printf("%s | rooms=%d | %dx%d\n",GAME_NAME,ROOM_COUNT,VW,VH);

    player_t player; int facing,won; reset_game(&player,&facing,&won);
    struct controller_data_s prev; memset(&prev,0,sizeof(prev));

    for(;;){
        struct controller_data_s pad; memset(&pad,0,sizeof(pad));
        if(get_controller_data(&pad,0)){
            int left=pad.left || pad.s1_x<-9000, right=pad.right || pad.s1_x>9000;
            if(!won){
                if(left&&!right){ player.vx=-PLAYER_SPEED; facing=-1; }
                else if(right&&!left){ player.vx=PLAYER_SPEED; facing=1; }
                else { player.vx*=0.55f; if(player.vx>-0.08f&&player.vx<0.08f) player.vx=0; }
                if(pad.a&&!prev.a&&player.on_ground){
                    player.vy=-PLAYER_JUMP_SPEED; player.on_ground=0;
                    play_pcm(sound_jump_pcm,SOUND_JUMP_LEN);
                }
                update_player(&player);
                for(int i=0;i<CRYSTAL_COUNT;++i){
                    if(crystals[i].room==g_room && !crystals[i].collected &&
                       overlaps(player.x,player.y,PLAYER_W,PLAYER_H,crystals[i].x,crystals[i].y,6,7)){
                        crystals[i].collected=1; play_collect_sound();
                    }
                }
                if(g_room==EXIT_ROOM && total_collected()==CRYSTAL_COUNT &&
                   overlaps(player.x,player.y,PLAYER_W,PLAYER_H,EXIT_X-2,EXIT_Y-3,EXIT_W+4,EXIT_H+3)){
                    won=1; player.vx=player.vy=0;
                }
            }
            if(pad.start&&!prev.start) reset_game(&player,&facing,&won);
            if(won&&pad.a&&!prev.a) reset_game(&player,&facing,&won);
            prev=pad;
        }
        draw_room(&player,facing,won);
        fb2d_present(&g_fb);
        usb_do_poll();
        ++g_frame_counter;
    }
    return 0;
}
