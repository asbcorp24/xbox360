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

typedef struct { int room; float x,y,w,h; } room_platform_t;
typedef struct { int room; int x,y; int collected; } room_crystal_t;
typedef struct { int room,x,y,tile,solid; } tile_cell_t;
typedef struct { int room; float x,y; int w,h; float left,right,speed; int dir,alive,initial_alive,type,hp,max_hp,damage; } enemy_t;
typedef struct { int room,x,y,w,h; const char *message; int dialogue; } npc_t;
typedef struct { int room,x,y,w,h,target_room,target_x,target_y,once,fired; const char *action; } trigger_t;
typedef struct { int room,x,y,w,h,target_room,target_x,target_y,requires_all,locked,key_item; } door_t;
typedef struct { int room,x,y,w,h,target_room,target_x,target_y; } teleport_t;
typedef struct { int room,type,x,y,rate,life; float speed,spread,gravity; int r,g,b,timer; } particle_emitter_t;
typedef struct { int active,room,life,damage; float x,y,vx,vy; } projectile_t;
typedef struct { int active,room,life,max_life,size; float x,y,vx,vy,gravity; uint32_t color; } particle_t;
typedef struct { float x,y,vx,vy; int on_ground,hp,max_hp,invuln,attack_cd,attack_frames,fire_cd; } player_t;
typedef struct { const char *name; int kind,power,count,initial_count,ammo_item,fire_rate; float projectile_speed; int projectile_life; } item_t;
typedef struct { int room,x,y,item_index,qty,collected; } pickup_t;
typedef struct { const char *id,*text; int choice_start,choice_count; } dialog_node_t;
typedef struct { const char *text; int next_node; const char *set_flag; int set_flag_value,give_item,give_qty; } dialog_choice_t;
typedef struct { int w,h; const unsigned char *rgba; const char *name; } visual_asset_t;
typedef struct { int room,asset; float speed_x,speed_y; int repeat_x,repeat_y,offset_x,offset_y,foreground,order; } visual_layer_t;

typedef struct { const char *name; int value,initial_value; } script_value_t;
typedef struct { int cmd; const char *name; int value,room,index,x,y; const char *text; } script_action_t;
typedef struct { int event,room,target,once,fired,cond_type; const char *cond_name; int cond_op,cond_value,action_start,action_count; } script_event_t;

enum { EVT_ON_ENTER=1, EVT_ON_INTERACT=2, EVT_ON_COLLECT=3 };
enum { ACT_SET_FLAG=1, ACT_SET_VAR=2, ACT_ADD_VAR=3, ACT_OPEN_DOOR=4, ACT_SPAWN_ENEMY=5, ACT_CHANGE_ROOM=6, ACT_DIALOG=7, ACT_EMIT_PARTICLES=8 };

#include "generated_game.h"
#include "generated_assets.h"
#include "generated_scripts.h"
#include "generated_rpg.h"
#include "generated_visuals.h"

static fb2d_t g_fb;
static int g_scale,g_off_x,g_off_y;
static int g_room=PLAYER_START_ROOM;
static unsigned g_frame_counter=0;
static int g_npc_flash=0;
static int g_teleport_cooldown=0;
static int g_checkpoint_room=PLAYER_START_ROOM;
static float g_checkpoint_x=PLAYER_START_X,g_checkpoint_y=PLAYER_START_Y;
static unsigned char g_door_open[(DOOR_COUNT>0)?DOOR_COUNT:1];
static const char *g_dialog_text=0;
static int g_dialog_timer=0;
static int g_last_script_room=-999;
static int g_active_dialog=-1;
static int g_dialog_choice=0;
static float g_camera_x=0.0f,g_camera_y=0.0f;
static int g_world_mode=0;
static int g_shake_frames=0,g_shake_strength=0;
static int g_shake_x=0,g_shake_y=0;
#define MAX_PROJECTILES 32
#define MAX_PARTICLES 128
static projectile_t g_projectiles[MAX_PROJECTILES];
static particle_t g_particles[MAX_PARTICLES];

