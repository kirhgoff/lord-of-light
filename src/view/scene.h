#ifndef VIEW_SCENE_H
#define VIEW_SCENE_H

#include <stdbool.h>

#include "../sim/battle.h"
#include "../sim/movement.h"
#include "atlas.h"

#define ZOOM_MIN 1
#define ZOOM_MAX 4
#define ZOOM_DEFAULT 2
#define PAN_SCREEN_PX_PER_SECOND 600

typedef struct {
    float centre_x;
    float centre_y;
    int zoom;
    int width;
    int height;
} Viewport;

void viewport_init(Viewport *v, int width, int height);
void viewport_clamp(Viewport *v);
void viewport_pan(Viewport *v, float dx_screen_px, float dy_screen_px);
void viewport_zoom(Viewport *v, int step);
float viewport_screen_x(const Viewport *v, float world_x);
float viewport_screen_y(const Viewport *v, float world_y);

#endif
