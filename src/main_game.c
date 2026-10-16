#include "raylib.h"
#include "sim/arena.h"
#include "view/arena_view.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

int main(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Lord of Light " GAME_VERSION);
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){20, 18, 16, 255});
        arena_view_draw_floor(WINDOW_WIDTH, WINDOW_HEIGHT);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
