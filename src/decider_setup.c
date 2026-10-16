#include "decider_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_text_file(const char *path)
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

bool decider_setup_parse(DeciderSetup *setup, int argc, char **argv, int *i)
{
    if (*i + 1 >= argc) return false;
    if (strcmp(argv[*i], "--replay") == 0) setup->replay_path = argv[++*i];
    else if (strcmp(argv[*i], "--decider") == 0 && strcmp(argv[*i + 1], "laya") == 0) setup->laya = true, ++*i;
    else if (strcmp(argv[*i], "--decider") == 0 && strcmp(argv[*i + 1], "reflex") == 0) setup->laya = false, ++*i;
    else return false;
    return true;
}

static bool open_replay(DeciderSetup *setup, const GameData *data, Battle *battle, char *err, size_t err_size)
{
    char *json = read_text_file(setup->replay_path);
    if (!json) return snprintf(err, err_size, "cannot read %s", setup->replay_path), false;
    bool loaded = battle_load_json(&setup->source, data, json, err, err_size);
    free(json);
    if (!loaded) return false;
    battle_setup(battle, data, setup->source.seed);
    replay_decider_init(&setup->replay, setup->source.log, setup->source.log_count);
    setup->decider = &setup->replay.base;
    return true;
}

bool decider_setup_open(DeciderSetup *setup, const GameData *data, Battle *battle, char *err, size_t err_size)
{
    if (setup->replay_path) return open_replay(setup, data, battle, err, err_size);
    if (setup->laya) {
        LayaTransport *transport = laya_curl_transport(data->laya.timeout_ms);
        if (!transport) return snprintf(err, err_size, "cannot start libcurl"), false;
        laya_decider_init(&setup->laya_decider, transport);
        setup->decider = &setup->laya_decider.base;
        return true;
    }
    setup->reflex = reflex_decider();
    setup->decider = &setup->reflex;
    return true;
}

void decider_setup_close(DeciderSetup *setup)
{
    if (setup->decider == &setup->laya_decider.base) laya_decider_close(&setup->laya_decider);
    if (setup->replay_path) battle_free(&setup->source);
}
