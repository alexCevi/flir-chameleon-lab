#ifndef FLEA3_CONTROLS_H
#define FLEA3_CONTROLS_H
#include <arv.h>
#include <SDL.h>
typedef struct Flea3Controls Flea3Controls;
Flea3Controls *flea3_controls_new(ArvCamera *camera);
void flea3_controls_free(Flea3Controls *c);
/* Return 1 if the key was consumed. */
int flea3_controls_handle_key(Flea3Controls *c, SDL_Keycode key, SDL_Keymod mod);
const char *flea3_controls_status(const Flea3Controls *c);
#endif
