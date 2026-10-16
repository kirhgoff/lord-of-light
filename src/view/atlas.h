#ifndef VIEW_ATLAS_H
#define VIEW_ATLAS_H

#include <stdbool.h>
#include <stddef.h>

#include "../sim/data.h"

typedef enum {
    FRAMES_IDLE,
    FRAMES_ACT,
    FRAMES_HURT,
    FRAMES_BROKEN,
    FRAMES_PLAY,
    FRAMES_VARIANTS,
    FRAMES_KIND_COUNT
} FramesKind;

typedef struct {
    int frame_w;
    int frame_h;
    int pivot_x;
    int pivot_y;
    int first[FRAMES_KIND_COUNT];
    int count[FRAMES_KIND_COUNT];
} Atlas;

typedef struct {
    Atlas parts[MAX_PARTS];
    Atlas pickups[MAX_ARENA_OBJECTS];
    Atlas traps[MAX_ARENA_OBJECTS];
    Atlas effects[ELEMENT_COUNT];
    Atlas floor;
    Atlas rock;
    Atlas pillar;
} Atlases;

bool atlas_parse(Atlas *atlas, const char *text);
bool atlas_load(Atlas *atlas, const char *folder, const char *id, char *err, size_t err_size);
bool atlases_load(Atlases *atlases, const GameData *data, char *err, size_t err_size);
int atlas_frame(const Atlas *atlas, FramesKind kind, int n);

#endif
