/*
 * Minimal Xbox 360 libxenon 2D framebuffer helper.
 *
 * The Xenos tiled-address calculation and display-info access are adapted
 * from the MIT-licensed xenon-soft3d project by SaveEditors:
 * https://github.com/SaveEditors/xenon-soft3d
 *
 * See THIRD_PARTY_LICENSES.md.
 */
#include "fb2d.h"

#include <stddef.h>
#include <stdlib.h>
#include <xenos/xenos.h>
#include <ppc/cache.h>

struct ati_display_info {
    uint32_t unknown1[4];
    uint32_t base;
    uint32_t unknown2[8];
    uint32_t width;
    uint32_t height;
} __attribute__((__packed__));

static inline int tiled_index(int tiled_width, int x, int y) {
    return (((y >> 5) * 32 * tiled_width + ((x >> 5) << 10)
        + (x & 3) + ((y & 1) << 2)
        + (((x & 31) >> 2) << 3)
        + (((y & 31) >> 1) << 6))
        ^ ((y & 8) << 2));
}

int fb2d_init(fb2d_t *fb) {
    struct ati_display_info *info =
        (struct ati_display_info *)0xec806100ULL;

    fb->framebuffer = (uint32_t *)(long)(info->base | 0x80000000);
    fb->width = (int)info->width;
    fb->height = (int)info->height;
    fb->tiled_width = ((fb->width + 31) >> 5) << 5;
    fb->tiled_height = ((fb->height + 31) >> 5) << 5;
    fb->framebuffer_size = fb->tiled_width * fb->tiled_height;

    fb->backbuffer = (uint32_t *)malloc(
        (size_t)fb->width * (size_t)fb->height * sizeof(uint32_t));

    return fb->backbuffer ? 0 : -1;
}

void fb2d_clear(fb2d_t *fb, uint32_t color) {
    const int count = fb->width * fb->height;
    for (int i = 0; i < count; ++i) {
        fb->backbuffer[i] = color;
    }
}

void fb2d_present(fb2d_t *fb) {
    const int w = fb->width;
    const int h = fb->height;
    const int tw = fb->tiled_width;

    for (int y = 0; y < h; ++y) {
        const int row = y * w;
        for (int x = 0; x < w; ++x) {
            fb->framebuffer[tiled_index(tw, x, y)] =
                fb->backbuffer[row + x];
        }
    }

    memdcbst(fb->framebuffer, fb->framebuffer_size * 4);
}
