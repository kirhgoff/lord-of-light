#ifndef VIEW_FRAME_TEXT_H
#define VIEW_FRAME_TEXT_H

#include <stdarg.h>

#define FRAME_TEXT_SIZE (64 * 1024)

typedef struct {
    char bytes[FRAME_TEXT_SIZE];
    int used;
} FrameText;

void frame_text_reset(FrameText *text);
const char *frame_text_vformat(FrameText *text, int *length, const char *format, va_list args);
const char *frame_text_format(FrameText *text, int *length, const char *format, ...);

#endif
