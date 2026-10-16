#ifndef VIEW_DRAW_H
#define VIEW_DRAW_H

#include <stdbool.h>

#include "raylib.h"
#include "atlas.h"
#include "scene.h"
#include "ui.h"

typedef struct {
    Texture2D parts[MAX_PARTS];
    Texture2D pickups[MAX_ARENA_OBJECTS];
    Texture2D traps[MAX_ARENA_OBJECTS];
    Texture2D effects[ELEMENT_COUNT];
    Texture2D floor;
    Texture2D rock;
    Texture2D pillar;
} Sheets;

#define TOAST_SIZE 320
#define TOAST_SECONDS 2.0

typedef struct {
    bool paused;
    bool confirm_quit;
    char toast[TOAST_SIZE];
    double toast_until;
} Hud;

void hud_show_toast(Hud *hud, const char *text);
bool sheets_load(Sheets *sheets, const GameData *data, const Atlases *atlases, char *err, size_t err_size);
void sheets_unload(Sheets *sheets);
void draw_frame_contents(const Battle *b, const GameData *data, const Scene *scene, const Atlases *atlases,
                         const Sheets *sheets, const Viewport *view, const Hud *hud, const Ui *ui,
                         const char (*laya_briefings)[BRIEFING_MAX + 1]);

#endif
