#include "ui.h"

#include <stdio.h>
#include <stdlib.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "raylib.h"
#include "clay_renderer_raylib.c"

#include "../sim/power.h"
#include "frame_text.h"
#include "report.h"

#define LOG_LINES 200
#define FONT_SMALL 16
#define FONT_PATH "assets/fonts/AtkinsonHyperlegible-Regular.ttf"
#define FONT_LOAD_SIZE 48
#define OVERLAY_LIFT_PX 48
#define OVERLAY_PADDING 3
#define OVERLAY_COMPACT_LINES 2
#define PANEL_ZINDEX 2
#define OVERLAY_ZINDEX 1
#define LOG_FOLLOW_SCROLL 1e9f
#define DETAIL_HEIGHT 360
#define DETAIL_PADDING 6
#define STATUS_TEXT_SIZE 96

#define FONT_BIG 32
#define END_SCREEN_WIDTH 820
#define DEATH_LOG_HEIGHT 260
#define LINEUP_SPRITE_WIDTH 120
#define PARTS_TEXT_SIZE 512

enum { FONT_BODY,
       FONT_COUNT };

static const Clay_Color LINEUP_BACKGROUND = {20, 18, 16, 255};
static const Clay_Color END_BACKGROUND = {0, 0, 0, 190};
static const Clay_Color PANEL_BACKGROUND = {16, 14, 12, 220};
static const Clay_Color OVERLAY_BACKGROUND = {0, 0, 0, 170};
static const Clay_Color OVERLAY_FOCUS_BACKGROUND = {70, 60, 10, 200};
static const Clay_Color DETAIL_BACKGROUND = {30, 26, 22, 230};
static const Clay_Color TEXT_COLOUR = {235, 235, 235, 255};
static const Clay_Color DIM_COLOUR = {140, 140, 140, 255};
static const Clay_Color RED_COLOUR = {240, 90, 80, 255};
static const Clay_Color GREEN_COLOUR = {120, 220, 120, 255};
static const Clay_Color YELLOW_COLOUR = {240, 210, 90, 255};
static const Clay_Color CYAN_COLOUR = {110, 220, 230, 255};
static const Clay_Color ORANGE_COLOUR = {240, 160, 80, 255};

static Font fonts[FONT_COUNT];
static void *clay_memory;
static FrameText frame_text;

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
    float spacing = (float)glyphs * (float)config->letterSpacing;
    return (Clay_Dimensions){width * scale + spacing, (float)config->fontSize};
}

static void handle_clay_error(Clay_ErrorData error)
{
    fprintf(stderr, "clay: %.*s\n", error.errorText.length, error.errorText.chars);
    abort();
}

static Clay_String frame_string(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length;
    const char *chars = frame_text_vformat(&frame_text, &length, format, args);
    va_end(args);
    return (Clay_String){.isStaticallyAllocated = false, .length = length, .chars = chars};
}

static void sized_text(Clay_String string, Clay_Color colour, int size)
{
    CLAY_TEXT(string, CLAY_TEXT_CONFIG({.fontId = FONT_BODY, .fontSize = (uint16_t)size, .letterSpacing = 1, .textColor = colour}));
}

static void text(Clay_String string, Clay_Color colour)
{
    sized_text(string, colour, FONT_SMALL);
}

bool ui_init(int width, int height)
{
    fonts[FONT_BODY] = LoadFontEx(FONT_PATH, FONT_LOAD_SIZE, NULL, 0);
    GenTextureMipmaps(&fonts[FONT_BODY].texture);
    SetTextureFilter(fonts[FONT_BODY].texture, TEXTURE_FILTER_TRILINEAR);
    uint32_t bytes = Clay_MinMemorySize();
    clay_memory = malloc(bytes);
    if (!clay_memory) return false;
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(bytes, clay_memory), (Clay_Dimensions){(float)width, (float)height},
                    (Clay_ErrorHandler){handle_clay_error, NULL});
    Clay_SetMeasureTextFunction(measure_text_utf8, fonts);
    return true;
}

Font ui_font(void)
{
    return fonts[FONT_BODY];
}

