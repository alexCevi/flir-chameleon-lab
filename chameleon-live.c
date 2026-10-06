/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>

#include <dc1394/dc1394.h>
#include <dc1394/conversions.h>
#include <SDL.h>

#define SENSOR_WIDTH  1280
#define SENSOR_HEIGHT 960

#define MAX_PIXELS (SENSOR_WIDTH * SENSOR_HEIGHT)

#define SHUTTER_STEP 25
#define GAIN_STEP    10

#define MIN_ZOOM  1.0
#define MAX_ZOOM  8.0
#define ZOOM_STEP 0.5

typedef enum
{
    ACQ_FULL = 0,
    ACQ_FAST = 1
} AcquisitionMode;

typedef enum
{
    ROI_OFF = 0,
    ROI_640 = 1,
    ROI_320 = 2
} RoiMode;

typedef struct
{
    bool mono16;

    AcquisitionMode acquisition;
    RoiMode roi;

    uint32_t width;
    uint32_t height;

} CameraState;

static volatile sig_atomic_t keep_running = 1;


/* ------------------------------------------------------------
 * Utility
 * ------------------------------------------------------------ */

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

static uint32_t clamp_u32(
    uint32_t value,
    uint32_t min,
    uint32_t max
)
{
    if (value < min)
        return min;

    if (value > max)
        return max;

    return value;
}

static double clamp_double(
    double value,
    double min,
    double max
)
{
    if (value < min)
        return min;

    if (value > max)
        return max;

    return value;
}


/* ------------------------------------------------------------
 * Controls
 * ------------------------------------------------------------ */

static void print_controls(void)
{
    printf("\n");
    printf("============================================================\n");
    printf(" TELEDYNE FLIR CHAMELEON CMLN-13S2M LAB VIEWER\n");
    printf("============================================================\n");
    printf("\n");

    printf("CAMERA MODES\n");
    printf(" B            Toggle MONO8 / MONO16 acquisition\n");
    printf(" M            Toggle FULL / FAST acquisition\n");
    printf(" X            Hardware ROI: OFF -> 640x480 -> 320x240\n");

    printf("\nVIEW\n");
    printf(" C            Crosshair on/off\n");
    printf(" + / =        Digital zoom in\n");
    printf(" -            Digital zoom out\n");
    printf(" 1            Reset digital zoom\n");
    printf(" Mouse wheel  Digital zoom\n");
    printf(" R            Rotate clockwise 90 degrees\n");
    printf(" L            Rotate counter-clockwise 90 degrees\n");
    printf(" H            Horizontal mirror\n");
    printf(" V            Vertical mirror\n");
    printf(" F            Fullscreen\n");

    printf("\nEXPOSURE\n");
    printf(" A            Toggle AUTO / MANUAL\n");
    printf(" UP           Increase shutter\n");
    printf(" DOWN         Decrease shutter\n");
    printf(" RIGHT        Increase gain\n");
    printf(" LEFT         Decrease gain\n");
    printf(" 0            Restore AUTO\n");

    printf("\nOTHER\n");
    printf(" S            Save displayed frame as PGM\n");
    printf(" I            Toggle terminal statistics\n");
    printf(" Q / ESC      Quit\n");

    printf("\n");
    printf("============================================================\n");
    printf("\n");
}


/* ------------------------------------------------------------
 * Exposure control
 * ------------------------------------------------------------ */

static void set_auto_mode(
    dc1394camera_t *camera,
    bool enabled
)
{
    dc1394feature_mode_t mode =
        enabled
        ? DC1394_FEATURE_MODE_AUTO
        : DC1394_FEATURE_MODE_MANUAL;

    dc1394_feature_set_mode(
        camera,
        DC1394_FEATURE_SHUTTER,
        mode
    );

    dc1394_feature_set_mode(
        camera,
        DC1394_FEATURE_GAIN,
        mode
    );

    dc1394_feature_set_mode(
        camera,
        DC1394_FEATURE_EXPOSURE,
        mode
    );

    printf(
        "Exposure: %s\n",
        enabled ? "AUTO" : "MANUAL"
    );
}


/* ------------------------------------------------------------
 * Mode names
 * ------------------------------------------------------------ */

static const char *bit_depth_name(
    const CameraState *state
)
{
    return state->mono16
        ? "MONO16"
        : "MONO8";
}

static const char *acquisition_name(
    const CameraState *state
)
{
    if (state->roi == ROI_640)
        return "HW-ROI 640x480";

    if (state->roi == ROI_320)
        return "HW-ROI 320x240";

    if (state->acquisition == ACQ_FAST)
        return "FAST 640x480";

    return "FULL 1280x960";
}

