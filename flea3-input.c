/* SPDX-License-Identifier: MIT */

#include "flea3-input.h"

/*
 * Quit is SDLK_q and SDLK_ESCAPE. flea3_input_is_quit reports them.
 * This handler does not consume them, so the caller can still test the key.
 *
 * Pass key-down codes only. Chameleon acts on SDL_KEYDOWN.
 * F toggles fullscreen. The caller applies SDL_WINDOW_FULLSCREEN_DESKTOP
 * when flea3_input_fullscreen() is non-zero, and clears it otherwise.
 * There is no pause key and no help-overlay key.
 */

static int fullscreen_on;
static int stats_on = 1;

static const char HELP_TEXT[] =
    "CAMERA MODES\n"
    " B            Toggle MONO8 / MONO16 acquisition\n"
    " M            Toggle FULL / FAST acquisition\n"
    " X            Hardware ROI: OFF -> 640x480 -> 320x240\n"
    "\n"
    "VIEW\n"
    " C            Crosshair on/off\n"
    " + / = / KP+  Digital zoom in\n"
    " - / KP-      Digital zoom out\n"
    " 1            Reset digital zoom\n"
    " Mouse wheel  Digital zoom\n"
    " R            Rotate clockwise 90 degrees\n"
    " L            Rotate counter-clockwise 90 degrees\n"
    " H            Horizontal mirror\n"
    " V            Vertical mirror\n"
    " F            Fullscreen\n"
    "\n"
    "EXPOSURE\n"
    " A            Toggle AUTO / MANUAL\n"
    " UP           Increase shutter\n"
    " DOWN         Decrease shutter\n"
    " RIGHT        Increase gain\n"
    " LEFT         Decrease gain\n"
    " 0            Restore AUTO\n"
    "\n"
    "OTHER\n"
    " S            Save the acquired frame before zoom, rotation, mirror, and the crosshair: flea3-NNNN.ppm for RGB8, flea3-NNNN.pgm for mono\n"
    " I            Toggle terminal statistics\n"
    " Q / ESC      Quit\n";

int flea3_input_is_quit(SDL_Keycode key)
{
    return key == SDLK_q || key == SDLK_ESCAPE;
}

int flea3_input_handle_key(SDL_Keycode key, SDL_Keymod mod)
{
    (void)mod;

    if (key == SDLK_f) {
        fullscreen_on = !fullscreen_on;
        return 1;
    }

    if (key == SDLK_i) {
        stats_on = !stats_on;
        return 1;
    }

    return 0;
}

int flea3_input_help_visible(void)
{
    return 0;
}

int flea3_input_paused(void)
{
    return 0;
}

int flea3_input_fullscreen(void)
{
    return fullscreen_on;
}

const char *flea3_help_text(void)
{
    return HELP_TEXT;
}

int flea3_input_stats_visible(void)
{
    return stats_on;
}
