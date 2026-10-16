#include "scene.h"

#define ARENA_WIDTH_PX (ARENA_WIDTH_TILES * TILE_SIZE)
#define ARENA_HEIGHT_PX (ARENA_HEIGHT_TILES * TILE_SIZE)

static float clamp_axis(float centre, float screen_size, int zoom, float arena_size)
{
    float visible = screen_size / (float)zoom;
    if (visible >= arena_size) return arena_size / 2;
    if (centre < visible / 2) return visible / 2;
    if (centre > arena_size - visible / 2) return arena_size - visible / 2;
    return centre;
}

void viewport_clamp(Viewport *v)
{
    v->centre_x = clamp_axis(v->centre_x, (float)v->width, v->zoom, ARENA_WIDTH_PX);
    v->centre_y = clamp_axis(v->centre_y, (float)v->height, v->zoom, ARENA_HEIGHT_PX);
}

void viewport_init(Viewport *v, int width, int height)
{
    *v = (Viewport){ARENA_WIDTH_PX / 2.0f, ARENA_HEIGHT_PX / 2.0f, ZOOM_DEFAULT, width, height};
    viewport_clamp(v);
}

void viewport_pan(Viewport *v, float dx_screen_px, float dy_screen_px)
{
    v->centre_x += dx_screen_px / (float)v->zoom;
    v->centre_y += dy_screen_px / (float)v->zoom;
    viewport_clamp(v);
}

void viewport_zoom(Viewport *v, int step)
{
    int zoom = v->zoom + step;
    v->zoom = zoom < ZOOM_MIN ? ZOOM_MIN : zoom > ZOOM_MAX ? ZOOM_MAX : zoom;
    viewport_clamp(v);
}

float viewport_screen_x(const Viewport *v, float world_x)
{
    return (float)v->width / 2 + (world_x - v->centre_x) * (float)v->zoom;
}

float viewport_screen_y(const Viewport *v, float world_y)
{
    return (float)v->height / 2 + (world_y - v->centre_y) * (float)v->zoom;
}