void ui_close(void)
{
    UnloadFont(fonts[FONT_BODY]);
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

static int detail_target(const Ui *ui, const Battle *b)
{
    int c = ui->hovered >= 0 ? ui->hovered : ui->selected;
    return c >= 0 && c < b->combatant_count ? c : -1;
}

static int pick_under_mouse(const Battle *b, const Scene *scene, const Atlases *atlases, const Viewport *view, Vector2 mouse)
{
    return scene_pick(scene, b, atlases, viewport_world_x(view, mouse.x), viewport_world_y(view, mouse.y));
}

void ui_update(Ui *ui, const Battle *b, const Scene *scene, const Atlases *atlases, const Viewport *view, bool input_blocked)
{
    frame_text_reset(&frame_text);
    Vector2 wheel = GetMouseWheelMoveV();
    Vector2 mouse = GetMousePosition();
    Clay_SetLayoutDimensions((Clay_Dimensions){(float)view->width, (float)view->height});
    Clay_SetPointerState((Clay_Vector2){mouse.x, mouse.y}, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    follow_log_tail(ui, wheel.y);
    Clay_UpdateScrollContainers(false, (Clay_Vector2){wheel.x, wheel.y}, GetFrameTime());
    bool picking = scene->xray && !input_blocked && !Clay_PointerOver(CLAY_ID("Panel")) && battle_winner(b) < 0;
    ui->hovered = picking ? pick_under_mouse(b, scene, atlases, view, mouse) : -1;
    if (picking && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ui->selected = ui->hovered;
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
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(6), .childGap = 2, .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
        for (int i = count - 1; i >= 0; i--) text(lines[i], colours[i]);
    }
}

static const char *element_name(int element)
{
    return element >= 0 && element < ELEMENT_COUNT ? element_id((Element)element) : "none";
}

static const char *const HURT_WORDS[HURT_COUNT] = {"unhurt", "wounded", "badly wounded", "near death"};

static void decision_lines(const Battle *b, int c)
{
    const Decision *decision = &b->combatants[c].decision;
    text(frame_string("Stance %s   %s   vulnerable to %s", stance_id(decision->stance), HURT_WORDS[decision->hurt_level],
                      element_name(decision->vulnerable_element)),
         TEXT_COLOUR);
    int last = battle_last_decision(b, c);
    if (last < 0) return;
    const Event *e = &b->log[last];
    text(frame_string("%s   fallback %s   confidence %d.%03d   answer %d ms", e->decider == DECIDER_LAYA ? "Laya" : "Reflex",
                      fallback_id(e->fallback), e->confidence_permille / 1000, e->confidence_permille % 1000, e->answer_ms),
         DIM_COLOUR);
}

static void briefing_text(const Battle *b, const GameData *data, int c, const char (*laya_briefings)[BRIEFING_MAX + 1])
{
    char live[BRIEFING_MAX + 1];
    const char *briefing = laya_briefings ? laya_briefings[c] : "";
    if (briefing[0] == '\0') {
        briefing_write(b, data, c, live, sizeof live);
        briefing = live;
    }
    text(frame_string("%s", briefing), TEXT_COLOUR);
}

static void detail(const Battle *b, const GameData *data, int c, const char (*laya_briefings)[BRIEFING_MAX + 1])
{
    char label[LABEL_SIZE];
    combatant_label(b, c, label, sizeof label);
    CLAY({.id = CLAY_ID("Detail"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(DETAIL_HEIGHT)}, .padding = CLAY_PADDING_ALL(DETAIL_PADDING), .childGap = 3, .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = DETAIL_BACKGROUND,
          .clip = {.vertical = true}}) {
        text(frame_string("%s%s", label, b->combatants[c].alive ? "" : " (dead)"), YELLOW_COLOUR);
        decision_lines(b, c);
        text(CLAY_STRING("Briefing"), DIM_COLOUR);
        briefing_text(b, data, c, laya_briefings);
    }
}

static void panel(const Ui *ui, const Battle *b, const GameData *data, const Viewport *view,
                  const char (*laya_briefings)[BRIEFING_MAX + 1])
{
    CLAY({.id = CLAY_ID("Panel"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(PANEL_WIDTH), CLAY_SIZING_FIXED((float)view->height)}, .padding = {0, 0, HUD_HEIGHT, 0}},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                       .zIndex = PANEL_ZINDEX}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = PANEL_BACKGROUND}) {
            battle_log(b, data);
            int target = detail_target(ui, b);
            if (target >= 0) detail(b, data, target, laya_briefings);
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
    Clay_Color colour = integrity <= 0 ? RED_COLOUR : working ? TEXT_COLOUR
                                                              : DIM_COLOUR;
    text(frame_string("%s %d/%d", part->name, integrity > 0 ? integrity : 0, part->integrity), colour);
}

static void overlay(const Battle *b, const GameData *data, const Scene *scene, const Viewport *view, int c, bool focused)
{
    const Combatant *who = &b->combatants[c];
    float x, y;
    scene_position(scene, b, c, &x, &y);
    char label[LABEL_SIZE];
    combatant_label(b, c, label, sizeof label);
    bool working[MAX_SLOTS];
    body_working_parts(&who->body, who->parts, working);
    int lines = focused ? who->body.slot_count + OVERLAY_COMPACT_LINES : OVERLAY_COMPACT_LINES;
    float height = (float)(lines * FONT_SMALL + 2 * OVERLAY_PADDING);
    float bottom = viewport_screen_y(view, y) - OVERLAY_LIFT_PX * (float)view->zoom;
    if (bottom < HUD_HEIGHT + height) bottom = HUD_HEIGHT + height;
    CLAY({.id = CLAY_IDI("Overlay", c),
          .layout = {.padding = CLAY_PADDING_ALL(OVERLAY_PADDING), .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = focused ? OVERLAY_FOCUS_BACKGROUND : OVERLAY_BACKGROUND,
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                       .offset = {viewport_screen_x(view, x), bottom},
                       .zIndex = OVERLAY_ZINDEX,
                       .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
        text(frame_string("%s  %d/%d", label, who->vitality, who->vitality_max), TEXT_COLOUR);
        if (focused)
            for (int s = 0; s < who->body.slot_count; s++) overlay_part_line(who, data, s, working[s]);
        text(stance_and_statuses(who), DIM_COLOUR);
    }
}

static void report_lines(const Battle *b, const GameData *data, const BattleReport *r)
{
    char who[LABEL_SIZE];
    if (r->top_dealer >= 0) combatant_label(b, r->top_dealer, who, sizeof who);
    text(r->top_dealer >= 0 ? frame_string("Top damage dealer: %s (%d damage)", who, r->top_dealer_damage)
                            : CLAY_STRING("Top damage dealer: nobody"),
         TEXT_COLOUR);
    text(r->destroyed_part >= 0 ? frame_string("Most destroyed Part: %s (%d times)", data->parts[r->destroyed_part].name, r->destroyed_count)
                                : CLAY_STRING("Most destroyed Part: none"),
         TEXT_COLOUR);
    if (r->last_to_fall >= 0) combatant_label(b, r->last_to_fall, who, sizeof who);
    text(r->last_to_fall >= 0 ? frame_string("Longest survivor to fall: %s at %d.%ds", who, r->last_fall_tick / TICKS_PER_SECOND,
                                             r->last_fall_tick % TICKS_PER_SECOND / 2)
                              : CLAY_STRING("Longest survivor to fall: nobody fell"),
         TEXT_COLOUR);
    text(frame_string("Pickups taken: %d", r->pickups), TEXT_COLOUR);
    text(r->top_element >= 0 ? frame_string("Top Element by damage: %s (%d)", element_name(r->top_element), r->top_element_damage)
                             : CLAY_STRING("Top Element by damage: none"),
         TEXT_COLOUR);
    const DecisionStats *d = &r->decisions;
    int total = d->laya + d->reflex;
    text(frame_string("Decisions: Laya %d (%d%%), Reflex %d (%d%%)", d->laya, total ? d->laya * 100 / total : 0, d->reflex,
                      total ? d->reflex * 100 / total : 0),
         TEXT_COLOUR);
    text(d->laya ? frame_string("Laya mean confidence %d.%03d, mean answer %d ms", d->mean_confidence_permille / 1000,
                                d->mean_confidence_permille % 1000, d->mean_answer_ms)
                 : CLAY_STRING("Laya mean confidence n/a"),
         TEXT_COLOUR);
}

static void death_log(const Battle *b, const GameData *data)
{
    CLAY({.id = CLAY_ID("DeathLog"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, DEATH_LOG_HEIGHT)}, .childGap = 2, .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
        char line[EVENT_LINE_SIZE];
        for (int e = 0; e < b->log_count; e++)
            if (b->log[e].kind == EVENT_DEATH && event_line(b, data, &b->log[e], line, sizeof line) > 0)
                text(frame_string("%s", line), RED_COLOUR);
    }
}

static void end_screen(const Ui *ui, const Battle *b, const GameData *data)
{
    BattleReport report = report_build(b);
    CLAY({.id = CLAY_ID("EndScreen"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = END_BACKGROUND}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(END_SCREEN_WIDTH), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 6, .layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = DETAIL_BACKGROUND}) {
            sized_text(report.winner == SIDE_PANTHEON ? CLAY_STRING("Pantheon wins") : CLAY_STRING("Horde wins"), YELLOW_COLOUR,
                       FONT_BIG);
            report_lines(b, data, &report);
            text(CLAY_STRING("Deaths"), DIM_COLOUR);
            death_log(b, data);
            if (ui->restart_allowed) text(frame_string("R: new Battle (seed %u)   Esc: quit", b->seed + 1), DIM_COLOUR);
            else text(CLAY_STRING("Esc: quit"), DIM_COLOUR);
        }
    }
}

static const char *deity_epithet(const Battle *b, const GameData *data, int c)
{
    const Pantheon *pantheon = &data->pantheons[b->pantheon];
    for (int d = 0; d < DEITIES_PER_PANTHEON; d++)
        if (strcmp(pantheon->deities[d].name, b->combatants[c].name) == 0) return pantheon->deities[d].epithet;
    return NULL;
}

static Clay_String parts_text(const Combatant *who, const GameData *data)
{
    char parts[PARTS_TEXT_SIZE] = "Parts:";
    for (int s = 0; s < who->body.slot_count; s++)
        snprintf(parts + strlen(parts), sizeof parts - strlen(parts), "%s %s", s ? "," : "", data->parts[who->body.slots[s].part].name);
    return frame_string("%s", parts);
}

static void lineup_entry(const Battle *b, const GameData *data, int c)
{
    const Combatant *who = &b->combatants[c];
    char label[LABEL_SIZE];
    combatant_label(b, c, label, sizeof label);
    const char *epithet = deity_epithet(b, data, c);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childGap = 12, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.id = CLAY_IDI("LineupSprite", c), .layout = {.sizing = {CLAY_SIZING_FIXED(LINEUP_SPRITE_WIDTH), CLAY_SIZING_GROW(0)}}}) {
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 2, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            text(frame_string("%s   Vitality %d   Power %ld", label, who->vitality_max, body_power(data, &who->body)), YELLOW_COLOUR);
            if (epithet) text(frame_string("%s", epithet), TEXT_COLOUR);
            text(parts_text(who, data), DIM_COLOUR);
        }
    }
}

