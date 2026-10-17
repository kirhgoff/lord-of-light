#ifndef DECIDER_SETUP_H
#define DECIDER_SETUP_H

#include <stdio.h>

#include "laya/laya.h"
#include "sim/decide.h"

typedef struct {
    Decider base;
    FILE *out;
} RecordingDecider;

#define JEV_KEY_PATH ".jev_key"

typedef struct {
    DeciderKind choice;
    bool jev_available;
    const char *jev_key_problem;
    char jev_key[LAYA_KEY_LEN];
    const char *dump_requests_path;
    const char *replay_path;
    Decider reflex;
    LayaDecider laya_decider;
    LayaDecider jev_decider;
    bool laya_open;
    bool jev_open;
    ReplayDecider replay;
    RecordingDecider recording;
    Battle source;
    Decider *decider;
} DeciderSetup;

bool decider_setup_parse(DeciderSetup *setup, int argc, char **argv, int *i);
bool decider_setup_open(DeciderSetup *setup, const GameData *data, Battle *battle, char *err, size_t err_size);
bool decider_setup_select(DeciderSetup *setup, const GameData *data, DeciderKind choice, char *err, size_t err_size);
LayaDecider *decider_setup_active_laya(DeciderSetup *setup);
bool decider_setup_networked(const DeciderSetup *setup);
void decider_setup_close(DeciderSetup *setup);
char *read_text_file(const char *path);

#endif
