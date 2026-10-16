/* SPDX-License-Identifier: MIT */

#include "flea3-view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_ZOOM  1.0
#define MAX_ZOOM  8.0
#define ZOOM_STEP 0.5

#define CROSS_ARM 40
#define CROSS_GAP 7

struct Flea3View {
    double zoom;
    int angle;
    int flip_h;
    int flip_v;
    int crosshair;
    char status[128];
};

static double clamp_double(double value, double min, double max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static void refresh_status(Flea3View *v)
{
    snprintf(
        v->status,
        sizeof v->status,
        "zoom %.1fx | rot %d | H %s | V %s | crosshair %s",
        v->zoom,
        v->angle,
        v->flip_h ? "on" : "off",
        v->flip_v ? "on" : "off",
        v->crosshair ? "on" : "off"
    );
}

static void zoom_by(Flea3View *v, int direction)
{
    if (direction > 0)
        v->zoom += ZOOM_STEP;
    else if (direction < 0)
        v->zoom -= ZOOM_STEP;

    v->zoom = clamp_double(v->zoom, MIN_ZOOM, MAX_ZOOM);
    refresh_status(v);
}

Flea3View *flea3_view_new(void)
{
    Flea3View *v = calloc(1, sizeof(*v));

    if (v == NULL)
        return NULL;

    v->zoom = 1.0;
    v->angle = 0;
    v->crosshair = 1;
    refresh_status(v);
    return v;
}

void flea3_view_free(Flea3View *v)
{
    free(v);
}

int flea3_view_handle_key(Flea3View *v, SDL_Keycode key, SDL_Keymod mod)
{
    (void)mod;

    if (v == NULL)
        return 0;

    if (key == SDLK_c) {
        v->crosshair = !v->crosshair;
        refresh_status(v);
        return 1;
    }

    if (key == SDLK_PLUS || key == SDLK_EQUALS || key == SDLK_KP_PLUS) {
        zoom_by(v, 1);
        return 1;
    }

    if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
        zoom_by(v, -1);
        return 1;
    }

    if (key == SDLK_1) {
        v->zoom = 1.0;
        refresh_status(v);
        return 1;
    }

    if (key == SDLK_r) {
        v->angle += 90;
        if (v->angle >= 360)
            v->angle = 0;
        refresh_status(v);
        return 1;
    }

    if (key == SDLK_l) {
        v->angle -= 90;
        if (v->angle < 0)
            v->angle = 270;
        refresh_status(v);
        return 1;
    }

    if (key == SDLK_h) {
        v->flip_h = !v->flip_h;
        refresh_status(v);
        return 1;
    }

    if (key == SDLK_v) {
        v->flip_v = !v->flip_v;
        refresh_status(v);
        return 1;
    }

    return 0;
}

int flea3_view_handle_wheel(Flea3View *v, int y)
{
    if (v == NULL || y == 0)
        return 0;

    zoom_by(v, y > 0 ? 1 : -1);
    return 1;
}

const char *flea3_view_status(const Flea3View *v)
{
    if (v == NULL)
        return "";
    return v->status;
}

static void put_rgb(uint8_t *img, int w, int h, int x, int y,
                     uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *p;

    if (x < 0 || y < 0 || x >= w || y >= h)
        return;

    p = img + ((size_t)y * (size_t)w + (size_t)x) * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

static void hline(uint8_t *img, int w, int h, int x0, int x1, int y,
                  uint8_t r, uint8_t g, uint8_t b)
{
    int x;

    if (y < 0 || y >= h)
        return;
    if (x0 > x1) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    if (x1 < 0 || x0 >= w)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= w)
        x1 = w - 1;

    for (x = x0; x <= x1; x++)
        put_rgb(img, w, h, x, y, r, g, b);
}

