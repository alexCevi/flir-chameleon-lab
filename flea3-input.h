#ifndef FLEA3_INPUT_H
#define FLEA3_INPUT_H
#include <SDL.h>
int flea3_input_is_quit(SDL_Keycode key);
/* Help, fullscreen, pause, and any remaining non-view non-control keys. Return 1 if consumed. */
int flea3_input_handle_key(SDL_Keycode key, SDL_Keymod mod);
int flea3_input_help_visible(void);
int flea3_input_paused(void);
int flea3_input_fullscreen(void);
/* Multiline help. Static storage. */
const char *flea3_help_text(void);
/* I toggles terminal statistics. Starts on, matching chameleon-live. */
int flea3_input_stats_visible(void);
#endif