static void print_camera_state(
    const CameraState *state
)
{
    printf("\n");
    printf("------------------------------------------------------------\n");
    printf(" CAMERA MODE\n");
    printf(" Acquisition : %s\n", acquisition_name(state));
    printf(" Bit depth   : %s\n", bit_depth_name(state));
    printf(" Frame size  : %ux%u\n", state->width, state->height);

    if (state->roi != ROI_OFF)
    {
        printf(" ROI          : HARDWARE / FORMAT7 MODE 0\n");
    }

    printf("------------------------------------------------------------\n");
    printf("\n");
}


/* ------------------------------------------------------------
 * Configure camera
 * ------------------------------------------------------------ */

static dc1394error_t configure_camera(
    dc1394camera_t *camera,
    CameraState *state,
    bool restart
)
{
    dc1394error_t err;

    if (restart)
    {
        dc1394_video_set_transmission(
            camera,
            DC1394_OFF
        );

        dc1394_capture_stop(camera);
    }

    if (state->roi != ROI_OFF)
    {
        dc1394video_mode_t mode =
            DC1394_VIDEO_MODE_FORMAT7_0;

        dc1394color_coding_t coding =
            state->mono16
            ? DC1394_COLOR_CODING_MONO16
            : DC1394_COLOR_CODING_MONO8;

        uint32_t roi_w;
        uint32_t roi_h;

        if (state->roi == ROI_640)
        {
            roi_w = 640;
            roi_h = 480;
        }
        else
        {
            roi_w = 320;
            roi_h = 240;
        }

        err = dc1394_video_set_mode(
            camera,
            mode
        );

        if (err != DC1394_SUCCESS)
        {
            fprintf(stderr, "Could not set Format7 Mode 0\n");
            return err;
        }

        uint32_t max_w = SENSOR_WIDTH;
        uint32_t max_h = SENSOR_HEIGHT;

        dc1394_format7_get_max_image_size(
            camera,
            mode,
            &max_w,
            &max_h
        );

        uint32_t unit_pos_x = 8;
        uint32_t unit_pos_y = 2;

        dc1394_format7_get_unit_position(
            camera,
            mode,
            &unit_pos_x,
            &unit_pos_y
        );

        if (unit_pos_x == 0)
            unit_pos_x = 1;

        if (unit_pos_y == 0)
            unit_pos_y = 1;

        uint32_t pos_x =
            (max_w - roi_w) / 2;

        uint32_t pos_y =
            (max_h - roi_h) / 2;

        pos_x =
            (pos_x / unit_pos_x) *
            unit_pos_x;

        pos_y =
            (pos_y / unit_pos_y) *
            unit_pos_y;

        printf(
            "Setting hardware ROI: %ux%u @ (%u,%u)\n",
            roi_w,
            roi_h,
            pos_x,
            pos_y
        );

        err = dc1394_format7_set_roi(
            camera,
            mode,
            coding,
            DC1394_USE_MAX_AVAIL,
            pos_x,
            pos_y,
            roi_w,
            roi_h
        );

        if (err != DC1394_SUCCESS)
        {
            fprintf(
                stderr,
                "Could not configure Format7 hardware ROI\n"
            );

            return err;
        }

        state->width = roi_w;
        state->height = roi_h;
    }
    else
    {
        dc1394video_mode_t mode;
        dc1394framerate_t framerate;

        if (state->acquisition == ACQ_FULL)
        {
            state->width = 1280;
            state->height = 960;

            if (state->mono16)
            {
                mode =
                    DC1394_VIDEO_MODE_1280x960_MONO16;

                framerate =
                    DC1394_FRAMERATE_7_5;
            }
            else
            {
                mode =
                    DC1394_VIDEO_MODE_1280x960_MONO8;

                framerate =
                    DC1394_FRAMERATE_15;
            }
        }
        else
        {
            state->width = 640;
            state->height = 480;

            if (state->mono16)
            {
                mode =
                    DC1394_VIDEO_MODE_640x480_MONO16;
            }
            else
            {
                mode =
                    DC1394_VIDEO_MODE_640x480_MONO8;
            }

            framerate =
                DC1394_FRAMERATE_30;
        }

        err = dc1394_video_set_mode(
            camera,
            mode
        );

        if (err != DC1394_SUCCESS)
        {
            fprintf(stderr, "Could not set video mode\n");
            return err;
        }

        err = dc1394_video_set_framerate(
            camera,
            framerate
        );

        if (err != DC1394_SUCCESS)
        {
            fprintf(
                stderr,
                "Could not set requested frame rate\n"
            );

            return err;
        }
    }

    err = dc1394_capture_setup(
        camera,
        8,
        DC1394_CAPTURE_FLAGS_DEFAULT
    );

    if (err != DC1394_SUCCESS)
    {
        fprintf(
            stderr,
            "dc1394_capture_setup failed\n"
        );

        return err;
    }

    err = dc1394_video_set_transmission(
        camera,
        DC1394_ON
    );

    if (err != DC1394_SUCCESS)
    {
        fprintf(
            stderr,
            "Could not start camera transmission\n"
        );

        dc1394_capture_stop(camera);

        return err;
    }

    print_camera_state(state);

    return DC1394_SUCCESS;
}


