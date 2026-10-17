#ifndef FB2D_H
#define FB2D_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int tiled_width;
    int tiled_height;
    int framebuffer_size;
    uint32_t *framebuffer;
    uint32_t *backbuffer;
} fb2d_t;

int fb2d_init(fb2d_t *fb);
void fb2d_clear(fb2d_t *fb, uint32_t color);
void fb2d_present(fb2d_t *fb);

static inline uint32_t fb2d_rgb(int r, int g, int b) {
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return ((uint32_t)b << 24) | ((uint32_t)g << 16) | ((uint32_t)r << 8);
}

#endif
