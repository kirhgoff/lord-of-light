#ifndef VIEW_SOUND_MIX_H
#define VIEW_SOUND_MIX_H

#include "../sim/battle.h"
#include "scene.h"

#define SOUND_MIN_VOLUME 0.05f
#define SOUND_STEP_TICKS 8
#define MAX_SOUND_CUES 512

#define GAIN_STEP 0.35f
#define GAIN_CUE 1.0f
#define GAIN_ABILITY 0.7f
#define GAIN_EFFECT 0.5f

typedef enum { SOUND_STEP, SOUND_CUE, SOUND_ABILITY, SOUND_EFFECT } SoundCategory;

typedef struct {
    float volume;
    float pan;
} Placement;

typedef struct {
    const char *id;
    float x;
    float y;
    SoundCategory category;
} SoundCue;

typedef struct {
    int seen_events;
    int last_tick;
    int prev_x[MAX_COMBATANTS];
    int prev_y[MAX_COMBATANTS];
    int step_ticks[MAX_COMBATANTS];
    int prev_count;
} SoundTracker;

Placement sound_place(float world_x, float world_y, const Viewport *view);
float sound_gain(SoundCategory category);
float sound_cue_volume(const SoundCue *cue, const Viewport *view, float *pan);
float raylib_pan(float pan);
void sound_tracker_reset(SoundTracker *t, const Battle *b);
int sound_collect(SoundTracker *t, const Battle *b, const GameData *data, SoundCue *out, int max);

#endif