/* ------------------------------------------------------------
 * MONO16 -> MONO8 display conversion
 * ------------------------------------------------------------ */

static void frame_to_mono8(
    dc1394video_frame_t *frame,
    uint8_t *output
)
{
    uint32_t width =
        frame->size[0];

    uint32_t height =
        frame->size[1];

    size_t pixels =
        (size_t)width *
        (size_t)height;

    if (
        frame->color_coding ==
        DC1394_COLOR_CODING_MONO8
    )
    {
        memcpy(
            output,
            frame->image,
            pixels
        );

        return;
    }

    dc1394error_t err =
        dc1394_convert_to_MONO8(
            frame->image,
            output,
            width,
            height,
            frame->yuv_byte_order,
            frame->color_coding,
            frame->data_depth
        );

    if (err == DC1394_SUCCESS)
        return;

    for (size_t i = 0; i < pixels; i++)
    {
        uint16_t value;

        if (frame->little_endian)
        {
            value =
                ((uint16_t)frame->image[i * 2 + 1] << 8) |
                ((uint16_t)frame->image[i * 2 + 0]);
        }
        else
        {
            value =
                ((uint16_t)frame->image[i * 2 + 0] << 8) |
                ((uint16_t)frame->image[i * 2 + 1]);
        }

        uint32_t bits =
            frame->data_depth;

        if (bits < 8)
            bits = 8;

        if (bits > 16)
            bits = 16;

        output[i] =
            (uint8_t)(
                value >>
                (bits - 8)
            );
    }
}


/* ------------------------------------------------------------
 * Snapshot
 * ------------------------------------------------------------ */

static void save_snapshot(
    const uint8_t *mono,
    uint32_t width,
    uint32_t height,
    uint32_t number
)
{
    char filename[256];

    snprintf(
        filename,
        sizeof(filename),
        "chameleon-%04u.pgm",
        number
    );

    FILE *fp =
        fopen(filename, "wb");

    if (!fp)
    {
        perror("fopen");
        return;
    }

    fprintf(
        fp,
        "P5\n%u %u\n255\n",
        width,
        height
    );

    fwrite(
        mono,
        1,
        (size_t)width *
        (size_t)height,
        fp
    );

    fclose(fp);

    printf(
        "Saved snapshot: %s\n",
        filename
    );
}


/* ------------------------------------------------------------
 * Digital zoom
 * ------------------------------------------------------------ */

static void calculate_zoom_rect(
    double zoom,
    uint32_t width,
    uint32_t height,
    SDL_Rect *src
)
{
    int crop_w =
        (int)((double)width / zoom);

    int crop_h =
        (int)((double)height / zoom);

    if (crop_w < 1)
        crop_w = 1;

    if (crop_h < 1)
        crop_h = 1;

    src->w = crop_w;
    src->h = crop_h;

    src->x =
        ((int)width - crop_w) / 2;

    src->y =
        ((int)height - crop_h) / 2;
}


/* ------------------------------------------------------------
 * Crosshair
 * ------------------------------------------------------------ */