static int iroundf_local(float v){ return (int)(v+(v>=0.0f?0.5f:-0.5f)); }
static int overlaps(float ax,float ay,float aw,float ah,float bx,float by,float bw,float bh){
    return ax<bx+bw && ax+aw>bx && ay<by+bh && ay+ah>by;
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
static void draw_rect(int x,int y,int w,int h,uint32_t c){
    int ox=0,oy=0;
    if(g_world_mode){ ox=-(int)g_camera_x+g_shake_x; oy=-(int)g_camera_y+g_shake_y; }
    draw_rect_screen(g_off_x+(x+ox)*g_scale,g_off_y+(y+oy)*g_scale,w*g_scale,h*g_scale,c);
}
static void draw_pixel(int x,int y,uint32_t c){ draw_rect(x,y,1,1,c); }
static void draw_visual_asset(const visual_asset_t *a,int dx,int dy){
    if(!a||!a->rgba)return;
    for(int y=0;y<a->h;++y) for(int x=0;x<a->w;++x){
        int i=(y*a->w+x)*4; if(a->rgba[i+3]<16)continue;
        draw_pixel(dx+x,dy+y,fb2d_rgb(a->rgba[i],a->rgba[i+1],a->rgba[i+2]));
    }
}
static int imod(int a,int m){ int r=a%m; return r<0?r+m:r; }
static void draw_visual_layers(int foreground){
    int old=g_world_mode; g_world_mode=0;
    for(int i=0;i<VISUAL_LAYER_COUNT;++i){
        const visual_layer_t *l=&visual_layers[i];
        if(l->room!=g_room||l->foreground!=foreground)continue;
        const visual_asset_t *a=&visual_assets[l->asset];
        int base_x=l->offset_x-(int)(g_camera_x*l->speed_x)+(foreground?g_shake_x:0);
        int base_y=l->offset_y-(int)(g_camera_y*l->speed_y)+(foreground?g_shake_y:0);
        int start_x=l->repeat_x? -imod(-base_x,a->w):base_x;
        int start_y=l->repeat_y? -imod(-base_y,a->h):base_y;
        int end_x=l->repeat_x?VW:start_x+1;
        int end_y=l->repeat_y?VH:start_y+1;
        for(int y=start_y;y<end_y;y+=a->h){
            for(int x=start_x;x<end_x;x+=a->w) draw_visual_asset(a,x,y);
        }
    }
    g_world_mode=old;
}
static void camera_shake(int strength,int frames){
    if(strength>g_shake_strength)g_shake_strength=strength;
    if(frames>g_shake_frames)g_shake_frames=frames;
}
static void update_camera(const player_t *p){
    int roomw=room_world_width[g_room];
    float target=p->x-(VW/2.0f);
    float dz=(float)room_camera_deadzone[g_room];
    float center=g_camera_x+VW/2.0f;
    if(p->x<center-dz) target=p->x-(VW/2.0f-dz);
    else if(p->x>center+dz) target=p->x-(VW/2.0f+dz);
    else target=g_camera_x;
    float maxx=(float)(roomw-VW); if(maxx<0)maxx=0;
    if(target<0)target=0; if(target>maxx)target=maxx;
    g_camera_x+=(target-g_camera_x)*room_camera_smoothing[g_room];
    if(g_shake_frames>0){
        int phase=(int)g_frame_counter;
        g_shake_x=(phase&1)?g_shake_strength:-g_shake_strength;
        g_shake_y=(phase&2)?(g_shake_strength/2):-(g_shake_strength/2);
        --g_shake_frames;
        if(g_shake_frames<=0){g_shake_x=g_shake_y=0;g_shake_strength=0;}
    }
}

static unsigned char glyph3x5(char ch,int row){
    if(ch>='a'&&ch<='z') ch=(char)(ch-'a'+'A');
    switch(ch){
        case 'A':{static const unsigned char g[5]={2,5,7,5,5};return g[row];}
        case 'B':{static const unsigned char g[5]={6,5,6,5,6};return g[row];}
        case 'C':{static const unsigned char g[5]={3,4,4,4,3};return g[row];}
        case 'D':{static const unsigned char g[5]={6,5,5,5,6};return g[row];}
        case 'E':{static const unsigned char g[5]={7,4,6,4,7};return g[row];}
        case 'F':{static const unsigned char g[5]={7,4,6,4,4};return g[row];}
        case 'G':{static const unsigned char g[5]={3,4,5,5,3};return g[row];}
        case 'H':{static const unsigned char g[5]={5,5,7,5,5};return g[row];}
        case 'I':{static const unsigned char g[5]={7,2,2,2,7};return g[row];}
        case 'J':{static const unsigned char g[5]={1,1,1,5,2};return g[row];}
        case 'K':{static const unsigned char g[5]={5,5,6,5,5};return g[row];}
        case 'L':{static const unsigned char g[5]={4,4,4,4,7};return g[row];}
        case 'M':{static const unsigned char g[5]={5,7,7,5,5};return g[row];}
        case 'N':{static const unsigned char g[5]={5,7,7,7,5};return g[row];}
        case 'O':{static const unsigned char g[5]={2,5,5,5,2};return g[row];}
        case 'P':{static const unsigned char g[5]={6,5,6,4,4};return g[row];}
        case 'Q':{static const unsigned char g[5]={2,5,5,3,1};return g[row];}
        case 'R':{static const unsigned char g[5]={6,5,6,5,5};return g[row];}
        case 'S':{static const unsigned char g[5]={3,4,2,1,6};return g[row];}
        case 'T':{static const unsigned char g[5]={7,2,2,2,2};return g[row];}
        case 'U':{static const unsigned char g[5]={5,5,5,5,7};return g[row];}
        case 'V':{static const unsigned char g[5]={5,5,5,5,2};return g[row];}
        case 'W':{static const unsigned char g[5]={5,5,7,7,5};return g[row];}
        case 'X':{static const unsigned char g[5]={5,5,2,5,5};return g[row];}
        case 'Y':{static const unsigned char g[5]={5,5,2,2,2};return g[row];}
        case 'Z':{static const unsigned char g[5]={7,1,2,4,7};return g[row];}
        case '0':{static const unsigned char g[5]={7,5,5,5,7};return g[row];}
        case '1':{static const unsigned char g[5]={2,6,2,2,7};return g[row];}
        case '2':{static const unsigned char g[5]={6,1,7,4,7};return g[row];}
        case '3':{static const unsigned char g[5]={6,1,3,1,6};return g[row];}
        case '4':{static const unsigned char g[5]={5,5,7,1,1};return g[row];}
        case '5':{static const unsigned char g[5]={7,4,6,1,6};return g[row];}
        case '6':{static const unsigned char g[5]={3,4,7,5,7};return g[row];}
        case '7':{static const unsigned char g[5]={7,1,2,2,2};return g[row];}
        case '8':{static const unsigned char g[5]={7,5,7,5,7};return g[row];}
        case '9':{static const unsigned char g[5]={7,5,7,1,6};return g[row];}
        case '.':{static const unsigned char g[5]={0,0,0,0,2};return g[row];}
        case '!':{static const unsigned char g[5]={2,2,2,0,2};return g[row];}
        case '?':{static const unsigned char g[5]={6,1,2,0,2};return g[row];}
        case ':':{static const unsigned char g[5]={0,2,0,2,0};return g[row];}
        case '-':{static const unsigned char g[5]={0,0,7,0,0};return g[row];}
        default:return 0;
    }
}
static void draw_text3x5(int x,int y,const char *s,uint32_t color,int max_chars){
    int cx=x,cy=y,n=0;
    while(*s&&n<max_chars){
        char ch=*s++;
        if(ch=='\n'||cx>x+196){cx=x;cy+=7;if(ch=='\n')continue;}
        if(ch!=' '){
            for(int r=0;r<5;++r){unsigned char bits=glyph3x5(ch,r);for(int col=0;col<3;++col)if(bits&(1<<(2-col)))draw_pixel(cx+col,cy+r,color);}
        }
        cx+=4;++n;
    }
}

static void draw_rgba_frame(const unsigned char *rgba,int sheet_w,int sheet_h,int frame_w,int frame_h,int frame_count,int frame,int x,int y,int flip){
    (void)sheet_h;
    if(frame<0) frame=0;
    if(frame>=frame_count) frame=frame_count-1;
    int cols=sheet_w/frame_w;
    if(cols<1) cols=1;
    int fx=(frame%cols)*frame_w, fy=(frame/cols)*frame_h;
    for(int sy=0;sy<frame_h;++sy) for(int sx=0;sx<frame_w;++sx){
        int srcx=fx+(flip?(frame_w-1-sx):sx), srcy=fy+sy;
        int i=(srcy*sheet_w+srcx)*4;
        if(rgba[i+3]<16) continue;
        draw_pixel(x+sx,y+sy,fb2d_rgb(rgba[i],rgba[i+1],rgba[i+2]));
    }
}
static void draw_sprite_frame(int frame,int x,int y,int flip){
    draw_rgba_frame(player_sprite_rgba,PLAYER_SHEET_W,PLAYER_SHEET_H,PLAYER_FRAME_W,PLAYER_FRAME_H,PLAYER_FRAME_COUNT,frame,x,y,flip);
}

static void play_pcm(const unsigned char *p,int len){ if(len>0 && xenon_sound_get_free()>=len) xenon_sound_submit((void*)p,len); }
static void snd_jump(void){ if(SOUND_JUMP_LEN) play_pcm(sound_jump_pcm,SOUND_JUMP_LEN); else xenon_tone(620,35,XENON_TONE_AMPLITUDE_25); }
static void snd_collect(void){ if(SOUND_COLLECT_LEN) play_pcm(sound_collect_pcm,SOUND_COLLECT_LEN); else xenon_tone(1100,45,XENON_TONE_AMPLITUDE_25); }
static void snd_hit(void){ if(SOUND_HIT_LEN) play_pcm(sound_hit_pcm,SOUND_HIT_LEN); else xenon_tone(180,90,XENON_TONE_AMPLITUDE_50); }
static void snd_door(void){ if(SOUND_DOOR_LEN) play_pcm(sound_door_pcm,SOUND_DOOR_LEN); else xenon_tone(760,55,XENON_TONE_AMPLITUDE_25); }
static void snd_npc(void){ if(SOUND_NPC_LEN) play_pcm(sound_npc_pcm,SOUND_NPC_LEN); else xenon_tone(900,35,XENON_TONE_AMPLITUDE_25); }

static void draw_crystal(int x,int y){
    uint32_t d=fb2d_rgb(52,107,123),m=fb2d_rgb(55,215,225),h=fb2d_rgb(194,255,250);
    draw_rect(x+2,y,2,1,h); draw_rect(x+1,y+1,4,1,m); draw_rect(x,y+2,6,3,m);
    draw_rect(x+1,y+5,4,1,d); draw_rect(x+2,y+6,2,1,d); draw_pixel(x+1,y+2,h);
}
static int total_collected(void){ int n=0; for(int i=0;i<CRYSTAL_COUNT;++i) if(crystals[i].collected) ++n; return n; }

static script_value_t *find_value(script_value_t *a,int count,const char *name){
    for(int i=0;i<count;++i) if(!strcmp(a[i].name,name)) return &a[i];
    return 0;
}
static int compare_value(int lhs,int op,int rhs){
    if(op==0)return lhs==rhs; if(op==1)return lhs!=rhs; if(op==2)return lhs>rhs;
    if(op==3)return lhs<rhs; if(op==4)return lhs>=rhs; if(op==5)return lhs<=rhs;
    return 0;
}
static int event_condition_ok(const script_event_t *e){
    if(e->cond_type==0)return 1;
    script_value_t *v=e->cond_type==1?find_value(script_flags,SCRIPT_FLAG_COUNT,e->cond_name):find_value(script_vars,SCRIPT_VAR_COUNT,e->cond_name);
    return v?compare_value(v->value,e->cond_op,e->cond_value):0;
}
static int find_room_enemy(int room,int local_index){
    int n=0; for(int i=0;i<ENEMY_COUNT;++i) if(enemies[i].room==room){ if(n==local_index)return i; ++n; } return -1;
}
static int find_room_door(int room,int local_index){
    int n=0; for(int i=0;i<DOOR_COUNT;++i) if(doors[i].room==room){ if(n==local_index)return i; ++n; } return -1;
}
static int local_crystal_index(int global_index){
    int n=0; for(int i=0;i<global_index;++i) if(crystals[i].room==crystals[global_index].room) ++n; return n;
}
static int local_npc_index(int global_index){
    int n=0; for(int i=0;i<global_index;++i) if(npcs[i].room==npcs[global_index].room) ++n; return n;
}
static void script_dialog(const char *text){
    g_dialog_text=text; g_dialog_timer=180; printf("DIALOG: %s\n",text);
}
static int inventory_count(int idx){ return (idx>=0&&idx<RPG_ITEM_COUNT)?items[idx].count:0; }
static void inventory_add(int idx,int qty){ if(idx>=0&&idx<RPG_ITEM_COUNT) items[idx].count+=qty; }
static int weapon_damage(void){
    int best=PLAYER_BASE_DAMAGE;
    for(int i=0;i<RPG_ITEM_COUNT;++i) if(items[i].kind==3&&items[i].count>0&&items[i].power>best) best=items[i].power;
    return best;
}
static int active_gun_index(void){
    int best=-1;
    for(int i=0;i<RPG_ITEM_COUNT;++i)
        if(items[i].kind==5&&items[i].count>0&&(best<0||items[i].power>items[best].power)) best=i;
    return best;
}
static int point_hits_solid(float x,float y){
    for(int i=0;i<PLATFORM_COUNT;++i){
        const room_platform_t *p=&platforms[i];
        if(p->room==g_room&&x>=p->x&&x<p->x+p->w&&y>=p->y&&y<p->y+p->h)return 1;
    }
    for(int i=0;i<TILE_COUNT;++i){
        const tile_cell_t *t=&tiles[i];
        if(t->room!=g_room||!t->solid)continue;
        float tx=(float)(t->x*TILE_SIZE),ty=(float)(t->y*TILE_SIZE);
        if(x>=tx&&x<tx+TILE_SIZE&&y>=ty&&y<ty+TILE_SIZE)return 1;
    }
    return 0;
}
static float frand_unit(void){ return (float)(rand()&1023)/1023.0f; }
static void spawn_particle(float x,float y,float vx,float vy,float gravity,int life,int size,uint32_t color){
    for(int i=0;i<MAX_PARTICLES;++i) if(!g_particles[i].active){
        particle_t *p=&g_particles[i]; p->active=1;p->room=g_room;p->x=x;p->y=y;p->vx=vx;p->vy=vy;p->gravity=gravity;
        p->life=p->max_life=life;p->size=size;p->color=color;return;
    }
}
static void particle_burst(float x,float y,int type,int count,int r,int g,int b){
    uint32_t color=fb2d_rgb((uint8_t)r,(uint8_t)g,(uint8_t)b);
    for(int i=0;i<count;++i){
        float a=frand_unit()*2.0f-1.0f;
        float vx=a*(type==2?1.8f:.9f);
        float vy=-(.3f+frand_unit()*(type==4?1.5f:1.0f));
        float grav=(type==1)?-0.005f:(type==2?0.08f:0.035f);
        int life=12+(int)(frand_unit()*22.0f);
        spawn_particle(x,y,vx,vy,grav,life,(type==1)?2:1,color);
    }
}
static void update_emitters(void){
    for(int i=0;i<PARTICLE_EMITTER_COUNT;++i){
        particle_emitter_t *e=&particle_emitters[i]; if(e->room!=g_room)continue;
        if(++e->timer<e->rate)continue; e->timer=0;
        float vx=(frand_unit()*2.0f-1.0f)*e->spread*e->speed;
        float vy=-(.2f+frand_unit()*e->speed);
        if(e->type==2){vx*=1.8f;vy*=1.6f;}
        if(e->type==3)vy=-.15f-frand_unit()*.25f;
        if(e->type==4)vy=-.4f-frand_unit()*e->speed;
        spawn_particle((float)e->x,(float)e->y,vx,vy,e->gravity,e->life,e->type==1?2:1,fb2d_rgb((uint8_t)e->r,(uint8_t)e->g,(uint8_t)e->b));
    }
}
static void update_particles(void){
    update_emitters();
    for(int i=0;i<MAX_PARTICLES;++i){
        particle_t *p=&g_particles[i]; if(!p->active)continue;
        if(p->room!=g_room){p->active=0;continue;}
        p->x+=p->vx;p->y+=p->vy;p->vy+=p->gravity;
        if(--p->life<=0)p->active=0;
    }
}
static void draw_particles(void){
    for(int i=0;i<MAX_PARTICLES;++i){
        particle_t *p=&g_particles[i]; if(!p->active||p->room!=g_room)continue;
        draw_rect(iroundf_local(p->x),iroundf_local(p->y),p->size,p->size,p->color);
    }
}
static void update_projectiles(void){
    for(int i=0;i<MAX_PROJECTILES;++i){
        projectile_t *p=&g_projectiles[i]; if(!p->active)continue;
        if(p->room!=g_room){p->active=0;continue;}
        p->x+=p->vx;p->y+=p->vy;
        if(--p->life<=0||p->x<0||p->x>room_world_width[g_room]||p->y<0||p->y>VH||point_hits_solid(p->x,p->y)){
            particle_burst(p->x,p->y,2,4,240,190,80);p->active=0;continue;
        }
        for(int j=0;j<ENEMY_COUNT;++j){
            enemy_t *e=&enemies[j];if(e->room!=g_room||!e->alive)continue;
            if(overlaps(p->x,p->y,2,2,e->x,e->y,e->w,e->h)){
                e->hp-=p->damage;particle_burst(p->x,p->y,2,7,255,170,70);camera_shake(1,4);p->active=0;
                if(e->hp<=0){e->alive=0;particle_burst(e->x+e->w/2,e->y+e->h/2,3,14,190,70,70);}
                else e->dir=-e->dir;
                break;
            }
        }
    }
}
static void draw_projectiles(void){
    for(int i=0;i<MAX_PROJECTILES;++i){
        projectile_t *p=&g_projectiles[i];if(p->active&&p->room==g_room)draw_rect(iroundf_local(p->x),iroundf_local(p->y),3,1,fb2d_rgb(255,225,120));
    }
}
static void fire_gun(player_t *p,int facing){
    if(p->fire_cd>0)return;
    int gi=active_gun_index();if(gi<0)return;
    item_t *gun=&items[gi];
    if(gun->ammo_item<0||gun->ammo_item>=RPG_ITEM_COUNT||items[gun->ammo_item].count<=0){xenon_tone(120,35,XENON_TONE_AMPLITUDE_25);p->fire_cd=8;return;}
    for(int i=0;i<MAX_PROJECTILES;++i) if(!g_projectiles[i].active){
        projectile_t *q=&g_projectiles[i];q->active=1;q->room=g_room;q->damage=gun->power;q->life=gun->projectile_life;
        q->x=p->x+(facing>0?PLAYER_W+2:-3);q->y=p->y+PLAYER_H/2;q->vx=gun->projectile_speed*(facing>0?1.0f:-1.0f);q->vy=0;
        --items[gun->ammo_item].count;p->fire_cd=gun->fire_rate>0?gun->fire_rate:1;
        particle_burst(q->x,q->y,4,6,255,210,90);camera_shake(1,3);xenon_tone(1050,18,XENON_TONE_AMPLITUDE_25);return;
    }
}
static void use_consumable(player_t *p){
    for(int i=0;i<RPG_ITEM_COUNT;++i){
        if(items[i].kind==2&&items[i].count>0){
            p->hp+=items[i].power; if(p->hp>p->max_hp)p->hp=p->max_hp;
            --items[i].count; xenon_tone(840,55,XENON_TONE_AMPLITUDE_25); return;
        }
    }
}
static void start_dialog(int node){
    if(node<0||node>=DIALOG_NODE_COUNT)return;
    g_active_dialog=node; g_dialog_choice=0; g_dialog_text=dialog_nodes[node].text; g_dialog_timer=999999;
}
static void choose_dialog(player_t *p){
    if(g_active_dialog<0||g_active_dialog>=DIALOG_NODE_COUNT)return;
    const dialog_node_t *n=&dialog_nodes[g_active_dialog];
    if(n->choice_count<=0){g_active_dialog=-1;g_dialog_timer=0;return;}
    int ci=n->choice_start+g_dialog_choice;
    const dialog_choice_t *ch=&dialog_choices[ci];
    if(ch->set_flag&&ch->set_flag[0]){
        script_value_t *v=find_value(script_flags,SCRIPT_FLAG_COUNT,ch->set_flag);
        if(v)v->value=ch->set_flag_value;
    }
    if(ch->give_item>=0) inventory_add(ch->give_item,ch->give_qty);
    if(ch->next_node>=0) start_dialog(ch->next_node);
    else { g_active_dialog=-1; g_dialog_timer=0; g_dialog_text=0; }
}
static void execute_action(const script_action_t *a,player_t *p){
    script_value_t *v; int i;
    switch(a->cmd){
        case ACT_SET_FLAG:
            v=find_value(script_flags,SCRIPT_FLAG_COUNT,a->name); if(v)v->value=a->value; break;
        case ACT_SET_VAR:
            v=find_value(script_vars,SCRIPT_VAR_COUNT,a->name); if(v)v->value=a->value; break;
        case ACT_ADD_VAR:
            v=find_value(script_vars,SCRIPT_VAR_COUNT,a->name); if(v)v->value+=a->value; break;
        case ACT_OPEN_DOOR:
            i=find_room_door(a->room,a->index); if(i>=0)g_door_open[i]=1; break;
        case ACT_SPAWN_ENEMY:
            i=find_room_enemy(a->room,a->index); if(i>=0)enemies[i].alive=1; break;
        case ACT_CHANGE_ROOM:
            if(a->room>=0&&a->room<ROOM_COUNT){g_room=a->room;p->x=a->x;p->y=a->y;p->vx=p->vy=0;g_teleport_cooldown=20;} break;
        case ACT_DIALOG:
            script_dialog(a->text); break;
        case ACT_EMIT_PARTICLES:{
            int type=!strcmp(a->name,"spark")?2:(!strcmp(a->name,"dust")?3:(!strcmp(a->name,"fire")?4:1));
            int r=type==2?255:(type==4?255:180), g=type==2?190:(type==4?100:150), b=type==2?80:(type==4?40:140);
            particle_burst((float)a->x,(float)a->y,type,a->value>0?a->value:8,r,g,b); break;
        }
    }
}
static void run_script_event(int event,int room,int target,player_t *p){
    for(int i=0;i<SCRIPT_EVENT_COUNT;++i){
        script_event_t *e=&script_events[i];
        if(e->event!=event)continue;
        if(e->room>=0&&e->room!=room)continue;
        if(e->target>=0&&e->target!=target)continue;
        if(e->once&&e->fired)continue;
        if(!event_condition_ok(e))continue;
        e->fired=1;
        for(int j=0;j<e->action_count;++j) execute_action(&script_actions[e->action_start+j],p);
    }
}
static void process_room_enter(player_t *p){
    for(int guard=0;guard<4&&g_last_script_room!=g_room;++guard){
        int r=g_room; g_last_script_room=r; run_script_event(EVT_ON_ENTER,r,-1,p);
    }
}

static void draw_tile(const tile_cell_t *t){
    int x=t->x*TILE_SIZE,y=t->y*TILE_SIZE;
    uint32_t a=t->tile==2?fb2d_rgb(72,58,95):fb2d_rgb(64,72,91);
    uint32_t b=t->solid?fb2d_rgb(132,117,128):fb2d_rgb(91,72,118);
    draw_rect(x,y,TILE_SIZE,TILE_SIZE,a);
    draw_rect(x,y,TILE_SIZE,2,b);
    draw_rect(x+2,y+4,TILE_SIZE-4,2,b);
}

static void draw_enemy(const enemy_t *e){
    int frame=(g_frame_counter/8)%4;
    const unsigned char *rgba=enemy_walker_rgba;
    int sw=WALKER_SHEET_W,sh=WALKER_SHEET_H,fw=WALKER_FRAME_W,fh=WALKER_FRAME_H,fc=WALKER_FRAME_COUNT;
    if(e->type==2){rgba=enemy_charger_rgba;sw=CHARGER_SHEET_W;sh=CHARGER_SHEET_H;fw=CHARGER_FRAME_W;fh=CHARGER_FRAME_H;fc=CHARGER_FRAME_COUNT;}
    else if(e->type==3){rgba=enemy_tank_rgba;sw=TANK_SHEET_W;sh=TANK_SHEET_H;fw=TANK_FRAME_W;fh=TANK_FRAME_H;fc=TANK_FRAME_COUNT;}
    int x=iroundf_local(e->x)+(e->w-fw)/2;
    int y=iroundf_local(e->y)+e->h-fh;
    draw_rgba_frame(rgba,sw,sh,fw,fh,fc,frame,x,y,e->dir<0);
}

static void draw_npc(const npc_t *n){
    int x=n->x+(n->w-NPC_FRAME_W)/2;
    int y=n->y+n->h-NPC_FRAME_H;
    draw_rgba_frame(npc_sprite_rgba,NPC_SHEET_W,NPC_SHEET_H,NPC_FRAME_W,NPC_FRAME_H,NPC_FRAME_COUNT,0,x,y,0);
}

static void draw_door(const door_t *d,int open){
    uint32_t c=open?fb2d_rgb(83,205,126):fb2d_rgb(97,72,116);
    draw_rect(d->x-2,d->y-2,d->w+4,d->h+2,fb2d_rgb(31,28,46));
    draw_rect(d->x,d->y,d->w,d->h,c);
    draw_pixel(d->x+d->w-3,d->y+d->h/2,fb2d_rgb(236,210,128));
}

static void draw_teleport(const teleport_t *t){
    draw_rect(t->x,t->y,t->w,t->h,fb2d_rgb(58,44,91));
    draw_rect(t->x+2,t->y+2,t->w-4,t->h-4,fb2d_rgb(139,86,220));
    if((g_frame_counter/8)&1) draw_rect(t->x+4,t->y+4,t->w-8,t->h-8,fb2d_rgb(203,160,255));
}

static void draw_exit(int open){
    uint32_t c=open?fb2d_rgb(109,255,156):fb2d_rgb(89,74,120);
    draw_rect(EXIT_X-2,EXIT_Y-3,EXIT_W+4,EXIT_H+3,fb2d_rgb(35,31,57));
    draw_rect(EXIT_X,EXIT_Y,EXIT_W,EXIT_H,c);
}

static void draw_room(const player_t *p,int facing,int won){
    fb2d_clear(&g_fb,fb2d_rgb(12,14,28));
    draw_visual_layers(0);
    g_world_mode=1;

    for(int i=0;i<TILE_COUNT;++i) if(tiles[i].room==g_room) draw_tile(&tiles[i]);
    for(int i=0;i<PLATFORM_COUNT;++i) if(platforms[i].room==g_room){
        draw_rect((int)platforms[i].x,(int)platforms[i].y,(int)platforms[i].w,(int)platforms[i].h,fb2d_rgb(55,52,73));
        draw_rect((int)platforms[i].x,(int)platforms[i].y,(int)platforms[i].w,2,fb2d_rgb(116,97,115));
    }
    for(int i=0;i<CRYSTAL_COUNT;++i) if(crystals[i].room==g_room&&!crystals[i].collected) draw_crystal(crystals[i].x,crystals[i].y);
    for(int i=0;i<ENEMY_COUNT;++i) if(enemies[i].room==g_room&&enemies[i].alive) draw_enemy(&enemies[i]);
    for(int i=0;i<NPC_COUNT;++i) if(npcs[i].room==g_room) draw_npc(&npcs[i]);
    for(int i=0;i<DOOR_COUNT;++i) if(doors[i].room==g_room) draw_door(&doors[i],g_door_open[i]||(!doors[i].locked&&(!doors[i].requires_all||total_collected()==CRYSTAL_COUNT)));
    for(int i=0;i<TELEPORT_COUNT;++i) if(teleports[i].room==g_room) draw_teleport(&teleports[i]);
    if(g_room==EXIT_ROOM) draw_exit(total_collected()==CRYSTAL_COUNT);

    int frame=0;
    if(p->attack_frames>0 && PLAYER_FRAME_COUNT>4) frame=4;
    else if(p->fire_cd>0 && p->fire_cd<=3 && PLAYER_FRAME_COUNT>5) frame=5;
    else if(!p->on_ground) frame=3;
    else if(p->vx>.15f||p->vx<-.15f) frame=1+((g_frame_counter/8)&1);
    int player_dx=iroundf_local(p->x)+(PLAYER_W-PLAYER_FRAME_W)/2;
    int player_dy=iroundf_local(p->y)+PLAYER_H-PLAYER_FRAME_H;
    draw_sprite_frame(frame,player_dx,player_dy,facing<0);

    for(int i=0;i<RPG_PICKUP_COUNT;++i) if(!pickups[i].collected&&pickups[i].room==g_room){draw_rect(pickups[i].x,pickups[i].y,6,6,fb2d_rgb(230,190,70));}
    draw_projectiles();
    draw_particles();
    if(p->attack_frames>0){int ax=facing>0?iroundf_local(p->x)+PLAYER_W:iroundf_local(p->x)-10;draw_rect(ax,iroundf_local(p->y)+3,10,3,fb2d_rgb(245,230,180));}

    g_world_mode=0;
    draw_visual_layers(1);

    draw_rect(6,6,72,11,fb2d_rgb(9,11,24));
    for(int i=0;i<CRYSTAL_COUNT&&i<8;++i) if(crystals[i].collected) draw_crystal(9+i*7,8); else draw_rect(11+i*7,11,2,2,fb2d_rgb(65,65,86));
    for(int i=0;i<ROOM_COUNT;++i) draw_rect(286+i*8,8,5,5,i==g_room?fb2d_rgb(109,255,156):fb2d_rgb(65,65,86));
    draw_rect(6,20,44,6,fb2d_rgb(40,28,40)); draw_rect(8,22,(p->hp*40)/(p->max_hp?p->max_hp:1),2,fb2d_rgb(220,72,72));
    int gun=active_gun_index(); if(gun>=0&&items[gun].ammo_item>=0){char ammo[16];snprintf(ammo,sizeof(ammo),"AMMO %d",items[items[gun].ammo_item].count);draw_text3x5(56,20,ammo,fb2d_rgb(245,220,130),15);}

    if(g_npc_flash>0||g_dialog_timer>0||g_active_dialog>=0){
        draw_rect(40,18,240,54,fb2d_rgb(245,240,220)); draw_rect(42,20,236,50,fb2d_rgb(252,249,235));
        if(g_dialog_text)draw_text3x5(48,25,g_dialog_text,fb2d_rgb(48,44,62),110);
        if(g_active_dialog>=0){
            const dialog_node_t *dn=&dialog_nodes[g_active_dialog];
            for(int k=0;k<dn->choice_count&&k<3;++k){
                const dialog_choice_t *ch=&dialog_choices[dn->choice_start+k];
                if(k==g_dialog_choice) draw_rect(46,48+k*7,3,5,fb2d_rgb(190,60,72));
                draw_text3x5(52,48+k*7,ch->text,fb2d_rgb(48,44,62),46);
            }
        }
    }
    if(won){ draw_rect(88,58,144,42,fb2d_rgb(8,10,22)); draw_rect(92,62,136,34,fb2d_rgb(47,39,72)); }
}

static void respawn(player_t *p){
    g_room=g_checkpoint_room; p->x=g_checkpoint_x; p->y=g_checkpoint_y; p->vx=p->vy=0; p->on_ground=0; g_teleport_cooldown=20;
    g_camera_x=p->x-(VW/2.0f); if(g_camera_x<0)g_camera_x=0;
}
static void reset_game(player_t *p,int *facing,int *won){
    *facing=1; *won=0; g_checkpoint_room=PLAYER_START_ROOM; g_checkpoint_x=PLAYER_START_X; g_checkpoint_y=PLAYER_START_Y; respawn(p);
    for(int i=0;i<CRYSTAL_COUNT;++i) crystals[i].collected=0;
    for(int i=0;i<ENEMY_COUNT;++i){ enemies[i].alive=enemies[i].initial_alive; enemies[i].hp=enemies[i].max_hp; }
    for(int i=0;i<TRIGGER_COUNT;++i) triggers[i].fired=0;
    for(int i=0;i<SCRIPT_EVENT_COUNT;++i) script_events[i].fired=0;
    for(int i=0;i<SCRIPT_FLAG_COUNT;++i) script_flags[i].value=script_flags[i].initial_value;
    for(int i=0;i<SCRIPT_VAR_COUNT;++i) script_vars[i].value=script_vars[i].initial_value;
    for(int i=0;i<RPG_ITEM_COUNT;++i) items[i].count=items[i].initial_count;
    for(int i=0;i<RPG_PICKUP_COUNT;++i) pickups[i].collected=0;
    memset(g_door_open,0,sizeof(g_door_open));
    g_last_script_room=-999; g_dialog_timer=0; g_dialog_text=0; g_active_dialog=-1;
    p->max_hp=PLAYER_MAX_HP; p->hp=PLAYER_MAX_HP; p->invuln=0; p->attack_cd=0; p->attack_frames=0; p->fire_cd=0;
    memset(g_projectiles,0,sizeof(g_projectiles));memset(g_particles,0,sizeof(g_particles));
    for(int i=0;i<PARTICLE_EMITTER_COUNT;++i)particle_emitters[i].timer=0;
}

static void collide_surface(player_t *p,float old_y,float x,float y,float w,float h){
    if(!overlaps(p->x,p->y,PLAYER_W,PLAYER_H,x,y,w,h)) return;
    float old_bottom=old_y+PLAYER_H,new_bottom=p->y+PLAYER_H;
    if(p->vy>=0.0f&&old_bottom<=y+1.0f&&new_bottom>=y){ p->y=y-PLAYER_H; p->vy=0; p->on_ground=1; }
    else if(p->vy<0.0f&&old_y>=y+h-1.0f){ p->y=y+h; p->vy=0; }
}

static void collide_room(player_t *p,float old_y){
    p->on_ground=0;
    for(int i=0;i<PLATFORM_COUNT;++i) if(platforms[i].room==g_room) collide_surface(p,old_y,platforms[i].x,platforms[i].y,platforms[i].w,platforms[i].h);
    for(int i=0;i<TILE_COUNT;++i) if(tiles[i].room==g_room&&tiles[i].solid) collide_surface(p,old_y,tiles[i].x*TILE_SIZE,tiles[i].y*TILE_SIZE,TILE_SIZE,TILE_SIZE);
}

static void update_player(player_t *p){
    p->x+=p->vx;
    int roomw=room_world_width[g_room];
    if(p->x<-PLAYER_W){ if(g_room>0){--g_room;p->x=room_world_width[g_room]-PLAYER_W-2;g_camera_x=room_world_width[g_room]-VW;} else p->x=0; }
    else if(p->x>roomw){ if(g_room<ROOM_COUNT-1){++g_room;p->x=2;g_camera_x=0;} else p->x=roomw-PLAYER_W; }
    float old_y=p->y; p->vy+=GAME_GRAVITY; if(p->vy>GAME_MAX_FALL_SPEED)p->vy=GAME_MAX_FALL_SPEED; p->y+=p->vy; collide_room(p,old_y);
    if(p->y>VH+30) respawn(p);
}

static void damage_player(player_t *p,int dmg){
    if(p->invuln>0)return;
    p->hp-=dmg; p->invuln=45; snd_hit(); camera_shake(3,12);
    if(p->hp<=0){ p->hp=p->max_hp; respawn(p); }
}
static void update_enemies(player_t *p){
    for(int i=0;i<ENEMY_COUNT;++i){
        enemy_t *e=&enemies[i]; if(e->room!=g_room||!e->alive) continue;
        float sp=e->speed;
        if(e->type==2 && (p->x-e->x<70.0f&&p->x-e->x>-70.0f)) e->dir=(p->x>=e->x)?1:-1, sp*=1.8f;
        if(e->type==3) sp*=0.55f;
        e->x+=sp*e->dir; if(e->x<=e->left){e->x=e->left;e->dir=1;} if(e->x>=e->right){e->x=e->right;e->dir=-1;}
        if(overlaps(p->x,p->y,PLAYER_W,PLAYER_H,e->x,e->y,e->w,e->h)){ damage_player(p,e->damage); }
    }
}
static void player_attack(player_t *p,int facing){
    if(p->attack_cd>0)return;
    p->attack_cd=20; p->attack_frames=6; camera_shake(1,4);
    float ax=facing>0?p->x+PLAYER_W:p->x-12;
    for(int i=0;i<ENEMY_COUNT;++i){
        enemy_t *e=&enemies[i]; if(e->room!=g_room||!e->alive)continue;
        if(overlaps(ax,p->y+2,12,PLAYER_H-2,e->x,e->y,e->w,e->h)){
            e->hp-=weapon_damage(); particle_burst(e->x+e->w/2,e->y+e->h/2,2,6,255,180,80); if(e->hp<=0){e->alive=0;particle_burst(e->x+e->w/2,e->y+e->h/2,3,12,190,70,70);} else e->dir=-e->dir;
        }
    }
}

static void process_triggers(player_t *p){
    for(int i=0;i<TRIGGER_COUNT;++i){
        trigger_t *t=&triggers[i]; if(t->room!=g_room||(t->once&&t->fired))continue;
        if(!overlaps(p->x,p->y,PLAYER_W,PLAYER_H,t->x,t->y,t->w,t->h))continue;
        t->fired=1;
        if(!strcmp(t->action,"checkpoint")){ g_checkpoint_room=g_room;g_checkpoint_x=p->x;g_checkpoint_y=p->y; xenon_tone(520,45,XENON_TONE_AMPLITUDE_25); }
        else if(!strcmp(t->action,"teleport")&&t->target_room>=0){ g_room=t->target_room;p->x=t->target_x;p->y=t->target_y;p->vx=p->vy=0;g_teleport_cooldown=20; }
        else xenon_tone(430,60,XENON_TONE_AMPLITUDE_25);
    }
}

static void process_teleports(player_t *p){
    if(g_teleport_cooldown>0){--g_teleport_cooldown;return;}
    for(int i=0;i<TELEPORT_COUNT;++i){ const teleport_t *t=&teleports[i]; if(t->room!=g_room)continue;
        if(overlaps(p->x,p->y,PLAYER_W,PLAYER_H,t->x,t->y,t->w,t->h)){ g_room=t->target_room;p->x=t->target_x;p->y=t->target_y;p->vx=p->vy=0;g_teleport_cooldown=30;snd_door();return; }
    }
}

static void interact(player_t *p){
    for(int i=0;i<NPC_COUNT;++i){ const npc_t *n=&npcs[i]; if(n->room==g_room&&overlaps(p->x-6,p->y-4,PLAYER_W+12,PLAYER_H+8,n->x,n->y,n->w,n->h)){ g_npc_flash=90;snd_npc(); if(n->dialogue>=0) start_dialog(n->dialogue); else script_dialog(n->message); run_script_event(EVT_ON_INTERACT,g_room,local_npc_index(i),p);return; } }
    for(int i=0;i<DOOR_COUNT;++i){ const door_t *d=&doors[i]; if(d->room!=g_room)continue;
        if(overlaps(p->x,p->y,PLAYER_W,PLAYER_H,d->x-3,d->y-3,d->w+6,d->h+6)){
            if(d->key_item>=0&&inventory_count(d->key_item)<=0){ xenon_tone(190,60,XENON_TONE_AMPLITUDE_25);return; }
            if(!g_door_open[i]&&d->locked&&d->key_item<0){ xenon_tone(190,60,XENON_TONE_AMPLITUDE_25);return; }
            if(!g_door_open[i]&&d->requires_all&&total_collected()!=CRYSTAL_COUNT){ xenon_tone(190,60,XENON_TONE_AMPLITUDE_25);return; }
            g_room=d->target_room;p->x=d->target_x;p->y=d->target_y;p->vx=p->vy=0;g_teleport_cooldown=30;snd_door();return;
        }
    }
}

int main(void){
    xenos_init(VIDEO_MODE_AUTO); console_init(); usb_init(); usb_do_poll(); xenon_sound_init();
    if(fb2d_init(&g_fb)!=0){printf("fb2d init failed\n");for(;;)usb_do_poll();}
    g_scale=g_fb.width/VW;if(g_fb.height/VH<g_scale)g_scale=g_fb.height/VH;if(g_scale<1)g_scale=1;
    g_off_x=(g_fb.width-VW*g_scale)/2;g_off_y=(g_fb.height-VH*g_scale)/2;
    printf("%s | rooms=%d | entities E%d N%d T%d D%d P%d\n",GAME_NAME,ROOM_COUNT,ENEMY_COUNT,NPC_COUNT,TRIGGER_COUNT,DOOR_COUNT,TELEPORT_COUNT);

    player_t player;int facing,won;reset_game(&player,&facing,&won);process_room_enter(&player);
    struct controller_data_s prev;memset(&prev,0,sizeof(prev));
    for(;;){
        struct controller_data_s pad;memset(&pad,0,sizeof(pad));
        if(get_controller_data(&pad,0)){
            int left=pad.left||pad.s1_x<-9000,right=pad.right||pad.s1_x>9000;
            if(g_active_dialog>=0){
                const dialog_node_t *dn=&dialog_nodes[g_active_dialog];
                if((pad.up&&!prev.up)||(pad.s1_y>9000&&prev.s1_y<=9000)){ if(dn->choice_count>0) g_dialog_choice=(g_dialog_choice+dn->choice_count-1)%dn->choice_count; }
                if((pad.down&&!prev.down)||(pad.s1_y<-9000&&prev.s1_y>=-9000)){ if(dn->choice_count>0) g_dialog_choice=(g_dialog_choice+1)%dn->choice_count; }
                if((pad.b&&!prev.b)||(pad.a&&!prev.a)) choose_dialog(&player);
            } else if(!won){
                if(left&&!right){player.vx=-PLAYER_SPEED;facing=-1;} else if(right&&!left){player.vx=PLAYER_SPEED;facing=1;} else {player.vx*=.55f;if(player.vx>-.08f&&player.vx<.08f)player.vx=0;}
                if(pad.a&&!prev.a&&player.on_ground){player.vy=-PLAYER_JUMP_SPEED;player.on_ground=0;snd_jump();}
                if(pad.x&&!prev.x) player_attack(&player,facing);
                if(pad.rb||pad.rt>32) fire_gun(&player,facing);
                if(pad.y&&!prev.y) use_consumable(&player);
                update_player(&player); update_enemies(&player); update_projectiles(); update_particles();
                for(int i=0;i<CRYSTAL_COUNT;++i) if(crystals[i].room==g_room&&!crystals[i].collected&&overlaps(player.x,player.y,PLAYER_W,PLAYER_H,crystals[i].x,crystals[i].y,6,7)){crystals[i].collected=1;snd_collect();run_script_event(EVT_ON_COLLECT,g_room,local_crystal_index(i),&player);}
                for(int i=0;i<RPG_PICKUP_COUNT;++i) if(!pickups[i].collected&&pickups[i].room==g_room&&overlaps(player.x,player.y,PLAYER_W,PLAYER_H,pickups[i].x,pickups[i].y,6,6)){pickups[i].collected=1;inventory_add(pickups[i].item_index,pickups[i].qty);snd_collect();}
                process_triggers(&player);process_teleports(&player);
                if(pad.b&&!prev.b) interact(&player);
                process_room_enter(&player);
                if(g_room==EXIT_ROOM&&total_collected()==CRYSTAL_COUNT&&overlaps(player.x,player.y,PLAYER_W,PLAYER_H,EXIT_X-2,EXIT_Y-3,EXIT_W+4,EXIT_H+3)){won=1;player.vx=player.vy=0;}
            }
            if(pad.start&&!prev.start)reset_game(&player,&facing,&won);
            if(won&&pad.a&&!prev.a)reset_game(&player,&facing,&won);
            prev=pad;
        }
        if(g_npc_flash>0)--g_npc_flash; if(g_dialog_timer>0&&g_active_dialog<0)--g_dialog_timer;
        if(player.invuln>0)--player.invuln; if(player.attack_cd>0)--player.attack_cd; if(player.attack_frames>0)--player.attack_frames; if(player.fire_cd>0)--player.fire_cd;
        update_camera(&player);
        draw_room(&player,facing,won);fb2d_present(&g_fb);usb_do_poll();++g_frame_counter;
    }
    return 0;
}
