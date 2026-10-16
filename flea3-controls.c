/* SPDX-License-Identifier: MIT */

#include "flea3-controls.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Chameleon keys ported here:
 *   Up/Down  shutter, as ExposureTime (Up longer, Down shorter)
 *   Right/Left  gain (Right higher, Left lower)
 *   A  toggle auto/manual, 0 restore auto
 *      (ExposureAuto + GainAuto, same trio as shutter/gain/exposure)
 *   M  full/fast frame rate (15 fps / 30 fps, clamped)
 *   X  hardware ROI: off -> 640x480 -> 320x240
 *
 * B is MONO8/MONO16 on the Chameleon. This viewer starts on RGB8 and the
 * stream is sized for that format. Mono8/Mono16 would replace RGB8, so B
 * is consumed and does not change PixelFormat.
 *
 * chameleon-live.c has no brightness, gamma, sharpness, or white-balance
 * key. Those nodes are left alone. Zoom, rotate, mirror, crosshair,
 * fullscreen, save, stats, and quit belong to the viewer.
 */

#define FULL_FPS 15.0
#define FAST_FPS 30.0
#define SENSOR_W 1280
#define SENSOR_H 960

struct Flea3Controls {
    ArvCamera *camera;
    int auto_mode;
    int fast;
    int roi;
    int has_exposure;
    int has_exposure_auto;
    int has_gain;
    int has_gain_auto;
    int has_frame_rate;
    int has_region;
    int has_binning;
    char status[320];
};

static int feature_ok(ArvCamera *camera, const char *name)
{
    GError *error = NULL;
    gboolean ok;

    ok = arv_camera_is_feature_available(camera, name, &error);
    if (error != NULL) {
        g_error_free(error);
        return 0;
    }
    return ok ? 1 : 0;
}

static int probe(ArvCamera *camera, gboolean (*fn)(ArvCamera *, GError **))
{
    GError *error = NULL;
    gboolean ok;

    ok = fn(camera, &error);
    if (error != NULL) {
        g_error_free(error);
        return 0;
    }
    return ok ? 1 : 0;
}