static void draw_crosshair(
    SDL_Renderer *renderer,
    const SDL_Rect *destination
)
{
    int cx =
        destination->x +
        destination->w / 2;

    int cy =
        destination->y +
        destination->h / 2;

    int arm = 40;
    int gap = 7;

    SDL_SetRenderDrawColor(
        renderer,
        0,
        0,
        0,
        255
    );

    SDL_RenderDrawLine(
        renderer,
        cx - arm - 1,
        cy + 1,
        cx - gap - 1,
        cy + 1
    );

    SDL_RenderDrawLine(
        renderer,
        cx + gap - 1,
        cy + 1,
        cx + arm - 1,
        cy + 1
    );

    SDL_RenderDrawLine(
        renderer,
        cx + 1,
        cy - arm - 1,
        cx + 1,
        cy - gap - 1
    );

    SDL_RenderDrawLine(
        renderer,
        cx + 1,
        cy + gap - 1,
        cx + 1,
        cy + arm - 1
    );

    SDL_SetRenderDrawColor(
        renderer,
        255,
        255,
        255,
        255
    );

    SDL_RenderDrawLine(
        renderer,
        cx - arm,
        cy,
        cx - gap,
        cy
    );

    SDL_RenderDrawLine(
        renderer,
        cx + gap,
        cy,
        cx + arm,
        cy
    );

    SDL_RenderDrawLine(
        renderer,
        cx,
        cy - arm,
        cx,
        cy - gap
    );

    SDL_RenderDrawLine(
        renderer,
        cx,
        cy + gap,
        cx,
        cy + arm
    );

    SDL_Rect center_box;

    center_box.x = cx - 3;
    center_box.y = cy - 3;
    center_box.w = 7;
    center_box.h = 7;

    SDL_RenderDrawRect(
        renderer,
        &center_box
    );
}


/* ------------------------------------------------------------
 * Recreate SDL texture after hardware mode change
 * ------------------------------------------------------------ */

static SDL_Texture *create_texture(
    SDL_Renderer *renderer,
    uint32_t width,
    uint32_t height
)
{
    SDL_Texture *texture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height
        );

    if (!texture)
    {
        fprintf(
            stderr,
            "SDL_CreateTexture: %s\n",
            SDL_GetError()
        );
    }

    return texture;
}


/* ------------------------------------------------------------
 * MAIN
 * ------------------------------------------------------------ */

