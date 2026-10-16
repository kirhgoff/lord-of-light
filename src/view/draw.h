#ifndef VIEW_DRAW_H
#define VIEW_DRAW_H

#include <stdbool.h>

#include "raylib.h"
#include "atlas.h"
#include "scene.h"

typedef struct {
    Texture2D parts[MAX_PARTS];
    Texture2D pickups[MAX_ARENA_OBJECTS];
    Texture2D traps[MAX_ARENA_OBJECTS];
    Texture2D effects[ELEMENT_COUNT];
    Texture2D floor;
    Texture2D rock;
    Texture2D pillar;
} Sheets;

typedef struct {
    bool paused;
    bool confirm_quit;
} Hud;

void sheets_load(Sheets *sheets, const GameData *data);
void sheets_unload(Sheets *sheets);
void draw_frame_contents(const Battle *b, const GameData *data, const Scene *scene, const Atlases *atlases,
                         const Sheets *sheets, const Viewport *view, const Hud *hud);

#endif
