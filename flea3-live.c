/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>

#include <arv.h>
#include <SDL.h>

#include "flea3-controls.h"
#include "flea3-input.h"
#include "flea3-save.h"
#include "flea3-view.h"

#define STREAM_BUFFERS 16
#define FIRST_FRAME_TIMEOUT_US 3000000

static volatile sig_atomic_t keep_running = 1;

static void handle_sigint(int sig)
{
    (void)sig;
    keep_running = 0;
}

static void die(const char *msg)
{
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}

static void die_gerror(const char *msg, GError *error)
{
    if (error != NULL) {
        fprintf(stderr, "ERROR: %s: %s\n", msg, error->message);
        g_error_free(error);
    } else {
        fprintf(stderr, "ERROR: %s\n", msg);
    }
    exit(1);
}

static const char *buffer_status_name(ArvBufferStatus status)
{
    switch (status) {
    case ARV_BUFFER_STATUS_SUCCESS:
        return "success";
    case ARV_BUFFER_STATUS_CLEARED:
        return "cleared";
    case ARV_BUFFER_STATUS_TIMEOUT:
        return "timeout";
    case ARV_BUFFER_STATUS_MISSING_PACKETS:
        return "missing-packets";
    case ARV_BUFFER_STATUS_WRONG_PACKET_ID:
        return "wrong-packet-id";
    case ARV_BUFFER_STATUS_SIZE_MISMATCH:
        return "size-mismatch";
    case ARV_BUFFER_STATUS_FILLING:
        return "filling";
    case ARV_BUFFER_STATUS_ABORTED:
        return "aborted";
    case ARV_BUFFER_STATUS_PAYLOAD_NOT_SUPPORTED:
        return "payload-not-supported";
    default:
        return "unknown";
    }
}

static int format_rank(const char *name)
{
    if (name == NULL)
        return 100;

    if (strcmp(name, "RGB8Packed") == 0 || strcmp(name, "RGB8") == 0)
        return 0;

    if (strcmp(name, "BGR8Packed") == 0 || strcmp(name, "BGR8") == 0)
        return 1;

    if (strcmp(name, "YUV422Packed") == 0 ||
        strcmp(name, "YUV422_8_UYVY") == 0 ||
        strcmp(name, "YUV422_YUYV_Packed") == 0 ||
        strcmp(name, "YUYV") == 0)
        return 2;

    if (strcmp(name, "BayerRG8") == 0)
        return 3;

    return 100;
}

static uint8_t sample_u8(const uint8_t *src, int stride, int width, int height, int x, int y)
{
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= width)
        x = width - 1;
    if (y >= height)
        y = height - 1;

    return src[(size_t)y * (size_t)stride + (size_t)x];
}

