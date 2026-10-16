#ifndef VIEW_UI_H
#define VIEW_UI_H

#include <stdbool.h>

#include "../sim/briefing.h"
#include "raylib.h"
#include "scene.h"

typedef struct {
    int selected;
    int hovered;
    bool follow_log;
    bool restart_allowed;
    bool intro;
} Ui;

bool ui_init(int width, int height);
void ui_close(void);
Font ui_font(void);
bool ui_lineup_box(int c, Rectangle *box);
void ui_update(Ui *ui, const Battle *b, const Scene *scene, const Atlases *atlases, const Viewport *view, bool input_blocked);
void ui_draw(const Ui *ui, const Battle *b, const GameData *data, const Scene *scene, const Viewport *view,
             const char (*laya_briefings)[BRIEFING_MAX + 1]);

#endif
