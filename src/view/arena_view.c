#include "arena_view.h"
#include "raylib.h"
#include "../sim/arena.h"

void arena_view_draw_floor(int window_width, int window_height)
{
    int tile = window_height / ARENA_HEIGHT_TILES;
    int left = (window_width - tile * ARENA_WIDTH_TILES) / 2;
    int top = (window_height - tile * ARENA_HEIGHT_TILES) / 2;
    for (int y = 0; y < ARENA_HEIGHT_TILES; y++) {
        for (int x = 0; x < ARENA_WIDTH_TILES; x++) {
            Color shade = (x + y) % 2 ? (Color){58, 54, 50, 255} : (Color){66, 62, 57, 255};
            DrawRectangle(left + x * tile, top + y * tile, tile, tile, shade);
        }
    }
}
