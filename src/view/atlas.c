#include "atlas.h"

#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"

#define PATH_SIZE 256

static const char *const frame_kind_names[FRAMES_KIND_COUNT] = {"idle", "act", "hurt", "broken", "play", "variants"};

static char *read_text_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = size < 0 ? NULL : malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, file) == (size_t)size) text[size] = '\0';
    else free(text), text = NULL;
    fclose(file);
    return text;
}

static bool read_int(const cJSON *object, const char *name, int *out)
{
    return json_to_int(cJSON_GetObjectItemCaseSensitive(object, name), out);
}

static bool read_frames(Atlas *atlas, const cJSON *frames)
{
    for (int k = 0; k < FRAMES_KIND_COUNT; k++) {
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(frames, frame_kind_names[k]);
        if (!list) continue;
        if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) == 0) return false;
        if (!json_to_int(cJSON_GetArrayItem(list, 0), &atlas->first[k])) return false;
        atlas->count[k] = cJSON_GetArraySize(list);
    }
    return true;
}

static bool read_atlas(Atlas *atlas, const cJSON *root)
{
    const cJSON *pivot = cJSON_GetObjectItemCaseSensitive(root, "pivot");
    const cJSON *frames = cJSON_GetObjectItemCaseSensitive(root, "frames");
    return cJSON_IsObject(pivot) && cJSON_IsObject(frames)
        && read_int(root, "frame_w", &atlas->frame_w) && read_int(root, "frame_h", &atlas->frame_h)
        && read_int(pivot, "x", &atlas->pivot_x) && read_int(pivot, "y", &atlas->pivot_y)
        && read_frames(atlas, frames);
}

bool atlas_load(Atlas *atlas, const char *folder, const char *id, char *err, size_t err_size)
{
    char path[PATH_SIZE];
    snprintf(path, sizeof path, "assets/%s/%s.json", folder, id);
    char *text = read_text_file(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    *atlas = (Atlas){0};
    bool ok = root && read_atlas(atlas, root);
    cJSON_Delete(root);
    if (!ok) snprintf(err, err_size, "%s: missing or invalid sprite sidecar", path);
    return ok;
}

bool atlases_load(Atlases *atlases, const GameData *data, char *err, size_t err_size)
{
    for (int p = 0; p < data->part_count; p++)
        if (!atlas_load(&atlases->parts[p], "parts", data->parts[p].sprite, err, err_size)) return false;
    for (int i = 0; i < data->pickup_count; i++)
        if (!atlas_load(&atlases->pickups[i], "pickups", data->pickups[i].sprite, err, err_size)) return false;
    for (int i = 0; i < data->trap_count; i++)
        if (!atlas_load(&atlases->traps[i], "traps", data->traps[i].sprite, err, err_size)) return false;
    for (int e = 0; e < ELEMENT_COUNT; e++)
        if (!atlas_load(&atlases->effects[e], "effects", element_id((Element)e), err, err_size)) return false;
    return atlas_load(&atlases->floor, "tiles", "floor", err, err_size)
        && atlas_load(&atlases->rock, "tiles", "rock", err, err_size)
        && atlas_load(&atlases->pillar, "tiles", "pillar", err, err_size);
}

int atlas_frame(const Atlas *atlas, FramesKind kind, int n)
{
    if (atlas->count[kind] == 0) return 0;
    return atlas->first[kind] + n % atlas->count[kind];
}
