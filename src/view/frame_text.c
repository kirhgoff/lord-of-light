#include "frame_text.h"

#include <stdarg.h>
#include <stdio.h>

void frame_text_reset(FrameText *text)
{
    text->used = 0;
}

const char *frame_text_vformat(FrameText *text, int *length, const char *format, va_list args)
{
    int room = FRAME_TEXT_SIZE - text->used - 1;
    if (room <= 0) {
        text->bytes[FRAME_TEXT_SIZE - 1] = '\0';
        *length = 0;
        return &text->bytes[FRAME_TEXT_SIZE - 1];
    }
    char *out = &text->bytes[text->used];
    int written = vsnprintf(out, (size_t)room + 1, format, args);
    if (written < 0) written = 0;
    if (written > room) written = room;
    text->used += written + 1;
    *length = written;
    return out;
}

const char *frame_text_format(FrameText *text, int *length, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const char *out = frame_text_vformat(text, length, format, args);
    va_end(args);
    return out;
}