static void vline(uint8_t *img, int w, int h, int x, int y0, int y1,
                  uint8_t r, uint8_t g, uint8_t b)
{
    int y;

    if (x < 0 || x >= w)
        return;
    if (y0 > y1) {
        int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    if (y1 < 0 || y0 >= h)
        return;
    if (y0 < 0)
        y0 = 0;
    if (y1 >= h)
        y1 = h - 1;

    for (y = y0; y <= y1; y++)
        put_rgb(img, w, h, x, y, r, g, b);
}

/* Same geometry as chameleon-live.c draw_crosshair, in output pixels. */
static void draw_crosshair(uint8_t *img, int w, int h)
{
    int cx = w / 2;
    int cy = h / 2;
    int arm = CROSS_ARM;
    int gap = CROSS_GAP;
    int box = 7;

    hline(img, w, h, cx - arm - 1, cx - gap - 1, cy + 1, 0, 0, 0);
    hline(img, w, h, cx + gap - 1, cx + arm - 1, cy + 1, 0, 0, 0);
    vline(img, w, h, cx + 1, cy - arm - 1, cy - gap - 1, 0, 0, 0);
    vline(img, w, h, cx + 1, cy + gap - 1, cy + arm - 1, 0, 0, 0);

    hline(img, w, h, cx - arm, cx - gap, cy, 255, 255, 255);
    hline(img, w, h, cx + gap, cx + arm, cy, 255, 255, 255);
    vline(img, w, h, cx, cy - arm, cy - gap, 255, 255, 255);
    vline(img, w, h, cx, cy + gap, cy + arm, 255, 255, 255);

    hline(img, w, h, cx - 3, cx - 3 + box - 1, cy - 3, 255, 255, 255);
    hline(img, w, h, cx - 3, cx - 3 + box - 1, cy - 3 + box - 1, 255, 255, 255);
    vline(img, w, h, cx - 3, cy - 3, cy - 3 + box - 1, 255, 255, 255);
    vline(img, w, h, cx - 3 + box - 1, cy - 3, cy - 3 + box - 1, 255, 255, 255);
}

int flea3_view_render(Flea3View *v, const uint8_t *src, int w, int h,
                      uint8_t **out, int *out_w, int *out_h)
{
    int crop_w;
    int crop_h;
    int crop_x;
    int crop_y;
    int dw;
    int dh;
    int rot;
    uint8_t *dst;
    size_t pixels;
    int dy;

    if (out != NULL)
        *out = NULL;
    if (out_w != NULL)
        *out_w = 0;
    if (out_h != NULL)
        *out_h = 0;

    if (v == NULL || src == NULL || out == NULL || out_w == NULL || out_h == NULL)
        return 0;
    if (w <= 0 || h <= 0)
        return 0;
    if ((size_t)w > SIZE_MAX / (size_t)h)
        return 0;

    pixels = (size_t)w * (size_t)h;
    if (pixels > SIZE_MAX / 3)
        return 0;

    crop_w = (int)((double)w / v->zoom);
    crop_h = (int)((double)h / v->zoom);
    if (crop_w < 1)
        crop_w = 1;
    if (crop_h < 1)
        crop_h = 1;
    if (crop_w > w)
        crop_w = w;
    if (crop_h > h)
        crop_h = h;
    crop_x = (w - crop_w) / 2;
    crop_y = (h - crop_h) / 2;

    rot = v->angle;
    if (rot == 90 || rot == 270) {
        dw = h;
        dh = w;
    } else {
        dw = w;
        dh = h;
    }

    dst = malloc(pixels * 3);
    if (dst == NULL)
        return 0;

    for (dy = 0; dy < dh; dy++) {
        int dx;

        for (dx = 0; dx < dw; dx++) {
            int ux;
            int uy;
            int sx;
            int sy;
            const uint8_t *sp;
            uint8_t *dp;

            if (rot == 90) {
                ux = dy;
                uy = h - 1 - dx;
            } else if (rot == 180) {
                ux = w - 1 - dx;
                uy = h - 1 - dy;
            } else if (rot == 270) {
                ux = w - 1 - dy;
                uy = dx;
            } else {
                ux = dx;
                uy = dy;
            }

            if (v->flip_h)
                ux = w - 1 - ux;
            if (v->flip_v)
                uy = h - 1 - uy;

            sx = crop_x + (int)(((int64_t)ux * crop_w) / w);
            sy = crop_y + (int)(((int64_t)uy * crop_h) / h);
            sp = src + ((size_t)sy * (size_t)w + (size_t)sx) * 3;
            dp = dst + ((size_t)dy * (size_t)dw + (size_t)dx) * 3;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
        }
    }

    if (v->crosshair)
        draw_crosshair(dst, dw, dh);

    *out = dst;
    *out_w = dw;
    *out_h = dh;
    return 1;
}
