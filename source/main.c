#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <input/input.h>
#include <xenos/xenos.h>
#include <console/console.h>
#include <usb/usbmain.h>

#include "fb2d.h"

#define VW 320
#define VH 180
#define PLAYER_W 7
#define PLAYER_H 11
#define CRYSTAL_COUNT 5

typedef struct {
    float x, y, w, h;
} rectf_t;

typedef struct {
    float x, y;
    float vx, vy;
    int on_ground;
} player_t;

typedef struct {
    int x, y;
    int collected;
} crystal_t;

static const rectf_t platforms[] = {
    {  0, 160, 320, 20 },
    { 35, 132,  58,  8 },
    {112, 108,  58,  8 },
    {194,  84,  58,  8 },
    {265, 126,  45,  8 }
};

static crystal_t crystals[CRYSTAL_COUNT] = {
    { 59, 121, 0 },
    {138,  97, 0 },
    {220,  73, 0 },
    {286, 115, 0 },
    {246, 149, 0 }
};

static fb2d_t g_fb;
static int g_scale;
static int g_off_x;
static int g_off_y;

static int iroundf(float v) {
    return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static int overlaps(float ax, float ay, float aw, float ah,
                    float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

static void draw_rect_screen(int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_fb.width)  w = g_fb.width - x;
    if (y + h > g_fb.height) h = g_fb.height - y;
    if (w <= 0 || h <= 0) return;

    for (int yy = y; yy < y + h; ++yy) {
        uint32_t *row = &g_fb.backbuffer[yy * g_fb.width + x];
        for (int xx = 0; xx < w; ++xx) row[xx] = color;
    }
}

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    draw_rect_screen(
        g_off_x + x * g_scale,
        g_off_y + y * g_scale,
        w * g_scale,
        h * g_scale,
        color
    );
}

static void draw_pixel(int x, int y, uint32_t color) {
    draw_rect(x, y, 1, 1, color);
}

static void draw_crystal(int x, int y) {
    const uint32_t dark = fb2d_rgb(52, 107, 123);
    const uint32_t mid  = fb2d_rgb(55, 215, 225);
    const uint32_t hi   = fb2d_rgb(194, 255, 250);

    draw_rect(x + 2, y + 0, 2, 1, hi);
    draw_rect(x + 1, y + 1, 4, 1, mid);
    draw_rect(x + 0, y + 2, 6, 3, mid);
    draw_rect(x + 1, y + 5, 4, 1, dark);
    draw_rect(x + 2, y + 6, 2, 1, dark);
    draw_pixel(x + 1, y + 2, hi);
}

static void draw_player(const player_t *p, int facing) {
    const int x = iroundf(p->x);
    const int y = iroundf(p->y);
    const uint32_t outline = fb2d_rgb(31, 28, 46);
    const uint32_t body    = fb2d_rgb(245, 93, 113);
    const uint32_t light   = fb2d_rgb(255, 196, 137);
    const uint32_t boot    = fb2d_rgb(76, 69, 100);

    draw_rect(x + 1, y + 0, 5, 1, outline);
    draw_rect(x + 0, y + 1, 7, 7, outline);
    draw_rect(x + 1, y + 1, 5, 6, body);
    draw_rect(x + 2, y + 2, 3, 2, light);
    draw_pixel(x + (facing > 0 ? 4 : 2), y + 2, outline);
    draw_rect(x + 1, y + 8, 2, 3, boot);
    draw_rect(x + 4, y + 8, 2, 3, boot);
}

static void draw_exit(int open) {
    const int x = 301, y = 139;
    const uint32_t frame = fb2d_rgb(35, 31, 57);
    const uint32_t shut  = fb2d_rgb(89, 74, 120);
    const uint32_t glow  = fb2d_rgb(109, 255, 156);

    draw_rect(x - 2, y - 3, 15, 24, frame);
    draw_rect(x, y, 11, 21, open ? glow : shut);
    if (open) {
        draw_rect(x + 3, y + 3, 5, 15, fb2d_rgb(213, 255, 211));
    } else {
        draw_pixel(x + 8, y + 10, fb2d_rgb(231, 204, 128));
    }
}

static void draw_scene(const player_t *p, int facing, int collected, int won) {
    const uint32_t sky = fb2d_rgb(19, 19, 38);
    fb2d_clear(&g_fb, sky);

    /* letterbox/pixel canvas background */
    draw_rect(0, 0, VW, VH, sky);

    /* distant stars */
    static const uint16_t stars[][2] = {
        {12,18},{39,31},{71,14},{101,38},{139,20},{173,29},
        {205,12},{238,35},{270,18},{305,42},{24,74},{88,61},
        {154,56},{257,65},{294,82}
    };
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i) {
        draw_pixel(stars[i][0], stars[i][1], fb2d_rgb(93, 91, 128));
    }

    /* city silhouettes */
    draw_rect(0, 145, 320, 15, fb2d_rgb(27, 31, 54));
    draw_rect(18, 135, 22, 25, fb2d_rgb(27, 31, 54));
    draw_rect(79, 140, 31, 20, fb2d_rgb(27, 31, 54));
    draw_rect(177, 137, 24, 23, fb2d_rgb(27, 31, 54));
    draw_rect(250, 142, 30, 18, fb2d_rgb(27, 31, 54));

    const uint32_t dirt = fb2d_rgb(55, 52, 73);
    const uint32_t edge = fb2d_rgb(116, 97, 115);

    for (unsigned i = 0; i < sizeof(platforms) / sizeof(platforms[0]); ++i) {
        const rectf_t *r = &platforms[i];
        draw_rect((int)r->x, (int)r->y, (int)r->w, (int)r->h, dirt);
        draw_rect((int)r->x, (int)r->y, (int)r->w, 2, edge);
    }

    for (int i = 0; i < CRYSTAL_COUNT; ++i) {
        if (!crystals[i].collected) draw_crystal(crystals[i].x, crystals[i].y);
    }

    draw_exit(collected == CRYSTAL_COUNT);
    draw_player(p, facing);

    /* HUD: one tiny crystal slot per collectible */
    draw_rect(6, 6, 42, 11, fb2d_rgb(9, 11, 24));
    for (int i = 0; i < CRYSTAL_COUNT; ++i) {
        if (i < collected)
            draw_crystal(9 + i * 7, 8);
        else
            draw_rect(11 + i * 7, 11, 2, 2, fb2d_rgb(65, 65, 86));
    }

    if (won) {
        /* simple celebratory banner without requiring a font asset */
        draw_rect(91, 57, 138, 45, fb2d_rgb(8, 10, 22));
        draw_rect(94, 60, 132, 39, fb2d_rgb(47, 39, 72));
        for (int i = 0; i < 8; ++i) {
            draw_rect(107 + i * 14, 74 + (i & 1) * 4, 8, 8,
                      (i & 1) ? fb2d_rgb(109,255,156) : fb2d_rgb(55,215,225));
        }
    }
}

