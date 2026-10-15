#ifndef FLEA3_VIEW_H
#define FLEA3_VIEW_H
#include <SDL.h>
#include <stdint.h>
typedef struct Flea3View Flea3View;
Flea3View *flea3_view_new(void);
void flea3_view_free(Flea3View *v);
/* Return 1 if this key was consumed. Zoom, rotate, mirror, and crosshair only. */
int flea3_view_handle_key(Flea3View *v, SDL_Keycode key, SDL_Keymod mod);
/* Mouse wheel. y > 0 zooms in, y < 0 zooms out. Return 1 if consumed. */
int flea3_view_handle_wheel(Flea3View *v, int y);
/* src is RGB8 w*h. On success malloc *out RGB8 of *out_w by *out_h (caller frees) and return 1. */
int flea3_view_render(Flea3View *v, const uint8_t *src, int w, int h, uint8_t **out, int *out_w, int *out_h);
/* One short status phrase, e.g. zoom/rotation/mirror/crosshair, no GUID. */
const char *flea3_view_status(const Flea3View *v);
#endif
