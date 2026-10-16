#include "sound.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "sound_mix.h"

#define MASTER_VOLUME 0.8f
#define VOICES_PER_SOUND 4
#define MAX_SOUNDS 128
#define MAX_STARTS_PER_FRAME 12

typedef struct {
    char id[ID_LEN];
    Sound voices[VOICES_PER_SOUND];
    int next;
} Voices;

static Voices library[MAX_SOUNDS];
static int library_count;
static SoundTracker tracker;
static bool device_open;
static bool muted;

static Voices *find_voices(const char *id)
{
    for (int i = 0; i < library_count; i++)
        if (strcmp(library[i].id, id) == 0) return &library[i];
    return NULL;
}

static bool load_voices(const char *id, char *err, size_t err_size)
{
    if (id[0] == '\0' || find_voices(id)) return true;
    char path[ID_LEN + 32];
    snprintf(path, sizeof path, "assets/sounds/%s.wav", id);
    Sound base = LoadSound(path);
    if (library_count == MAX_SOUNDS || !IsSoundValid(base)) {
        snprintf(err, err_size, "cannot load sound %s", path);
        return false;
    }
    Voices *entry = &library[library_count++];
    snprintf(entry->id, sizeof entry->id, "%s", id);
    entry->voices[0] = base;
    for (int v = 1; v < VOICES_PER_SOUND; v++) entry->voices[v] = LoadSoundAlias(base);
    return true;
}

static bool load_all(const GameData *data, char *err, size_t err_size)
{
    bool ok = load_voices(data->cues.death, err, err_size) && load_voices(data->cues.part_destroyed, err, err_size) &&
              load_voices(data->cues.spawn_appear, err, err_size);
    for (int e = 0; ok && e < ELEMENT_COUNT; e++)
        ok = load_voices(data->elements[e].hit_sound, err, err_size) && load_voices(data->elements[e].effect_sound, err, err_size);
    for (int p = 0; ok && p < data->part_count; p++) {
        ok = load_voices(data->parts[p].step_sound, err, err_size);
        for (int a = 0; ok && a < data->parts[p].ability_count; a++) ok = load_voices(data->parts[p].abilities[a].sound, err, err_size);
    }
    for (int i = 0; ok && i < data->pickup_count; i++) ok = load_voices(data->pickups[i].sound, err, err_size);
    for (int i = 0; ok && i < data->trap_count; i++) ok = load_voices(data->traps[i].sound, err, err_size);
    return ok;
}

bool sound_init(const GameData *data, const Battle *b, char *err, size_t err_size)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        fprintf(stderr, "warning: no audio device, running silent\n");
        return true;
    }
    SetMasterVolume(MASTER_VOLUME);
    device_open = true;
    sound_tracker_reset(&tracker, b);
    return load_all(data, err, err_size);
}

void sound_reset(const Battle *b)
{
    if (device_open) sound_tracker_reset(&tracker, b);
}

static bool start(const SoundCue *cue, const Viewport *view)
{
    float pan;
    float volume = sound_cue_volume(cue, view, &pan);
    Voices *entry = find_voices(cue->id);
    if (volume < SOUND_MIN_VOLUME || !entry) return false;
    Sound voice = entry->voices[entry->next];
    entry->next = (entry->next + 1) % VOICES_PER_SOUND;
    SetSoundVolume(voice, volume);
    SetSoundPan(voice, raylib_pan(pan));
    PlaySound(voice);
    return true;
}

void sound_update(const Battle *b, const GameData *data, const Viewport *view, bool silent)
{
    static SoundCue cues[MAX_SOUND_CUES];
    if (!device_open) return;
    int count = sound_collect(&tracker, b, data, cues, MAX_SOUND_CUES);
    if (silent || muted) return;
    int started = 0;
    for (int i = 0; i < count && started < MAX_STARTS_PER_FRAME; i++) started += start(&cues[i], view);
}

void sound_toggle_mute(void)
{
    muted = !muted;
}

void sound_close(void)
{
    if (!device_open) return;
    for (int i = 0; i < library_count; i++) {
        for (int v = 1; v < VOICES_PER_SOUND; v++) UnloadSoundAlias(library[i].voices[v]);
        UnloadSound(library[i].voices[0]);
    }
    library_count = 0;
    CloseAudioDevice();
    device_open = false;
}