static void bayer_rg8_to_rgb(
    const uint8_t *src,
    int stride,
    int phase_x,
    int phase_y,
    uint8_t *dst,
    int width,
    int height,
    double *mean_out)
{
    uint64_t sum = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int px = (x + phase_x) & 1;
            int py = (y + phase_y) & 1;
            int r;
            int g;
            int b;

            if (py == 0 && px == 0) {
                r = sample_u8(src, stride, width, height, x, y);
                g = (
                    sample_u8(src, stride, width, height, x - 1, y) +
                    sample_u8(src, stride, width, height, x + 1, y) +
                    sample_u8(src, stride, width, height, x, y - 1) +
                    sample_u8(src, stride, width, height, x, y + 1)
                ) / 4;
                b = (
                    sample_u8(src, stride, width, height, x - 1, y - 1) +
                    sample_u8(src, stride, width, height, x + 1, y - 1) +
                    sample_u8(src, stride, width, height, x - 1, y + 1) +
                    sample_u8(src, stride, width, height, x + 1, y + 1)
                ) / 4;
            } else if (py == 0 && px == 1) {
                g = sample_u8(src, stride, width, height, x, y);
                r = (
                    sample_u8(src, stride, width, height, x - 1, y) +
                    sample_u8(src, stride, width, height, x + 1, y)
                ) / 2;
                b = (
                    sample_u8(src, stride, width, height, x, y - 1) +
                    sample_u8(src, stride, width, height, x, y + 1)
                ) / 2;
            } else if (py == 1 && px == 0) {
                g = sample_u8(src, stride, width, height, x, y);
                r = (
                    sample_u8(src, stride, width, height, x, y - 1) +
                    sample_u8(src, stride, width, height, x, y + 1)
                ) / 2;
                b = (
                    sample_u8(src, stride, width, height, x - 1, y) +
                    sample_u8(src, stride, width, height, x + 1, y)
                ) / 2;
            } else {
                b = sample_u8(src, stride, width, height, x, y);
                g = (
                    sample_u8(src, stride, width, height, x - 1, y) +
                    sample_u8(src, stride, width, height, x + 1, y) +
                    sample_u8(src, stride, width, height, x, y - 1) +
                    sample_u8(src, stride, width, height, x, y + 1)
                ) / 4;
                r = (
                    sample_u8(src, stride, width, height, x - 1, y - 1) +
                    sample_u8(src, stride, width, height, x + 1, y - 1) +
                    sample_u8(src, stride, width, height, x - 1, y + 1) +
                    sample_u8(src, stride, width, height, x + 1, y + 1)
                ) / 4;
            }

            size_t i = ((size_t)y * (size_t)width + (size_t)x) * 3;
            dst[i + 0] = (uint8_t)r;
            dst[i + 1] = (uint8_t)g;
            dst[i + 2] = (uint8_t)b;
            sum += (uint64_t)(r + g + b) / 3;
        }
    }

    if (mean_out != NULL && width > 0 && height > 0)
        *mean_out = (double)sum / ((double)width * (double)height);
}

static void packed_to_rgb(
    const uint8_t *src,
    int stride,
    int channels,
    int swap_rb,
    uint8_t *dst,
    int width,
    int height,
    double *mean_out)
{
    uint64_t sum = 0;

    for (int y = 0; y < height; y++) {
        const uint8_t *row = src + (size_t)y * (size_t)stride;

        for (int x = 0; x < width; x++) {
            const uint8_t *p = row + (size_t)x * (size_t)channels;
            uint8_t r = swap_rb ? p[2] : p[0];
            uint8_t g = p[1];
            uint8_t b = swap_rb ? p[0] : p[2];
            size_t i = ((size_t)y * (size_t)width + (size_t)x) * 3;

            dst[i + 0] = r;
            dst[i + 1] = g;
            dst[i + 2] = b;
            sum += (uint64_t)(r + g + b) / 3;
        }
    }

    if (mean_out != NULL && width > 0 && height > 0)
        *mean_out = (double)sum / ((double)width * (double)height);
}

static void yuv422_to_rgb(
    const uint8_t *src,
    int stride,
    bool yuyv,
    uint8_t *dst,
    int width,
    int height,
    double *mean_out)
{
    uint64_t sum = 0;

    for (int y = 0; y < height; y++) {
        const uint8_t *row = src + (size_t)y * (size_t)stride;

        for (int x = 0; x < width; x += 2) {
            int y0;
            int y1;
            int u;
            int v;

            if (yuyv) {
                y0 = row[x * 2 + 0];
                u = row[x * 2 + 1];
                y1 = row[x * 2 + 2];
                v = row[x * 2 + 3];
            } else {
                u = row[x * 2 + 0];
                y0 = row[x * 2 + 1];
                v = row[x * 2 + 2];
                y1 = row[x * 2 + 3];
            }

            for (int k = 0; k < 2 && x + k < width; k++) {
                int yy = k == 0 ? y0 : y1;
                int c = yy - 16;
                int d = u - 128;
                int e = v - 128;
                int r = (298 * c + 409 * e + 128) >> 8;
                int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int b = (298 * c + 516 * d + 128) >> 8;

                if (r < 0) r = 0;
                if (g < 0) g = 0;
                if (b < 0) b = 0;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;

                size_t i = ((size_t)y * (size_t)width + (size_t)(x + k)) * 3;
                dst[i + 0] = (uint8_t)r;
                dst[i + 1] = (uint8_t)g;
                dst[i + 2] = (uint8_t)b;
                sum += (uint64_t)(r + g + b) / 3;
            }
        }
    }

    if (mean_out != NULL && width > 0 && height > 0)
        *mean_out = (double)sum / ((double)width * (double)height);
}

