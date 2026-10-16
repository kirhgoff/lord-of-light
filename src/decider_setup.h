#ifndef DECIDER_SETUP_H
#define DECIDER_SETUP_H

#include "laya/laya.h"
#include "sim/decide.h"

typedef struct {
    bool laya;
    const char *replay_path;
    Decider reflex;
    LayaDecider laya_decider;
    ReplayDecider replay;
    Battle source;
    Decider *decider;
} DeciderSetup;

bool decider_setup_parse(DeciderSetup *setup, int argc, char **argv, int *i);
bool decider_setup_open(DeciderSetup *setup, const GameData *data, Battle *battle, char *err, size_t err_size);
void decider_setup_close(DeciderSetup *setup);
char *read_text_file(const char *path);

#endif
