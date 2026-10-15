/* SPDX-License-Identifier: MIT */

#include "flea3-save.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FLEA3_SNAPSHOT_DIGITS 4
#define FLEA3_SNAPSHOT_MAX 9999

static int parse_snapshot_name(const char *name, int *index)
{
    const char *digits;
    const char *ext;
    int n = 0;
    int i;

    if (strncmp(name, "flea3-", 6) != 0)
        return 0;

    digits = name + 6;
    for (i = 0; i < FLEA3_SNAPSHOT_DIGITS; i++) {
        if (digits[i] < '0' || digits[i] > '9')
            return 0;
        n = n * 10 + (digits[i] - '0');
    }

    ext = digits + FLEA3_SNAPSHOT_DIGITS;
    if (strcmp(ext, ".pgm") != 0 && strcmp(ext, ".ppm") != 0)
        return 0;

    *index = n;
    return 1;
}

/* Smallest unused flea3-NNNN index. Either extension occupies the number. */
static int next_snapshot_index(void)
{
    unsigned char used[FLEA3_SNAPSHOT_MAX + 1];
    DIR *dir;
    struct dirent *ent;
    int i;

    dir = opendir(".");
    if (dir == NULL)
        return -1;

    memset(used, 0, sizeof used);
    while ((ent = readdir(dir)) != NULL) {
        int index;

        if (parse_snapshot_name(ent->d_name, &index))
            used[index] = 1;
    }
    closedir(dir);

    for (i = 0; i <= FLEA3_SNAPSHOT_MAX; i++) {
        if (!used[i])
            return i;
    }
    return -1;
}

static int pixels_ok(int w, int h, int channels)
{
    if (w <= 0 || h <= 0)
        return -1;
    if (channels != 1 && channels != 3)
        return -1;
    return 0;
}

static int frame_nbytes(int w, int h, int channels, size_t *nbytes)
{
    size_t width;
    size_t height;
    size_t count;

    width = (size_t)w;
    height = (size_t)h;
    if (height > SIZE_MAX / width)
        return -1;

    count = width * height;
    if ((size_t)channels > SIZE_MAX / count)
        return -1;

    *nbytes = count * (size_t)channels;
    return 0;
}

int flea3_save_frame(const uint8_t *pixels, int w, int h, int channels)
{
    size_t nbytes;
    int index;
    const char *ext;
    const char *magic;
    char filename[32];

    if (pixels == NULL || pixels_ok(w, h, channels) != 0)
        return -1;
    if (frame_nbytes(w, h, channels, &nbytes) != 0)
        return -1;

    index = next_snapshot_index();
    if (index < 0)
        return -1;

    ext = (channels == 1) ? "pgm" : "ppm";
    magic = (channels == 1) ? "P5" : "P6";

    for (;;) {
        int fd;
        FILE *fp;
        int written;

        if (index > FLEA3_SNAPSHOT_MAX)
            return -1;

        written = snprintf(
            filename,
            sizeof filename,
            "flea3-%04d.%s",
            index,
            ext
        );
        if (written < 0 || (size_t)written >= sizeof filename)
            return -1;

        fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd < 0) {
            if (errno == EEXIST || errno == EISDIR) {
                index++;
                continue;
            }
            perror("flea3_save_frame");
            return -1;
        }

        fp = fdopen(fd, "wb");
        if (fp == NULL) {
            close(fd);
            unlink(filename);
            perror("flea3_save_frame");
            return -1;
        }

        written = fprintf(fp, "%s\n%d %d\n255\n", magic, w, h);
        if (written >= 0 && fwrite(pixels, 1, nbytes, fp) != nbytes)
            written = -1;
        if (fclose(fp) != 0)
            written = -1;
        if (written < 0) {
            int saved = errno;

            unlink(filename);
            errno = saved != 0 ? saved : EIO;
            perror("flea3_save_frame");
            return -1;
        }

        printf("Saved snapshot: %s\n", filename);
        fflush(stdout);
        return 0;
    }
}