static const char *choose_pixel_format(ArvCamera *camera, GError **error)
{
    guint n = 0;
    const char **formats =
        arv_camera_dup_available_pixel_formats_as_strings(camera, &n, error);
    const char *best = NULL;
    int best_rank = 100;

    if (formats == NULL)
        return NULL;

    printf("Pixel formats:");
    for (guint i = 0; i < n; i++) {
        int rank;

        printf(" %s", formats[i]);
        rank = format_rank(formats[i]);
        if (rank < best_rank) {
            best_rank = rank;
            best = formats[i];
        }
    }
    printf("\n");

    if (best != NULL) {
        best = g_strdup(best);
        g_free(formats);
        arv_camera_set_pixel_format_from_string(camera, best, error);
        if (*error != NULL) {
            g_free((char *)best);
            return NULL;
        }
        return best;
    }

    g_free(formats);
    best = arv_camera_get_pixel_format_as_string(camera, error);
    if (best != NULL)
        best = g_strdup(best);
    return best;
}

static ArvStream *open_stream(ArvCamera *camera, ArvUvUsbMode mode, GError **error)
{
    guint payload;
    ArvStream *stream;

    if (arv_camera_is_uv_device(camera))
        arv_camera_uv_set_usb_mode(camera, mode);

    payload = arv_camera_get_payload(camera, error);
    if (*error != NULL || payload == 0)
        return NULL;

    stream = arv_camera_create_stream(camera, NULL, NULL, error);
    if (stream == NULL)
        return NULL;

    for (int i = 0; i < STREAM_BUFFERS; i++)
        arv_stream_push_buffer(stream, arv_buffer_new_allocate(payload));

    arv_camera_start_acquisition(camera, error);
    if (*error != NULL) {
        g_object_unref(stream);
        return NULL;
    }

    return stream;
}

static ArvBuffer *pop_first_image(ArvStream *stream)
{
    uint64_t start = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    ArvBufferStatus last = ARV_BUFFER_STATUS_UNKNOWN;

    for (;;) {
        ArvBuffer *buffer = arv_stream_timeout_pop_buffer(stream, 500000);
        double elapsed =
            (double)(SDL_GetPerformanceCounter() - start) / (double)freq;

        if (buffer != NULL) {
            last = arv_buffer_get_status(buffer);
            if (last == ARV_BUFFER_STATUS_SUCCESS &&
                arv_buffer_get_payload_type(buffer) == ARV_BUFFER_PAYLOAD_TYPE_IMAGE &&
                arv_buffer_get_image_width(buffer) > 0 &&
                arv_buffer_get_image_height(buffer) > 0) {
                return buffer;
            }

            fprintf(stderr, "Skipping buffer (%s)\n", buffer_status_name(last));
            arv_stream_push_buffer(stream, buffer);
        }

        if (elapsed * 1000000.0 >= FIRST_FRAME_TIMEOUT_US)
            break;
    }

    fprintf(stderr, "No image yet (last status: %s)\n", buffer_status_name(last));
    return NULL;
}

/* Acquisition is already stopped and the previous stream released. */
static ArvStream *reopen_for_region(
    ArvCamera *camera,
    ArvUvUsbMode mode,
    gint *x,
    gint *y,
    gint *width,
    gint *height,
    uint8_t **rgb,
    SDL_Window *window,
    SDL_Texture **texture,
    int *tex_w,
    int *tex_h)
{
    GError *error = NULL;
    gint nx;
    gint ny;
    gint nw;
    gint nh;
    uint8_t *next;
    ArvStream *stream;

    nx = *x;
    ny = *y;
    nw = *width;
    nh = *height;
    arv_camera_get_region(camera, &nx, &ny, &nw, &nh, &error);
    if (error == NULL && nw >= 2 && nh >= 2 && nw <= 4096 && nh <= 4096) {
        *x = nx;
        *y = ny;
        *width = nw;
        *height = nh;
    }
    g_clear_error(&error);

    next = realloc(*rgb, (size_t)(*width) * (size_t)(*height) * 3);
    if (next == NULL)
        die("Unable to allocate the display buffer");
    *rgb = next;

    if (*texture != NULL) {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
        *tex_w = 0;
        *tex_h = 0;
    }

    if (window != NULL && !flea3_input_fullscreen())
        SDL_SetWindowSize(window, *width, *height);

    stream = open_stream(camera, mode, &error);
    if (stream == NULL)
        die_gerror("USB stream", error);

    printf("Frame size: %dx%d\n", *width, *height);
    fflush(stdout);
    return stream;
}

