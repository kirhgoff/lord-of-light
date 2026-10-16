#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "raylib.h"
#include "clay_renderer_raylib.c"

#include "report.h"

#define FRAME_TEXT_SIZE (64 * 1024)
#define PANEL_WIDTH 360
#define LOG_LINES 200
#define FONT_SMALL 10
#define OVERLAY_LIFT_PX 48
#define OVERLAY_PADDING 3
#define OVERLAY_EXTRA_LINES 2
#define OVERLAY_ZINDEX 1
#define LOG_FOLLOW_SCROLL 1e9f
#define STATUS_TEXT_SIZE 96

enum { FONT_BODY, FONT_COUNT };

static const Clay_Color PANEL_BACKGROUND = {16, 14, 12, 220};
static const Clay_Color OVERLAY_BACKGROUND = {0, 0, 0, 170};
static const Clay_Color TEXT_COLOUR = {235, 235, 235, 255};
static const Clay_Color DIM_COLOUR = {140, 140, 140, 255};
static const Clay_Color RED_COLOUR = {240, 90, 80, 255};
static const Clay_Color GREEN_COLOUR = {120, 220, 120, 255};
static const Clay_Color YELLOW_COLOUR = {240, 210, 90, 255};
static const Clay_Color CYAN_COLOUR = {110, 220, 230, 255};
static const Clay_Color ORANGE_COLOUR = {240, 160, 80, 255};

static Font fonts[FONT_COUNT];
static void *clay_memory;
static char frame_text[FRAME_TEXT_SIZE];
static int frame_text_used;

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
    float spacing = (float)glyphs * config->letterSpacing;
    return (Clay_Dimensions){width * scale + spacing, (float)config->fontSize};
}

static void handle_clay_error(Clay_ErrorData error)
{
    fprintf(stderr, "clay: %.*s\n", error.errorText.length, error.errorText.chars);
    abort();
}

static Clay_String frame_string(const char *format, ...)
{
    char *out = frame_text + frame_text_used;
    int capacity = FRAME_TEXT_SIZE - frame_text_used - 1;
    va_list args;
    va_start(args, format);
    int length = vsnprintf(out, (size_t)capacity + 1, format, args);
    va_end(args);
    if (length < 0) length = 0;
    if (length > capacity) length = capacity;
    frame_text_used += length + 1;
    return (Clay_String){.isStaticallyAllocated = false, .length = length, .chars = out};
}