static void lineup_side(const Battle *b, const GameData *data, Side side, Clay_String title)
{
    CLAY({.layout = {.sizing = {CLAY_SIZING_PERCENT(0.5f), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        sized_text(title, YELLOW_COLOUR, FONT_BIG);
        for (int c = 0; c < b->combatant_count; c++)
            if (b->combatants[c].side == side && b->combatants[c].spawner < 0) lineup_entry(b, data, c);
    }
}

static void lineup(const Battle *b, const GameData *data)
{
    CLAY({.id = CLAY_ID("Lineup"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {.x = CLAY_ALIGN_X_CENTER}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = LINEUP_BACKGROUND}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {
            lineup_side(b, data, SIDE_PANTHEON, frame_string("The %s Pantheon", data->pantheons[b->pantheon].name));
            lineup_side(b, data, SIDE_HORDE, CLAY_STRING("The Horde"));
        }
        text(CLAY_STRING("Enter / Space: begin the Battle   Esc: quit"), DIM_COLOUR);
    }
}

bool ui_lineup_box(int c, Rectangle *box)
{
    Clay_ElementData element = Clay_GetElementData(CLAY_IDI("LineupSprite", c));
    *box = (Rectangle){element.boundingBox.x, element.boundingBox.y, element.boundingBox.width, element.boundingBox.height};
    return element.found;
}

void ui_draw(const Ui *ui, const Battle *b, const GameData *data, const Scene *scene, const Viewport *view,
             const char (*laya_briefings)[BRIEFING_MAX + 1])
{
    Clay_BeginLayout();
    if (ui->intro) {
        lineup(b, data);
    } else if (battle_winner(b) >= 0) {
        end_screen(ui, b, data);
    } else if (scene->xray) {
        panel(ui, b, data, view, laya_briefings);
        for (int c = 0; c < b->combatant_count; c++)
            if (b->combatants[c].alive) overlay(b, data, scene, view, c, c == detail_target(ui, b));
    }
    Clay_RenderCommandArray commands = Clay_EndLayout();
    Clay_Raylib_Render(commands, fonts);
}