static void reset_game(player_t *p, int *facing, int *collected, int *won) {
    p->x = 14.0f;
    p->y = 148.0f;
    p->vx = 0.0f;
    p->vy = 0.0f;
    p->on_ground = 0;
    *facing = 1;
    *collected = 0;
    *won = 0;
    for (int i = 0; i < CRYSTAL_COUNT; ++i) crystals[i].collected = 0;
}

static void move_and_collide(player_t *p) {
    const float old_y = p->y;

    p->x += p->vx;
    if (p->x < 0) p->x = 0;
    if (p->x > VW - PLAYER_W) p->x = VW - PLAYER_W;

    p->vy += 0.24f;
    if (p->vy > 5.0f) p->vy = 5.0f;
    p->y += p->vy;
    p->on_ground = 0;

    for (unsigned i = 0; i < sizeof(platforms) / sizeof(platforms[0]); ++i) {
        const rectf_t *r = &platforms[i];

        if (!overlaps(p->x, p->y, PLAYER_W, PLAYER_H,
                      r->x, r->y, r->w, r->h))
            continue;

        const float old_bottom = old_y + PLAYER_H;
        const float new_bottom = p->y + PLAYER_H;

        if (p->vy >= 0.0f && old_bottom <= r->y + 1.0f && new_bottom >= r->y) {
            p->y = r->y - PLAYER_H;
            p->vy = 0.0f;
            p->on_ground = 1;
        } else if (p->vy < 0.0f && old_y >= r->y + r->h - 1.0f) {
            p->y = r->y + r->h;
            p->vy = 0.0f;
        }
    }

    if (p->y > VH + 20) {
        p->x = 14;
        p->y = 148;
        p->vx = p->vy = 0;
    }
}

int main(void) {
    xenos_init(VIDEO_MODE_AUTO);
    console_init();

    usb_init();
    usb_do_poll();

    if (fb2d_init(&g_fb) != 0) {
        printf("fb2d_init failed\n");
        for (;;) usb_do_poll();
    }

    g_scale = g_fb.width / VW;
    if (g_fb.height / VH < g_scale) g_scale = g_fb.height / VH;
    if (g_scale < 1) g_scale = 1;

    g_off_x = (g_fb.width - VW * g_scale) / 2;
    g_off_y = (g_fb.height - VH * g_scale) / 2;

    printf("Pixel Run 360: display=%dx%d scale=%d\n",
           g_fb.width, g_fb.height, g_scale);

    player_t player;
    int facing, collected, won;
    reset_game(&player, &facing, &collected, &won);

    struct controller_data_s previous;
    memset(&previous, 0, sizeof(previous));

    for (;;) {
        struct controller_data_s pad;
        memset(&pad, 0, sizeof(pad));

        const int connected = get_controller_data(&pad, 0);

        if (connected) {
            int left  = pad.left  || pad.s1_x < -9000;
            int right = pad.right || pad.s1_x >  9000;

            if (!won) {
                if (left && !right) {
                    player.vx = -1.25f;
                    facing = -1;
                } else if (right && !left) {
                    player.vx = 1.25f;
                    facing = 1;
                } else {
                    player.vx *= 0.55f;
                    if (player.vx > -0.08f && player.vx < 0.08f) player.vx = 0;
                }

                if (pad.a && !previous.a && player.on_ground) {
                    player.vy = -4.35f;
                    player.on_ground = 0;
                }

                move_and_collide(&player);

                for (int i = 0; i < CRYSTAL_COUNT; ++i) {
                    if (!crystals[i].collected &&
                        overlaps(player.x, player.y, PLAYER_W, PLAYER_H,
                                 crystals[i].x, crystals[i].y, 6, 7)) {
                        crystals[i].collected = 1;
                        ++collected;
                    }
                }

                if (collected == CRYSTAL_COUNT &&
                    overlaps(player.x, player.y, PLAYER_W, PLAYER_H,
                             299, 136, 17, 24)) {
                    won = 1;
                    player.vx = player.vy = 0;
                }
            }

            if (pad.start && !previous.start) {
                reset_game(&player, &facing, &collected, &won);
            }

            if (won && pad.a && !previous.a) {
                reset_game(&player, &facing, &collected, &won);
            }

            previous = pad;
        }

        draw_scene(&player, facing, collected, won);
        fb2d_present(&g_fb);
        usb_do_poll();
    }

    return 0;
}