int main(void)
{
    signal(
        SIGINT,
        handle_sigint
    );

    dc1394_t *dc = NULL;
    dc1394camera_list_t *list = NULL;
    dc1394camera_t *camera = NULL;
    dc1394video_frame_t *frame = NULL;

    dc1394error_t err;

    printf(
        "Initializing libdc1394...\n"
    );

    dc =
        dc1394_new();

    if (!dc)
        die("dc1394_new failed");

    err =
        dc1394_camera_enumerate(
            dc,
            &list
        );

    if (err != DC1394_SUCCESS)
        die("Camera enumeration failed");

    if (list->num == 0)
        die("No cameras found");

    camera =
        dc1394_camera_new(
            dc,
            list->ids[0].guid
        );

    if (!camera)
        die("Unable to open camera");

    printf(
        "Camera: %s %s\n",
        camera->vendor,
        camera->model
    );

    printf(
        "GUID: 0x%016llx\n",
        (unsigned long long)
            camera->guid
    );

    dc1394_camera_free_list(list);
    list = NULL;

    CameraState state;

    state.mono16 = false;
    state.acquisition = ACQ_FULL;
    state.roi = ROI_OFF;
    state.width = 1280;
    state.height = 960;

    dc1394_feature_set_power(
        camera,
        DC1394_FEATURE_EXPOSURE,
        DC1394_ON
    );

    bool auto_mode = true;

    set_auto_mode(
        camera,
        auto_mode
    );

    uint32_t shutter_min = 0;
    uint32_t shutter_max = 1077;

    uint32_t gain_min = 160;
    uint32_t gain_max = 842;

    dc1394_feature_get_boundaries(
        camera,
        DC1394_FEATURE_SHUTTER,
        &shutter_min,
        &shutter_max
    );

    dc1394_feature_get_boundaries(
        camera,
        DC1394_FEATURE_GAIN,
        &gain_min,
        &gain_max
    );

    err =
        configure_camera(
            camera,
            &state,
            false
        );

    if (err != DC1394_SUCCESS)
        die("Initial camera setup failed");

    if (
        SDL_Init(SDL_INIT_VIDEO) != 0
    )
    {
        die(SDL_GetError());
    }

    SDL_Window *window =
        SDL_CreateWindow(
            "Teledyne FLIR Chameleon CMLN-13S2M",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            SENSOR_WIDTH,
            SENSOR_HEIGHT,
            SDL_WINDOW_RESIZABLE
        );

    if (!window)
        die(SDL_GetError());

    SDL_Renderer *renderer =
        SDL_CreateRenderer(
            window,
            -1,
            SDL_RENDERER_ACCELERATED |
            SDL_RENDERER_PRESENTVSYNC
        );

    if (!renderer)
    {
        renderer =
            SDL_CreateRenderer(
                window,
                -1,
                0
            );
    }

    if (!renderer)
        die(SDL_GetError());

    SDL_Texture *texture =
        create_texture(
            renderer,
            state.width,
            state.height
        );

    if (!texture)
        die("Could not create texture");

    uint8_t *mono =
        malloc(MAX_PIXELS);

    uint8_t *rgb =
        malloc(MAX_PIXELS * 3);

    if (!mono || !rgb)
        die("Unable to allocate image buffers");

    print_controls();

    bool running = true;
    bool show_stats = true;
    bool save_next_frame = false;
    bool fullscreen = false;
    bool flip_h = false;
    bool flip_v = false;
    bool crosshair_enabled = true;

    double angle = 0.0;
    double zoom = 1.0;

    uint32_t snapshot_number = 0;

    uint64_t frame_count = 0;
    uint64_t fps_counter = 0;
    double current_fps = 0.0;

    uint64_t perf_frequency =
        SDL_GetPerformanceFrequency();

    uint64_t fps_start =
        SDL_GetPerformanceCounter();

    while (
        running &&
        keep_running
    )
    {
        SDL_Event event;

        while (
            SDL_PollEvent(&event)
        )
        {
            if (
                event.type ==
                SDL_QUIT
            )
            {
                running = false;
            }

            if (
                event.type ==
                SDL_MOUSEWHEEL
            )
            {
                if (
                    event.wheel.y > 0
                )
                {
                    zoom += ZOOM_STEP;
                }
                else if (
                    event.wheel.y < 0
                )
                {
                    zoom -= ZOOM_STEP;
                }

                zoom =
                    clamp_double(
                        zoom,
                        MIN_ZOOM,
                        MAX_ZOOM
                    );

                printf(
                    "Digital zoom: %.1fx\n",
                    zoom
                );
            }

            if (
                event.type !=
                SDL_KEYDOWN
            )
            {
                continue;
            }

            SDL_Keycode key =
                event.key.keysym.sym;

            if (
                key == SDLK_ESCAPE ||
                key == SDLK_q
            )
            {
                running = false;
            }

            else if (
                key == SDLK_b
            )
            {
                bool old =
                    state.mono16;

                state.mono16 =
                    !state.mono16;

                printf(
                    "\nSwitching acquisition to %s...\n",
                    bit_depth_name(&state)
                );

                err =
                    configure_camera(
                        camera,
                        &state,
                        true
                    );

                if (
                    err !=
                    DC1394_SUCCESS
                )
                {
                    fprintf(
                        stderr,
                        "Bit-depth switch failed. Restoring previous mode.\n"
                    );

                    state.mono16 = old;

                    configure_camera(
                        camera,
                        &state,
                        false
                    );
                }

                set_auto_mode(
                    camera,
                    auto_mode
                );

                SDL_DestroyTexture(
                    texture
                );

                texture =
                    create_texture(
                        renderer,
                        state.width,
                        state.height
                    );

                zoom = 1.0;
            }

            else if (
                key == SDLK_m
            )
            {
                state.roi =
                    ROI_OFF;

                state.acquisition =
                    state.acquisition ==
                    ACQ_FULL
                    ? ACQ_FAST
                    : ACQ_FULL;

                printf(
                    "\nSwitching acquisition mode...\n"
                );

                err =
                    configure_camera(
                        camera,
                        &state,
                        true
                    );

                if (
                    err !=
                    DC1394_SUCCESS
                )
                {
                    fprintf(
                        stderr,
                        "Mode switch failed\n"
                    );

                    running = false;
                    continue;
                }

                set_auto_mode(
                    camera,
                    auto_mode
                );

                SDL_DestroyTexture(
                    texture
                );

                texture =
                    create_texture(
                        renderer,
                        state.width,
                        state.height
                    );

                zoom = 1.0;
            }

            else if (
                key == SDLK_x
            )
            {
                if (
                    state.roi ==
                    ROI_OFF
                )
                {
                    state.roi =
                        ROI_640;
                }
                else if (
                    state.roi ==
                    ROI_640
                )
                {
                    state.roi =
                        ROI_320;
                }
                else
                {
                    state.roi =
                        ROI_OFF;
                }

                printf(
                    "\nSwitching hardware ROI...\n"
                );

                err =
                    configure_camera(
                        camera,
                        &state,
                        true
                    );

                if (
                    err !=
                    DC1394_SUCCESS
                )
                {
                    fprintf(
                        stderr,
                        "ROI switch failed. Returning to standard acquisition.\n"
                    );

                    state.roi =
                        ROI_OFF;

                    err =
                        configure_camera(
                            camera,
                            &state,
                            false
                        );

                    if (
                        err !=
                        DC1394_SUCCESS
                    )
                    {
                        running = false;
                        continue;
                    }
                }

                set_auto_mode(
                    camera,
                    auto_mode
                );

                SDL_DestroyTexture(
                    texture
                );

                texture =
                    create_texture(
                        renderer,
                        state.width,
                        state.height
                    );

                zoom = 1.0;
            }

            else if (
                key == SDLK_c
            )
            {
                crosshair_enabled =
                    !crosshair_enabled;

                printf(
                    "Crosshair: %s\n",
                    crosshair_enabled
                    ? "ON"
                    : "OFF"
                );
            }

            else if (
                key == SDLK_PLUS ||
                key == SDLK_EQUALS ||
                key == SDLK_KP_PLUS
            )
            {
                zoom += ZOOM_STEP;

                zoom =
                    clamp_double(
                        zoom,
                        MIN_ZOOM,
                        MAX_ZOOM
                    );

                printf(
                    "Digital zoom: %.1fx\n",
                    zoom
                );
            }

            else if (
                key == SDLK_MINUS ||
                key == SDLK_KP_MINUS
            )
            {
                zoom -= ZOOM_STEP;

                zoom =
                    clamp_double(
                        zoom,
                        MIN_ZOOM,
                        MAX_ZOOM
                    );

                printf(
                    "Digital zoom: %.1fx\n",
                    zoom
                );
            }

            else if (
                key == SDLK_1
            )
            {
                zoom = 1.0;

                printf(
                    "Digital zoom reset\n"
                );
            }

            else if (
                key == SDLK_r
            )
            {
                angle += 90.0;

                if (
                    angle >= 360.0
                )
                {
                    angle = 0.0;
                }

                printf(
                    "Rotation: %.0f degrees\n",
                    angle
                );
            }

            else if (
                key == SDLK_l
            )
            {
                angle -= 90.0;

                if (
                    angle < 0.0
                )
                {
                    angle = 270.0;
                }

                printf(
                    "Rotation: %.0f degrees\n",
                    angle
                );
            }

            else if (
                key == SDLK_h
            )
            {
                flip_h =
                    !flip_h;
            }

            else if (
                key == SDLK_v
            )
            {
                flip_v =
                    !flip_v;
            }

            else if (
                key == SDLK_f
            )
            {
                fullscreen =
                    !fullscreen;

                SDL_SetWindowFullscreen(
                    window,
                    fullscreen
                    ? SDL_WINDOW_FULLSCREEN_DESKTOP
                    : 0
                );
            }

            else if (
                key == SDLK_s
            )
            {
                save_next_frame = true;
            }

            else if (
                key == SDLK_i
            )
            {
                show_stats =
                    !show_stats;
            }

            else if (
                key == SDLK_a
            )
            {
                auto_mode =
                    !auto_mode;

                set_auto_mode(
                    camera,
                    auto_mode
                );
            }

            else if (
                key == SDLK_0
            )
            {
                auto_mode = true;

                set_auto_mode(
                    camera,
                    true
                );
            }

            else if (
                key == SDLK_UP
            )
            {
                if (auto_mode)
                {
                    auto_mode = false;

                    set_auto_mode(
                        camera,
                        false
                    );
                }

                uint32_t shutter = 0;

                dc1394_feature_get_value(
                    camera,
                    DC1394_FEATURE_SHUTTER,
                    &shutter
                );

                uint64_t proposed =
                    (uint64_t)shutter +
                    SHUTTER_STEP;

                if (
                    proposed >
                    shutter_max
                )
                {
                    proposed =
                        shutter_max;
                }

                shutter =
                    clamp_u32(
                        (uint32_t)proposed,
                        shutter_min,
                        shutter_max
                    );

                dc1394_feature_set_value(
                    camera,
                    DC1394_FEATURE_SHUTTER,
                    shutter
                );
            }

            else if (
                key == SDLK_DOWN
            )
            {
                if (auto_mode)
                {
                    auto_mode = false;

                    set_auto_mode(
                        camera,
                        false
                    );
                }

                uint32_t shutter = 0;

                dc1394_feature_get_value(
                    camera,
                    DC1394_FEATURE_SHUTTER,
                    &shutter
                );

                if (
                    shutter >
                    shutter_min +
                    SHUTTER_STEP
                )
                {
                    shutter -=
                        SHUTTER_STEP;
                }
                else
                {
                    shutter =
                        shutter_min;
                }

                dc1394_feature_set_value(
                    camera,
                    DC1394_FEATURE_SHUTTER,
                    shutter
                );
            }

            else if (
                key == SDLK_RIGHT
            )
            {
                if (auto_mode)
                {
                    auto_mode = false;

                    set_auto_mode(
                        camera,
                        false
                    );
                }

                uint32_t gain = 0;

                dc1394_feature_get_value(
                    camera,
                    DC1394_FEATURE_GAIN,
                    &gain
                );

                uint64_t proposed =
                    (uint64_t)gain +
                    GAIN_STEP;

                if (
                    proposed >
                    gain_max
                )
                {
                    proposed =
                        gain_max;
                }

                gain =
                    clamp_u32(
                        (uint32_t)proposed,
                        gain_min,
                        gain_max
                    );

                dc1394_feature_set_value(
                    camera,
                    DC1394_FEATURE_GAIN,
                    gain
                );
            }

            else if (
                key == SDLK_LEFT
            )
            {
                if (auto_mode)
                {
                    auto_mode = false;

                    set_auto_mode(
                        camera,
                        false
                    );
                }

                uint32_t gain = 0;

                dc1394_feature_get_value(
                    camera,
                    DC1394_FEATURE_GAIN,
                    &gain
                );

                if (
                    gain >
                    gain_min +
                    GAIN_STEP
                )
                {
                    gain -=
                        GAIN_STEP;
                }
                else
                {
                    gain =
                        gain_min;
                }

                dc1394_feature_set_value(
                    camera,
                    DC1394_FEATURE_GAIN,
                    gain
                );
            }
        }

        frame = NULL;

        err =
            dc1394_capture_dequeue(
                camera,
                DC1394_CAPTURE_POLICY_WAIT,
                &frame
            );

        if (
            err != DC1394_SUCCESS ||
            !frame
        )
        {
            continue;
        }

        uint32_t width =
            frame->size[0];

        uint32_t height =
            frame->size[1];

        size_t pixels =
            (size_t)width *
            (size_t)height;

        if (
            pixels >
            MAX_PIXELS
        )
        {
            fprintf(
                stderr,
                "Unexpected frame size\n"
            );

            dc1394_capture_enqueue(
                camera,
                frame
            );

            continue;
        }

        frame_to_mono8(
            frame,
            mono
        );

        if (
            save_next_frame
        )
        {
            save_snapshot(
                mono,
                width,
                height,
                snapshot_number++
            );

            save_next_frame =
                false;
        }

        uint64_t sum = 0;
        uint8_t minv = 255;
        uint8_t maxv = 0;
        uint64_t focus_sum = 0;
        uint64_t focus_samples = 0;

        for (
            uint32_t y = 0;
            y < height;
            y++
        )
        {
            for (
                uint32_t x = 0;
                x < width;
                x++
            )
            {
                size_t i =
                    (size_t)y *
                    width +
                    x;

                uint8_t value =
                    mono[i];

                if (
                    value < minv
                )
                {
                    minv = value;
                }

                if (
                    value > maxv
                )
                {
                    maxv = value;
                }

                sum += value;

                if (
                    x > 0 &&
                    y > 0
                )
                {
                    int dx =
                        abs(
                            (int)value -
                            (int)mono[i - 1]
                        );

                    int dy =
                        abs(
                            (int)value -
                            (int)mono[
                                i - width
                            ]
                        );

                    focus_sum +=
                        (uint64_t)(
                            dx + dy
                        );

                    focus_samples++;
                }

                size_t ri =
                    i * 3;

                rgb[ri + 0] =
                    value;

                rgb[ri + 1] =
                    value;

                rgb[ri + 2] =
                    value;
            }
        }

        double mean =
            (double)sum /
            (double)pixels;

        double focus_score =
            focus_samples
            ? (double)focus_sum /
              (double)focus_samples
            : 0.0;

        SDL_UpdateTexture(
            texture,
            NULL,
            rgb,
            width * 3
        );

        SDL_Rect source_rect;

        calculate_zoom_rect(
            zoom,
            width,
            height,
            &source_rect
        );

        int window_w = 0;
        int window_h = 0;

        SDL_GetRendererOutputSize(
            renderer,
            &window_w,
            &window_h
        );

        int rotated_w =
            (((int)angle % 180) == 0)
            ? source_rect.w
            : source_rect.h;

        int rotated_h =
            (((int)angle % 180) == 0)
            ? source_rect.h
            : source_rect.w;

        double scale_x =
            (double)window_w /
            (double)rotated_w;

        double scale_y =
            (double)window_h /
            (double)rotated_h;

        double scale =
            scale_x < scale_y
            ? scale_x
            : scale_y;

        SDL_Rect destination;

        destination.w =
            (int)(
                rotated_w *
                scale
            );

        destination.h =
            (int)(
                rotated_h *
                scale
            );

        destination.x =
            (
                window_w -
                destination.w
            ) / 2;

        destination.y =
            (
                window_h -
                destination.h
            ) / 2;

        SDL_RendererFlip flip =
            SDL_FLIP_NONE;

        if (flip_h)
        {
            flip =
                (SDL_RendererFlip)(
                    flip |
                    SDL_FLIP_HORIZONTAL
                );
        }

        if (flip_v)
        {
            flip =
                (SDL_RendererFlip)(
                    flip |
                    SDL_FLIP_VERTICAL
                );
        }

        SDL_SetRenderDrawColor(
            renderer,
            0,
            0,
            0,
            255
        );

        SDL_RenderClear(
            renderer
        );

        SDL_RenderCopyEx(
            renderer,
            texture,
            &source_rect,
            &destination,
            angle,
            NULL,
            flip
        );

        if (
            crosshair_enabled
        )
        {
            draw_crosshair(
                renderer,
                &destination
            );
        }

        SDL_RenderPresent(
            renderer
        );

        frame_count++;
        fps_counter++;

        uint64_t now =
            SDL_GetPerformanceCounter();

        double elapsed =
            (double)(
                now -
                fps_start
            ) /
            (double)perf_frequency;

        if (
            elapsed >= 1.0
        )
        {
            current_fps =
                (double)fps_counter /
                elapsed;

            fps_counter = 0;

            fps_start =
                now;
        }

        uint32_t shutter = 0;
        uint32_t gain = 0;

        float shutter_abs = 0.0f;
        float gain_abs = 0.0f;

        dc1394_feature_get_value(
            camera,
            DC1394_FEATURE_SHUTTER,
            &shutter
        );

        dc1394_feature_get_value(
            camera,
            DC1394_FEATURE_GAIN,
            &gain
        );

        dc1394_feature_get_absolute_value(
            camera,
            DC1394_FEATURE_SHUTTER,
            &shutter_abs
        );

        dc1394_feature_get_absolute_value(
            camera,
            DC1394_FEATURE_GAIN,
            &gain_abs
        );

        if (
            frame_count % 8 ==
            0
        )
        {
            char title[768];

            snprintf(
                title,
                sizeof(title),

                "Chameleon | %s | %s | %ux%u | %.1f FPS | DZoom %.1fx | mean %.1f | focus %.1f | shutter %.2f ms | gain %.1f dB",

                acquisition_name(
                    &state
                ),

                bit_depth_name(
                    &state
                ),

                width,
                height,

                current_fps,

                zoom,

                mean,
                focus_score,

                shutter_abs *
                    1000.0f,

                gain_abs
            );

            SDL_SetWindowTitle(
                window,
                title
            );
        }

        if (
            show_stats &&
            frame_count % 30 ==
            0
        )
        {
            printf(
                "frame=%llu  "
                "fps=%.2f  "
                "acq=%s  "
                "depth=%s  "
                "size=%ux%u  "
                "dzoom=%.1fx  "
                "min=%u  "
                "max=%u  "
                "mean=%.2f  "
                "focus=%.2f  "
                "shutter=%.3fms(%u)  "
                "gain=%.2fdB(%u)  "
                "%s\n",

                (unsigned long long)
                    frame_count,

                current_fps,

                acquisition_name(
                    &state
                ),

                bit_depth_name(
                    &state
                ),

                width,
                height,

                zoom,

                minv,
                maxv,

                mean,
                focus_score,

                shutter_abs *
                    1000.0f,

                shutter,

                gain_abs,
                gain,

                auto_mode
                    ? "AUTO"
                    : "MANUAL"
            );

            fflush(stdout);
        }

        dc1394_capture_enqueue(
            camera,
            frame
        );

        frame = NULL;
    }

    printf(
        "\nStopping camera...\n"
    );

    dc1394_video_set_transmission(
        camera,
        DC1394_OFF
    );

    dc1394_capture_stop(
        camera
    );

    free(mono);
    free(rgb);

    SDL_DestroyTexture(
        texture
    );

    SDL_DestroyRenderer(
        renderer
    );

    SDL_DestroyWindow(
        window
    );

    SDL_Quit();

    dc1394_camera_free(
        camera
    );

    dc1394_free(
        dc
    );

    printf(
        "Camera closed cleanly.\n"
    );

    return 0;
}