static double clamp_d(double value, double min, double max)
{
    if (!(max >= min))
        return value;
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static double snap_d(double value, double min, double inc)
{
    double steps;

    if (!(inc > 0.0))
        return value;
    steps = (value - min) / inc;
    if (steps >= 0.0)
        steps = (double)(long long)(steps + 0.5);
    else
        steps = (double)(long long)(steps - 0.5);
    return min + steps * inc;
}

static void refresh_status(Flea3Controls *c)
{
    GError *error = NULL;
    double shutter = 0.0;
    double gain = 0.0;
    double fps = 0.0;
    gint width = 0;
    gint height = 0;
    char shutter_buf[32];
    char gain_buf[32];
    char fps_buf[32];
    char size_buf[32];
    const char *roi_name;

    snprintf(shutter_buf, sizeof shutter_buf, "-");
    snprintf(gain_buf, sizeof gain_buf, "-");
    snprintf(fps_buf, sizeof fps_buf, "-");
    snprintf(size_buf, sizeof size_buf, "-");

    if (c->has_exposure) {
        shutter = arv_camera_get_exposure_time(c->camera, &error);
        if (error == NULL)
            snprintf(shutter_buf, sizeof shutter_buf, "%.1fus", shutter);
        g_clear_error(&error);
    }

    if (c->has_gain) {
        gain = arv_camera_get_gain(c->camera, &error);
        if (error == NULL)
            snprintf(gain_buf, sizeof gain_buf, "%.2f", gain);
        g_clear_error(&error);
    }

    if (c->has_frame_rate) {
        fps = arv_camera_get_frame_rate(c->camera, &error);
        if (error == NULL)
            snprintf(fps_buf, sizeof fps_buf, "%.1f", fps);
        g_clear_error(&error);
    }

    arv_camera_get_region(c->camera, NULL, NULL, &width, &height, &error);
    if (error == NULL && width > 0 && height > 0)
        snprintf(size_buf, sizeof size_buf, "%dx%d", width, height);
    g_clear_error(&error);

    if (c->roi == 1)
        roi_name = "640x480";
    else if (c->roi == 2)
        roi_name = "320x240";
    else
        roi_name = "off";

    snprintf(
        c->status,
        sizeof c->status,
        "%s shutter=%s gain=%s fps=%s mode=%s roi=%s size=%s",
        c->auto_mode ? "AUTO" : "MANUAL",
        shutter_buf,
        gain_buf,
        fps_buf,
        c->fast ? "fast" : "full",
        roi_name,
        size_buf
    );
}

static void set_auto_mode(Flea3Controls *c, int enabled)
{
    GError *error = NULL;
    ArvAuto mode;

    c->auto_mode = enabled ? 1 : 0;
    mode = enabled ? ARV_AUTO_CONTINUOUS : ARV_AUTO_OFF;

    if (c->has_exposure_auto) {
        arv_camera_set_exposure_time_auto(c->camera, mode, &error);
        g_clear_error(&error);
    }
    if (c->has_gain_auto) {
        arv_camera_set_gain_auto(c->camera, mode, &error);
        g_clear_error(&error);
    }
}

static int read_range(
    ArvCamera *camera,
    void (*bounds)(ArvCamera *, double *, double *, GError **),
    double (*increment)(ArvCamera *, GError **),
    double (*get)(ArvCamera *, GError **),
    double *value,
    double *min,
    double *max,
    double *inc)
{
    GError *error = NULL;

    *value = 0.0;
    *min = 0.0;
    *max = 0.0;
    *inc = 0.0;

    bounds(camera, min, max, &error);
    if (error != NULL || !(*max >= *min)) {
        g_clear_error(&error);
        return 0;
    }

    *inc = increment(camera, &error);
    if (error != NULL || !(*inc > 0.0)) {
        g_clear_error(&error);
        *inc = 0.0;
    }

    *value = get(camera, &error);
    if (error != NULL) {
        g_clear_error(&error);
        return 0;
    }
    return 1;
}

static void nudge(
    Flea3Controls *c,
    int direction,
    int proportional,
    int present,
    void (*bounds)(ArvCamera *, double *, double *, GError **),
    double (*increment)(ArvCamera *, GError **),
    double (*get)(ArvCamera *, GError **),
    void (*set)(ArvCamera *, double, GError **))
{
    GError *error = NULL;
    double value;
    double min;
    double max;
    double inc;
    double step;
    double span;
    double next;

    if (!present)
        return;

    if (c->auto_mode)
        set_auto_mode(c, 0);

    if (!read_range(c->camera, bounds, increment, get, &value, &min, &max, &inc))
        return;

    if (proportional) {
        step = value * 0.10;
        if (step < 1.0)
            step = 1.0;
    } else {
        span = max - min;
        step = span * (10.0 / 682.0);
        if (!(step > 0.0))
            step = span / 40.0;
    }
    if (inc > step)
        step = inc;
    if (!(step > 0.0))
        return;

    next = direction > 0 ? value + step : value - step;
    next = clamp_d(next, min, max);
    next = snap_d(next, min, inc);
    next = clamp_d(next, min, max);

    if (direction > 0 && !(next > value) && value < max) {
        next = inc > 0.0 ? value + inc : max;
        next = clamp_d(next, min, max);
    } else if (direction < 0 && !(next < value) && value > min) {
        next = inc > 0.0 ? value - inc : min;
        next = clamp_d(next, min, max);
    }

    set(c->camera, next, &error);
    g_clear_error(&error);
}

static void apply_frame_rate(Flea3Controls *c)
{
    GError *error = NULL;
    double min = 0.0;
    double max = 0.0;
    double target;

    if (!c->has_frame_rate)
        return;

    target = c->fast ? FAST_FPS : FULL_FPS;
    arv_camera_get_frame_rate_bounds(c->camera, &min, &max, &error);
    if (error != NULL || !(max >= min)) {
        g_clear_error(&error);
        return;
    }
    g_clear_error(&error);

    target = clamp_d(target, min, max);
    arv_camera_set_frame_rate(c->camera, target, &error);
    g_clear_error(&error);
}

static int read_int_inc(gint (*fn)(ArvCamera *, GError **), ArvCamera *camera)
{
    GError *error = NULL;
    gint inc;

    inc = fn(camera, &error);
    if (error != NULL || inc <= 0) {
        g_clear_error(&error);
        return 1;
    }
    return inc;
}

static void read_int_bounds(
    void (*fn)(ArvCamera *, gint *, gint *, GError **),
    ArvCamera *camera,
    int *min,
    int *max,
    int fallback_min,
    int fallback_max)
{
    GError *error = NULL;
    gint lo = fallback_min;
    gint hi = fallback_max;

    fn(camera, &lo, &hi, &error);
    if (error != NULL || hi < lo) {
        g_clear_error(&error);
        *min = fallback_min;
        *max = fallback_max;
        return;
    }
    *min = lo;
    *max = hi;
}

static int align_size(int value, int min, int max, int inc)
{
    if (max < min)
        return value;
    if (value < min)
        value = min;
    if (value > max)
        value = max;
    if (inc > 1) {
        int steps = (value - min) / inc;
        value = min + steps * inc;
        if (value > max) {
            steps = (max - min) / inc;
            value = min + steps * inc;
        }
    }
    if (value < 1)
        value = 1;
    return value;
}

static int align_offset(int value, int inc)
{
    if (value < 0)
        value = 0;
    if (inc > 1)
        value = (value / inc) * inc;
    return value;
}

static int clamp_bin(ArvCamera *camera, int value, int axis)
{
    GError *error = NULL;
    gint min = 1;
    gint max = 1;
    gint inc = 1;

    if (axis == 0)
        arv_camera_get_x_binning_bounds(camera, &min, &max, &error);
    else
        arv_camera_get_y_binning_bounds(camera, &min, &max, &error);
    if (error != NULL || max < min) {
        g_clear_error(&error);
        return 1;
    }

    if (axis == 0)
        inc = arv_camera_get_x_binning_increment(camera, &error);
    else
        inc = arv_camera_get_y_binning_increment(camera, &error);
    if (error != NULL || inc < 1) {
        g_clear_error(&error);
        inc = 1;
    }

    if (value < min)
        value = min;
    if (value > max)
        value = max;
    if (inc > 1)
        value = min + ((value - min) / inc) * inc;
    if (value < 1)
        value = 1;
    return value;
}

static void sensor_size(ArvCamera *camera, int *width, int *height)
{
    GError *error = NULL;
    gint w = SENSOR_W;
    gint h = SENSOR_H;

    arv_camera_get_sensor_size(camera, &w, &h, &error);
    if (error != NULL || w < 2 || h < 2) {
        g_clear_error(&error);
        w = SENSOR_W;
        h = SENSOR_H;
    }
    *width = w;
    *height = h;
}

static int apply_binning(Flea3Controls *c)
{
    GError *error = NULL;
    int dx = 1;
    int dy = 1;

    if (!c->has_binning)
        return 0;

    if (c->roi == 1) {
        dx = 2;
        dy = 2;
    } else if (c->roi == 2) {
        dx = 4;
        dy = 4;
    }

    dx = clamp_bin(c->camera, dx, 0);
    dy = clamp_bin(c->camera, dy, 1);
    if (c->roi != 0 && dx <= 1 && dy <= 1)
        return 0;

    arv_camera_set_binning(c->camera, dx, dy, &error);
    if (error != NULL) {
        g_error_free(error);
        return 0;
    }
    return 1;
}

static int apply_roi(Flea3Controls *c)
{
    GError *error = NULL;
    int sensor_w = SENSOR_W;
    int sensor_h = SENSOR_H;
    int min_w = 1;
    int max_w = SENSOR_W;
    int min_h = 1;
    int max_h = SENSOR_H;
    int width;
    int height;
    int x;
    int y;
    int winc;
    int hinc;
    int xinc;
    int yinc;

    if (c->has_binning) {
        arv_camera_set_binning(c->camera, 1, 1, &error);
        g_clear_error(&error);
    }

    if (!c->has_region)
        return apply_binning(c);

    sensor_size(c->camera, &sensor_w, &sensor_h);
    read_int_bounds(arv_camera_get_width_bounds, c->camera, &min_w, &max_w, 1, sensor_w);
    read_int_bounds(arv_camera_get_height_bounds, c->camera, &min_h, &max_h, 1, sensor_h);
    winc = read_int_inc(arv_camera_get_width_increment, c->camera);
    hinc = read_int_inc(arv_camera_get_height_increment, c->camera);
    xinc = read_int_inc(arv_camera_get_x_offset_increment, c->camera);
    yinc = read_int_inc(arv_camera_get_y_offset_increment, c->camera);

    if (c->roi == 1) {
        width = 640;
        height = 480;
    } else if (c->roi == 2) {
        width = 320;
        height = 240;
    } else {
        width = sensor_w;
        height = sensor_h;
    }

    width = align_size(width, min_w, max_w, winc);
    height = align_size(height, min_h, max_h, hinc);
    x = align_offset((sensor_w - width) / 2, xinc);
    y = align_offset((sensor_h - height) / 2, yinc);
    if (x + width > sensor_w)
        x = align_offset(sensor_w - width, xinc);
    if (y + height > sensor_h)
        y = align_offset(sensor_h - height, yinc);

    arv_camera_set_region(c->camera, x, y, width, height, &error);
    if (error == NULL)
        return 1;

    g_error_free(error);
    return apply_binning(c);
}

static void select_gain_all(ArvCamera *camera)
{
    GError *error = NULL;
    gboolean all;

    if (!feature_ok(camera, "GainSelector"))
        return;

    all = arv_camera_is_enumeration_entry_available(camera, "GainSelector", "All", &error);
    if (error != NULL) {
        g_error_free(error);
        return;
    }
    if (!all)
        return;

    error = NULL;
    arv_camera_select_gain(camera, "All", &error);
    g_clear_error(&error);
}

static void enable_exposure(ArvCamera *camera)
{
    GError *error = NULL;

    if (!feature_ok(camera, "ExposureMode"))
        return;

    arv_camera_set_exposure_mode(camera, ARV_EXPOSURE_MODE_TIMED, &error);
    g_clear_error(&error);
}

Flea3Controls *flea3_controls_new(ArvCamera *camera)
{
    Flea3Controls *c;

    if (camera == NULL)
        return NULL;

    c = calloc(1, sizeof *c);
    if (c == NULL)
        return NULL;

    c->camera = g_object_ref(camera);
    c->auto_mode = 1;
    c->has_exposure = probe(camera, arv_camera_is_exposure_time_available);
    c->has_exposure_auto = probe(camera, arv_camera_is_exposure_auto_available);
    c->has_gain = probe(camera, arv_camera_is_gain_available);
    c->has_gain_auto = probe(camera, arv_camera_is_gain_auto_available);
    c->has_frame_rate = probe(camera, arv_camera_is_frame_rate_available);
    c->has_region = feature_ok(camera, "Width");
    c->has_binning = probe(camera, arv_camera_is_binning_available);

    select_gain_all(camera);
    enable_exposure(camera);
    set_auto_mode(c, 1);
    refresh_status(c);
    return c;
}

void flea3_controls_free(Flea3Controls *c)
{
    if (c == NULL)
        return;
    if (c->camera != NULL)
        g_object_unref(c->camera);
    free(c);
}

int flea3_controls_handle_key(Flea3Controls *c, SDL_Keycode key, SDL_Keymod mod)
{
    int next_roi;
    int previous_roi;

    (void)mod;

    if (c == NULL || c->camera == NULL)
        return 0;

    if (key == SDLK_b) {
        /* Mono8/Mono16 is the Chameleon bit-depth key. Applying it here
           would drop the default RGB8 startup format, so leave PixelFormat. */
        printf("Bit depth unchanged (RGB8; MONO8/MONO16 would replace it).\n");
        fflush(stdout);
        return 1;
    }

    if (key == SDLK_a) {
        set_auto_mode(c, !c->auto_mode);
    } else if (key == SDLK_0) {
        set_auto_mode(c, 1);
    } else if (key == SDLK_UP) {
        nudge(
            c, 1, 1, c->has_exposure,
            arv_camera_get_exposure_time_bounds,
            arv_camera_get_exposure_time_increment,
            arv_camera_get_exposure_time,
            arv_camera_set_exposure_time
        );
    } else if (key == SDLK_DOWN) {
        nudge(
            c, -1, 1, c->has_exposure,
            arv_camera_get_exposure_time_bounds,
            arv_camera_get_exposure_time_increment,
            arv_camera_get_exposure_time,
            arv_camera_set_exposure_time
        );
    } else if (key == SDLK_RIGHT) {
        nudge(
            c, 1, 0, c->has_gain,
            arv_camera_get_gain_bounds,
            arv_camera_get_gain_increment,
            arv_camera_get_gain,
            arv_camera_set_gain
        );
    } else if (key == SDLK_LEFT) {
        nudge(
            c, -1, 0, c->has_gain,
            arv_camera_get_gain_bounds,
            arv_camera_get_gain_increment,
            arv_camera_get_gain,
            arv_camera_set_gain
        );
    } else if (key == SDLK_m) {
        c->fast = !c->fast;
        apply_frame_rate(c);
    } else if (key == SDLK_x) {
        previous_roi = c->roi;
        if (c->roi == 0)
            next_roi = 1;
        else if (c->roi == 1)
            next_roi = 2;
        else
            next_roi = 0;
        c->roi = next_roi;
        if (!apply_roi(c))
            c->roi = previous_roi;
        else
            set_auto_mode(c, c->auto_mode);
        apply_frame_rate(c);
    } else {
        return 0;
    }

    refresh_status(c);
    printf("%s\n", c->status);
    fflush(stdout);
    return 1;
}

const char *flea3_controls_status(const Flea3Controls *c)
{
    if (c == NULL)
        return "";
    return c->status;
}
