#ifndef VIEW_SAVES_H
#define VIEW_SAVES_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "../sim/battle.h"

#define SAVES_DIR "saves"
#define SAVES_PATH_SIZE 512

void saves_path_for(const char *dir, time_t when, char *out, size_t size);
bool saves_newest(const char *dir, char *out, size_t size, char *err, size_t err_size);
bool saves_write(const char *dir, time_t when, const Battle *battle, const GameData *data, char *path, size_t path_size,
                 char *err, size_t err_size);
bool saves_load_newest(const char *dir, Battle *battle, const GameData *data, char *path, size_t path_size, char *err,
                       size_t err_size);

#endif