int main(void)
{
    GError *error = NULL;
    ArvCamera *camera;
    const char *vendor;
    const char *model;
    const char *chosen;
    gint x = 0;
    gint y = 0;
    gint width = 0;
    gint height = 0;
    ArvStream *stream = NULL;
    ArvBuffer *held = NULL;
    ArvUvUsbMode mode = ARV_UV_USB_MODE_SYNC;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    uint8_t *rgb = NULL;
    Flea3View *view = NULL;
    Flea3Controls *controls = NULL;
    bool running = true;
    bool warned_black = false;
    bool save_next = false;
    int applied_fullscreen = 0;
    int tex_w = 0;
    int tex_h = 0;
    uint64_t frame_count = 0;
    uint64_t fps_counter = 0;
    double current_fps = 0.0;
    uint64_t perf_frequency;
    uint64_t fps_start;

    signal(SIGINT, handle_sigint);

    printf("Initializing Aravis USB3 Vision...\n");

    camera = arv_camera_new(NULL, &error);
    if (camera == NULL)
        die_gerror("No USB3 Vision camera", error);

    vendor = arv_camera_get_vendor_name(camera, &error);
    if (error != NULL)
        die_gerror("Camera vendor", error);

    model = arv_camera_get_model_name(camera, &error);
    if (error != NULL)
        die_gerror("Camera model", error);

    printf("Camera: %s %s\n", vendor, model);
    fflush(stdout);

    arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS, &error);
    if (error != NULL)
        g_clear_error(&error);

    arv_camera_set_exposure_time_auto(camera, ARV_AUTO_CONTINUOUS, &error);
    if (error != NULL)
        g_clear_error(&error);

    arv_camera_set_gain_auto(camera, ARV_AUTO_CONTINUOUS, &error);
    if (error != NULL)
        g_clear_error(&error);

    chosen = choose_pixel_format(camera, &error);
    if (chosen == NULL)
        die_gerror("Pixel format", error);

    printf("Using pixel format: %s\n", chosen);

    arv_camera_get_region(camera, &x, &y, &width, &height, &error);
    if (error != NULL)
        die_gerror("Frame region", error);

    if (width != 1280 || height != 960) {
        arv_camera_set_region(camera, 0, 0, 1280, 960, &error);
        if (error != NULL) {
            g_clear_error(&error);
        } else {
            x = 0;
            y = 0;
            width = 1280;
            height = 960;
        }
    }

    printf("Frame size: %dx%d\n", width, height);
    fflush(stdout);

    if (width < 2 || height < 2 || width > 4096 || height > 4096)
        die("Unexpected frame size");

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        die(SDL_GetError());

    stream = open_stream(camera, ARV_UV_USB_MODE_SYNC, &error);
    if (stream == NULL)
        die_gerror("USB stream", error);

    printf("USB mode: sync\n");
    held = pop_first_image(stream);
    if (held == NULL) {
        fprintf(stderr, "Sync mode produced no frame. Trying async.\n");
        arv_camera_stop_acquisition(camera, NULL);
        g_object_unref(stream);
        stream = NULL;
        mode = ARV_UV_USB_MODE_ASYNC;
        stream = open_stream(camera, mode, &error);
        if (stream == NULL)
            die_gerror("USB stream", error);
        printf("USB mode: async\n");
        held = pop_first_image(stream);
        if (held == NULL)
            die("Camera did not deliver a frame");
    }

    window = SDL_CreateWindow(
        "Point Grey Flea3",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    if (window == NULL)
        die(SDL_GetError());

    SDL_RaiseWindow(window);

    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (renderer == NULL)
        renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL)
        die(SDL_GetError());

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );
    if (texture == NULL)
        die(SDL_GetError());
    tex_w = width;
    tex_h = height;

    rgb = malloc((size_t)width * (size_t)height * 3);
    if (rgb == NULL)
        die("Unable to allocate the display buffer");

    controls = flea3_controls_new(camera);
    if (controls == NULL)
        die("Unable to create camera controls");

    view = flea3_view_new();
    if (view == NULL)
        die("Unable to create the view");

    fputs(flea3_help_text(), stdout);
    fflush(stdout);

    printf("Viewer window is open. Q or Esc quits.\n");
    fflush(stdout);

    perf_frequency = SDL_GetPerformanceFrequency();
    fps_start = SDL_GetPerformanceCounter();

    while (running && keep_running) {
        SDL_Event event;
        ArvBuffer *buffer;
        const uint8_t *image;
        size_t image_size = 0;
        gint frame_w;
        gint frame_h;
        gint x_padding = 0;
        gint y_padding = 0;
        int stride;
        double mean = 0.0;
        ArvPixelFormat pixel_format;
        const char *pixel_name;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                running = false;

            if (event.type == SDL_MOUSEWHEEL)
                flea3_view_handle_wheel(view, event.wheel.y);

            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod mod = event.key.keysym.mod;

                if (flea3_input_is_quit(key)) {
                    running = false;
                } else if (flea3_input_handle_key(key, mod)) {
                    int fullscreen = flea3_input_fullscreen();

                    if (fullscreen != applied_fullscreen) {
                        if (SDL_SetWindowFullscreen(
                                window,
                                fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
                            fprintf(stderr, "Fullscreen: %s\n", SDL_GetError());
                        }
                        applied_fullscreen = fullscreen;
                    }
                } else if (key == SDLK_x) {
                    /* Width and height cannot change while the stream is running. */
                    if (held != NULL) {
                        arv_stream_push_buffer(stream, held);
                        held = NULL;
                    }
                    arv_camera_stop_acquisition(camera, NULL);
                    g_object_unref(stream);
                    stream = NULL;
                    flea3_controls_handle_key(controls, key, mod);
                    stream = reopen_for_region(
                        camera,
                        mode,
                        &x,
                        &y,
                        &width,
                        &height,
                        &rgb,
                        window,
                        &texture,
                        &tex_w,
                        &tex_h
                    );
                } else if (flea3_controls_handle_key(controls, key, mod)) {
                } else if (flea3_view_handle_key(view, key, mod)) {
                } else if (key == SDLK_s) {
                    save_next = true;
                }
            }
        }

        if (!running || !keep_running)
            break;

        if (held != NULL) {
            buffer = held;
            held = NULL;
        } else {
            buffer = arv_stream_timeout_pop_buffer(stream, 200000);
            if (buffer == NULL)
                continue;

            if (arv_buffer_get_status(buffer) != ARV_BUFFER_STATUS_SUCCESS) {
                arv_stream_push_buffer(stream, buffer);
                continue;
            }
        }

        frame_w = arv_buffer_get_image_width(buffer);
        frame_h = arv_buffer_get_image_height(buffer);
        if (frame_w != width || frame_h != height) {
            arv_stream_push_buffer(stream, buffer);
            continue;
        }

        image = arv_buffer_get_image_data(buffer, &image_size);
        arv_buffer_get_image_padding(buffer, &x_padding, &y_padding);
        pixel_format = arv_buffer_get_image_pixel_format(buffer);
        pixel_name = chosen;
        (void)y_padding;

        if (image == NULL) {
            arv_stream_push_buffer(stream, buffer);
            continue;
        }

        if (pixel_format == ARV_PIXEL_FORMAT_RGB_8_PACKED) {
            stride = width * 3 + x_padding;
            packed_to_rgb(image, stride, 3, 0, rgb, width, height, &mean);
        } else if (pixel_format == ARV_PIXEL_FORMAT_BGR_8_PACKED) {
            stride = width * 3 + x_padding;
            packed_to_rgb(image, stride, 3, 1, rgb, width, height, &mean);
        } else if (pixel_format == ARV_PIXEL_FORMAT_YUV_422_YUYV_PACKED) {
            stride = width * 2 + x_padding;
            yuv422_to_rgb(image, stride, true, rgb, width, height, &mean);
        } else if (pixel_format == ARV_PIXEL_FORMAT_YUV_422_PACKED) {
            stride = width * 2 + x_padding;
            yuv422_to_rgb(image, stride, false, rgb, width, height, &mean);
        } else if (pixel_format == ARV_PIXEL_FORMAT_BAYER_RG_8) {
            stride = width + x_padding;
            bayer_rg8_to_rgb(image, stride, x & 1, y & 1, rgb, width, height, &mean);
        } else {
            fprintf(stderr, "Unsupported pixel format %s\n", pixel_name);
            arv_stream_push_buffer(stream, buffer);
            running = false;
            continue;
        }

        if (save_next && !flea3_input_paused()) {
            const uint8_t *raw = rgb;
            int channels = 3;

            if (pixel_format == ARV_PIXEL_FORMAT_RGB_8_PACKED && x_padding == 0)
                raw = image;

            flea3_save_frame(raw, width, height, channels);
            save_next = false;
        }

        {
            uint8_t *displayed = NULL;
            int disp_w = 0;
            int disp_h = 0;
            int rendered;

            rendered = flea3_view_render(
                view,
                rgb,
                width,
                height,
                &displayed,
                &disp_w,
                &disp_h
            );
            if (!rendered || displayed == NULL) {
                displayed = rgb;
                disp_w = width;
                disp_h = height;
                rendered = 0;
            }

            if (texture == NULL || tex_w != disp_w || tex_h != disp_h) {
                if (texture != NULL)
                    SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(
                    renderer,
                    SDL_PIXELFORMAT_RGB24,
                    SDL_TEXTUREACCESS_STREAMING,
                    disp_w,
                    disp_h
                );
                if (texture == NULL)
                    die(SDL_GetError());
                tex_w = disp_w;
                tex_h = disp_h;
            }

            SDL_UpdateTexture(texture, NULL, displayed, disp_w * 3);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            if (rendered)
                free(displayed);
        }

        frame_count++;
        fps_counter++;

        if (!warned_black && frame_count == 15 && mean < 2.0) {
            printf("Picture is black (mean %.2f). Check the lens cap and the light.\n", mean);
            warned_black = true;
        }

        {
            uint64_t now = SDL_GetPerformanceCounter();
            double elapsed = (double)(now - fps_start) / (double)perf_frequency;

            if (elapsed >= 1.0) {
                char title[1024];
                const char *view_status = flea3_view_status(view);
                const char *controls_status = flea3_controls_status(controls);

                current_fps = (double)fps_counter / elapsed;
                fps_counter = 0;
                fps_start = now;

                if (flea3_input_stats_visible()) {
                    printf(
                        "frame=%llu  fps=%.2f  size=%dx%d  format=%s  mean=%.2f  %s  %s\n",
                        (unsigned long long)frame_count,
                        current_fps,
                        width,
                        height,
                        pixel_name,
                        mean,
                        view_status,
                        controls_status
                    );
                    fflush(stdout);
                }

                snprintf(
                    title,
                    sizeof(title),
                    "%s %s | %dx%d | %.1f FPS | %s | %s",
                    vendor,
                    model,
                    width,
                    height,
                    current_fps,
                    view_status,
                    controls_status
                );
                SDL_SetWindowTitle(window, title);
            }
        }

        arv_stream_push_buffer(stream, buffer);
    }

    printf("\nStopping camera...\n");

    arv_camera_stop_acquisition(camera, NULL);
    if (stream != NULL)
        g_object_unref(stream);
    flea3_view_free(view);
    flea3_controls_free(controls);
    g_object_unref(camera);
    g_free((char *)chosen);
    free(rgb);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("Camera closed cleanly.\n");
    return 0;
}
