#include "decider_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/briefing.h"

char *read_text_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
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
    else if (strcmp(argv[*i], "--dump-requests") == 0) setup->dump_requests_path = argv[++*i];
    else if (strcmp(argv[*i], "--decider") == 0 && strcmp(argv[*i + 1], "laya") == 0) setup->choice = DECIDER_LAYA, ++*i;
    else if (strcmp(argv[*i], "--decider") == 0 && strcmp(argv[*i + 1], "jev") == 0) setup->choice = DECIDER_JEV, ++*i;
    else if (strcmp(argv[*i], "--decider") == 0 && strcmp(argv[*i + 1], "reflex") == 0) setup->choice = DECIDER_REFLEX, ++*i;
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

static bool recording_decide(Decider *self, const Battle *battle, const GameData *data, int combatant, Event *out)
{
    RecordingDecider *recording = (RecordingDecider *)self;
    reflex_decide(self, battle, data, combatant, out);
    char *record = laya_request_record_json(battle, data, combatant, out);
    fprintf(recording->out, "%s\n", record);
    free(record);
    return true;
}

static bool open_recording(DeciderSetup *setup, char *err, size_t err_size)
{
    setup->recording.out = fopen(setup->dump_requests_path, "w");
    if (!setup->recording.out) return snprintf(err, err_size, "cannot write %s", setup->dump_requests_path), false;
    setup->recording.base.decide = recording_decide;
    setup->decider = &setup->recording.base;
    return true;
}

static void load_jev_key(DeciderSetup *setup)
{
    char *text = read_text_file(JEV_KEY_PATH);
    setup->jev_available = text && laya_parse_key(text, setup->jev_key, sizeof setup->jev_key);
    free(text);
}

static bool open_networked(LayaDecider *decider, bool *open, LayaTransport *transport, const LayaEndpoint *endpoint, char *err, size_t err_size)
{
    if (!transport) return snprintf(err, err_size, "cannot start libcurl"), false;
    laya_decider_init(decider, transport, endpoint);
    *open = true;
    return true;
}

static bool open_laya(DeciderSetup *setup, const GameData *data, char *err, size_t err_size)
{
    if (setup->laya_open) return true;
    const char *urls[LAYA_MAX_URLS];
    for (int i = 0; i < data->laya.url_count; i++) urls[i] = data->laya.urls[i];
    LayaEndpoint endpoint = {DECIDER_LAYA, urls, data->laya.url_count, NULL};
    return open_networked(&setup->laya_decider, &setup->laya_open, laya_curl_transport(data->laya.timeout_ms, NULL), &endpoint, err, err_size);
}

static bool open_jev(DeciderSetup *setup, const GameData *data, char *err, size_t err_size)
{
    if (setup->jev_open) return true;
    if (!setup->jev_available) return snprintf(err, err_size, "cannot read %s", JEV_KEY_PATH), false;
    const char *urls[1] = {data->laya.jev_url};
    LayaEndpoint endpoint = {DECIDER_JEV, urls, 1, data->laya.jev_model};
    return open_networked(&setup->jev_decider, &setup->jev_open, laya_curl_transport(data->laya.timeout_ms, setup->jev_key), &endpoint, err, err_size);
}

bool decider_setup_select(DeciderSetup *setup, const GameData *data, DeciderKind choice, char *err, size_t err_size)
{
    LayaDecider *networked = choice == DECIDER_LAYA ? &setup->laya_decider : &setup->jev_decider;
    if (choice == DECIDER_REFLEX) {
        setup->reflex = reflex_decider();
        setup->decider = &setup->reflex;
    } else {
        bool opened = choice == DECIDER_LAYA ? open_laya(setup, data, err, err_size) : open_jev(setup, data, err, err_size);
        if (!opened) return false;
        laya_decider_reset(networked);
        setup->decider = &networked->base;
    }
    setup->choice = choice;
    return true;
}

LayaDecider *decider_setup_active_laya(DeciderSetup *setup)
{
    if (setup->decider == &setup->laya_decider.base) return &setup->laya_decider;
    if (setup->decider == &setup->jev_decider.base) return &setup->jev_decider;
    return NULL;
}

bool decider_setup_networked(const DeciderSetup *setup)
{
    return setup->choice == DECIDER_LAYA || setup->choice == DECIDER_JEV;
}

bool decider_setup_open(DeciderSetup *setup, const GameData *data, Battle *battle, char *err, size_t err_size)
{
    if (setup->choice == DECIDER_NONE) setup->choice = DECIDER_REFLEX;
    load_jev_key(setup);
    if (setup->dump_requests_path && (setup->replay_path || decider_setup_networked(setup)))
        return snprintf(err, err_size, "--dump-requests cannot combine with --replay or --decider laya|jev"), false;
    if (setup->replay_path) return open_replay(setup, data, battle, err, err_size);
    if (setup->dump_requests_path) return open_recording(setup, err, err_size);
    return decider_setup_select(setup, data, setup->choice, err, err_size);
}

void decider_setup_close(DeciderSetup *setup)
{
    if (setup->laya_open) laya_decider_close(&setup->laya_decider);
    if (setup->jev_open) laya_decider_close(&setup->jev_decider);
    memset(setup->jev_key, 0, sizeof setup->jev_key);
    if (setup->recording.out) fclose(setup->recording.out);
    if (setup->replay_path) battle_free(&setup->source);
}