static void text(Clay_String string, Clay_Color colour)
{
    CLAY_TEXT(string, CLAY_TEXT_CONFIG({.fontId = FONT_BODY, .fontSize = FONT_SMALL, .letterSpacing = 1, .textColor = colour}));
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

static void follow_log_tail(Ui *ui, float wheel_y)
{
    Clay_ScrollContainerData log = Clay_GetScrollContainerData(CLAY_ID("BattleLog"));
    if (!log.found) return;
    bool at_bottom = log.scrollPosition->y <= log.scrollContainerDimensions.height - log.contentDimensions.height + 1;
    if (wheel_y > 0 && Clay_PointerOver(CLAY_ID("BattleLog"))) ui->follow_log = false;
    else if (at_bottom) ui->follow_log = true;
    if (ui->follow_log) log.scrollPosition->y = -LOG_FOLLOW_SCROLL;
}

void ui_update(Ui *ui, const Viewport *view)
{
    frame_text_used = 0;
    Vector2 wheel = GetMouseWheelMoveV();
    Vector2 mouse = GetMousePosition();
    Clay_SetLayoutDimensions((Clay_Dimensions){(float)view->width, (float)view->height});
    Clay_SetPointerState((Clay_Vector2){mouse.x, mouse.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    follow_log_tail(ui, wheel.y);
    Clay_UpdateScrollContainers(false, (Clay_Vector2){wheel.x, wheel.y}, GetFrameTime());
}

static Clay_Color event_colour(const Event *e)
{
    switch (e->kind) {
    case EVENT_DEATH: return RED_COLOUR;
    case EVENT_PICKUP: return GREEN_COLOUR;
    case EVENT_TRAP: return YELLOW_COLOUR;
    case EVENT_SPAWN: return CYAN_COLOUR;
    case EVENT_EFFECT: return e->detail == EFFECT_HEAL ? GREEN_COLOUR : ORANGE_COLOUR;
    case EVENT_DECISION: return DIM_COLOUR;
    default: return TEXT_COLOUR;
    }
}

static int collect_log_lines(const Battle *b, const GameData *data, Clay_String *lines, Clay_Color *colours)
{
    int count = 0;
    char line[EVENT_LINE_SIZE];
    for (int e = b->log_count - 1; e >= 0 && count < LOG_LINES; e--) {
        int length = event_line(b, data, &b->log[e], line, sizeof line);
        if (length == 0) continue;
        lines[count] = frame_string("%s", line);
        colours[count++] = event_colour(&b->log[e]);
    }
    return count;
}

static void battle_log(const Battle *b, const GameData *data)
{
    Clay_String lines[LOG_LINES];
    Clay_Color colours[LOG_LINES];
    int count = collect_log_lines(b, data, lines, colours);
    CLAY({.id = CLAY_ID("BattleLog"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(6), .childGap = 2,
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
        for (int i = count - 1; i >= 0; i--) text(lines[i], colours[i]);
    }
}

static void panel(const Battle *b, const GameData *data)
{
    CLAY({.id = CLAY_ID("Panel"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(PANEL_WIDTH), CLAY_SIZING_GROW(0)}, .padding = {0, 0, HUD_HEIGHT, 0}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = PANEL_BACKGROUND}) {
            battle_log(b, data);
        }
    }
}

static void append_status(char *out, const char *name, int ticks)
{
    if (ticks > 0) snprintf(out + strlen(out), STATUS_TEXT_SIZE - strlen(out), " %s", name);
}

static Clay_String stance_and_statuses(const Combatant *who)
{
    char status[STATUS_TEXT_SIZE];
    snprintf(status, sizeof status, "%s", stance_id(who->decision.stance));
    append_status(status, "slowed", who->slow_ticks);
    append_status(status, "burning", who->dot_ticks);
    append_status(status, "charmed", who->charm_ticks);
    append_status(status, "empowered", who->damage_bonus_ticks);
    append_status(status, "hasted", who->haste_ticks);
    append_status(status, "far-sighted", who->vision_ticks);
    append_status(status, "warded", who->ward_ticks);
    return frame_string("%s", status);
}

static void overlay_part_line(const Combatant *who, const GameData *data, int slot, bool working)
{
    const Part *part = &data->parts[who->body.slots[slot].part];
    int integrity = who->parts[slot].integrity;
    Clay_Color colour = integrity <= 0 ? RED_COLOUR : working ? TEXT_COLOUR : DIM_COLOUR;
    text(frame_string("%s %d/%d", part->name, integrity > 0 ? integrity : 0, part->integrity), colour);
}

static void overlay(const Battle *b, const GameData *data, const Scene *scene, const Viewport *view, int c)
{
    const Combatant *who = &b->combatants[c];
    float x, y;
    scene_position(scene, b, c, &x, &y);
    char label[LABEL_SIZE];
    combatant_label(b, c, label, sizeof label);
    bool working[MAX_SLOTS];
    body_working_parts(&who->body, who->parts, working);
    float height = (float)((who->body.slot_count + OVERLAY_EXTRA_LINES) * FONT_SMALL + 2 * OVERLAY_PADDING);
    float bottom = viewport_screen_y(view, y) - OVERLAY_LIFT_PX * (float)view->zoom;
    if (bottom < HUD_HEIGHT + height) bottom = HUD_HEIGHT + height;
    CLAY({.id = CLAY_IDI("Overlay", c),
          .layout = {.padding = CLAY_PADDING_ALL(OVERLAY_PADDING), .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = OVERLAY_BACKGROUND,
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                       .offset = {viewport_screen_x(view, x), bottom},
                       .zIndex = OVERLAY_ZINDEX,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
        text(frame_string("%s  %d/%d", label, who->vitality, who->vitality_max), TEXT_COLOUR);
        for (int s = 0; s < who->body.slot_count; s++) overlay_part_line(who, data, s, working[s]);
        text(stance_and_statuses(who), DIM_COLOUR);
    }
}

void ui_draw(const Ui *ui, const Battle *b, const GameData *data, const Scene *scene, const Viewport *view)
{
    (void)ui;
    Clay_BeginLayout();
    if (scene->xray) {
        CLAY({.id = CLAY_ID("Root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT}}}) {
            panel(b, data);
        }
        for (int c = 0; c < b->combatant_count; c++)
            if (b->combatants[c].alive) overlay(b, data, scene, view, c);
    }
    Clay_RenderCommandArray commands = Clay_EndLayout();
    Clay_Raylib_Render(commands, fonts);
}
