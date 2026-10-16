#ifndef FLEA3_SAVE_H
#define FLEA3_SAVE_H
#include <stdint.h>
/* channels is 1 or 3. Returns 0 on success, -1 on error. Prints the filename, nothing else sensitive. */
int flea3_save_frame(const uint8_t *pixels, int w, int h, int channels);
#endif
