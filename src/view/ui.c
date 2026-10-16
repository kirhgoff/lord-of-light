#include "ui.h"

#include <stdio.h>
#include <stdlib.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "raylib.h"
#include "clay_renderer_raylib.c"

enum { FONT_BODY, FONT_COUNT };

static Font fonts[FONT_COUNT];
static void *clay_memory;

static Clay_Dimensions measure_text_utf8(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data)
{
    Font font = ((Font *)user_data)[config->fontId];
    float scale = (float)config->fontSize / (float)font.baseSize;
    float width = 0;
    int glyphs = 0;
    for (int i = 0; i < text.length;) {
        int size = 0;
        int index = GetGlyphIndex(font, GetCodepointNext(&text.chars[i], &size));
        width += font.glyphs[index].advanceX ? (float)font.glyphs[index].advanceX
                                             : font.recs[index].width + (float)font.glyphs[index].offsetX;
        i += size;
        glyphs++;
    }
    float spacing = glyphs > 0 ? (float)(glyphs - 1) * config->letterSpacing : 0;
    return (Clay_Dimensions){width * scale + spacing, (float)config->fontSize};
}

static void handle_clay_error(Clay_ErrorData error)
{
    fprintf(stderr, "clay: %.*s\n", error.errorText.length, error.errorText.chars);
    abort();
}

bool ui_init(int width, int height)
{
    fonts[FONT_BODY] = GetFontDefault();
    uint32_t bytes = Clay_MinMemorySize();
    clay_memory = malloc(bytes);
    if (!clay_memory) return false;
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(bytes, clay_memory), (Clay_Dimensions){(float)width, (float)height},
                    (Clay_ErrorHandler){handle_clay_error, NULL});
    Clay_SetMeasureTextFunction(measure_text_utf8, fonts);
    return true;
}

void ui_close(void)
{
    free(clay_memory);
    clay_memory = NULL;
}

void ui_update(Ui *ui, int width, int height)
{
    (void)ui;
    Clay_SetLayoutDimensions((Clay_Dimensions){(float)width, (float)height});
}

void ui_draw(const Ui *ui)
{
    (void)ui;
    Clay_BeginLayout();
    Clay_RenderCommandArray commands = Clay_EndLayout();
    Clay_Raylib_Render(commands, fonts);
}
