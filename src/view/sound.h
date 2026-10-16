#ifndef VIEW_SOUND_H
#define VIEW_SOUND_H

#include <stdbool.h>
#include <stddef.h>

#include "scene.h"

bool sound_init(const GameData *data, const Battle *b, char *err, size_t err_size);
void sound_reset(const Battle *b);
void sound_update(const Battle *b, const GameData *data, const Viewport *view, bool silent);
void sound_toggle_mute(void);
void sound_close(void);

#endif
