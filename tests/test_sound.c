#include <math.h>
#include <string.h>
#include "check.h"
#include "../src/view/sound_mix.h"

#define ERR_SIZE 256
#define OUT_MAX 16

static GameData data;
static Battle battle;
static SoundTracker tracker;
static SoundCue cues[OUT_MAX];

static bool near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static void setup(void)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    battle_free(&battle);
    battle = (Battle){0};
    battle_setup(&battle, &data, 1);
    sound_tracker_reset(&tracker, &battle);
}

static Viewport zoomed_view(void)
{
    Viewport view;
    viewport_init(&view, 1280, 720);
    view.zoom = 4;
    view.centre_x = 400;
    view.centre_y = 256;
    return view;
}

static void test_place_centre_is_full_volume_and_centred(void)
{
    Viewport view = zoomed_view();
    Placement p = sound_place(400, 256, &view);
    CHECK(near(p.volume, 1) && near(p.pan, 0));
}

static void test_place_pan_follows_horizontal_offset_and_clamps(void)
{
    Viewport view = zoomed_view();
    CHECK(near(sound_place(400 + 80, 256, &view).pan, 0.5f));
    CHECK(near(sound_place(400 - 160, 256, &view).pan, -1));
    CHECK(near(sound_place(400 + 1000, 256, &view).pan, 1));
    CHECK(near(sound_place(400 - 1000, 256, &view).pan, -1));
}

static void test_place_volume_is_full_inside_and_falls_off_to_zero_one_half_width_outside(void)
{
    Viewport view = zoomed_view();
    CHECK(near(sound_place(400 + 160, 256, &view).volume, 1));
    CHECK(near(sound_place(400 + 160 + 80, 256, &view).volume, 0.5f));
    CHECK(near(sound_place(400 + 160 + 160, 256, &view).volume, 0));
    CHECK(near(sound_place(400 + 160 + 400, 256, &view).volume, 0));
}

static void test_place_volume_counts_vertical_distance_and_diagonals(void)
{
    Viewport view = zoomed_view();
    CHECK(near(sound_place(400, 256 + 84, &view).volume, 1));
    CHECK(near(sound_place(400, 256 + 84 + 80, &view).volume, 0.5f));
    CHECK(near(sound_place(400 + 160 + 96, 256 + 84 + 128, &view).volume, 0));
}

static void test_zooming_out_hears_more_and_quieter_pan(void)
{
    Viewport view = zoomed_view();
    Placement close = sound_place(700, 256, &view);
    view.zoom = 2;
    Placement far = sound_place(700, 256, &view);
    CHECK(close.volume < 1 && near(far.volume, 1));
    CHECK(far.pan < close.pan && near(far.pan, 300.0f / 320));
}

static void test_gain_per_category_and_cue_volume(void)
{
    CHECK(near(sound_gain(SOUND_STEP), 0.35f) && near(sound_gain(SOUND_CUE), 1.0f));
    CHECK(near(sound_gain(SOUND_ABILITY), 0.7f) && near(sound_gain(SOUND_EFFECT), 0.5f));
    Viewport view = zoomed_view();
    SoundCue cue = {"x", 480, 256, SOUND_EFFECT};
    float pan;
    CHECK(near(sound_cue_volume(&cue, &view, &pan), 0.5f) && near(pan, 0.5f));
}

static void test_raylib_pan_is_centre_half_with_left_high(void)
{
    CHECK(near(raylib_pan(0), 0.5f) && near(raylib_pan(-1), 1) && near(raylib_pan(1), 0));
}

static int slot_with_ability(int combatant, OrderKind kind)
{
    const Combatant *who = &battle.combatants[combatant];
    for (int s = 0; s < who->body.slot_count; s++) {
        const Part *part = &data.parts[who->body.slots[s].part];
        for (int a = 0; a < part->ability_count; a++)
            if (part->abilities[a].order == kind) return s;
    }
    return -1;
}

static int part_with_ability(OrderKind kind)
{
    for (int p = 0; p < data.part_count; p++)
        for (int a = 0; a < data.parts[p].ability_count; a++)
            if (data.parts[p].abilities[a].order == kind) return p;
    return -1;
}

static void log_event(EventKind kind, int actor, int actor_part, int target, int element, int detail)
{
    Event e = {.kind = kind, .actor = actor, .actor_part = actor_part, .target = target, .target_part = 0, .element = element, .detail = detail};
    battle_log_event(&battle, e);
}

static float combatant_x(int c)
{
    return (float)battle.combatants[c].x_millipx / MILLIPX_PER_PX;
}

static void test_hit_plays_ability_at_actor_then_hit_sound_at_target(void)
{
    setup();
    int slot = slot_with_ability(0, ORDER_STRIKE);
    if (slot < 0) slot = slot_with_ability(0, ORDER_CAST);
    CHECK(slot >= 0);
    const Ability *ability = NULL;
    const Part *part = &data.parts[battle.combatants[0].body.slots[slot].part];
    for (int a = 0; a < part->ability_count; a++)
        if (part->abilities[a].order == ORDER_STRIKE || part->abilities[a].order == ORDER_CAST) ability = &part->abilities[a];
    int target = battle.combatant_count - 1;
    log_event(EVENT_HIT, 0, slot, target, ability->element, 5);
    int n = sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    CHECK(n == 2);
    CHECK(strcmp(cues[0].id, ability->sound) == 0 && cues[0].category == SOUND_ABILITY && cues[0].x == combatant_x(0));
    CHECK(strcmp(cues[1].id, data.elements[ability->element].hit_sound) == 0 && cues[1].x == combatant_x(target));
}

static void test_destroyed_part_adds_the_cue(void)
{
    setup();
    log_event(EVENT_HIT, -1, -1, 1, ELEMENT_FIRE, 0);
    int n = sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    CHECK(n == 2);
    CHECK(strcmp(cues[0].id, data.elements[ELEMENT_FIRE].hit_sound) == 0);
    CHECK(strcmp(cues[1].id, data.cues.part_destroyed) == 0 && cues[1].category == SOUND_CUE);
}

static void test_death_and_effect_and_unknown_element(void)
{
    setup();
    log_event(EVENT_DEATH, 0, -1, 1, ELEMENT_FIRE, 0);
    log_event(EVENT_EFFECT, 0, -1, 1, ELEMENT_FROST, 3);
    log_event(EVENT_EFFECT, 0, -1, 1, -1, EFFECT_SLOW);
    int n = sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    CHECK(n == 2);
    CHECK(strcmp(cues[0].id, data.cues.death) == 0 && cues[0].category == SOUND_CUE);
    CHECK(strcmp(cues[1].id, data.elements[ELEMENT_FROST].effect_sound) == 0 && cues[1].category == SOUND_EFFECT);
}

static void test_heal_plays_the_support_ability(void)
{
    setup();
    int part = part_with_ability(ORDER_SUPPORT);
    CHECK(part >= 0);
    battle.combatants[0].body.slots[0].part = part;
    log_event(EVENT_EFFECT, 0, 0, 1, ELEMENT_RADIANCE, EFFECT_HEAL);
    int n = sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    CHECK(n == 1 && cues[0].category == SOUND_ABILITY && cues[0].x == combatant_x(0));
    CHECK(strcmp(cues[0].id, data.elements[ELEMENT_RADIANCE].effect_sound) != 0);
}

static void test_spawn_plays_ability_then_appear_cue_at_the_minion(void)
{
    setup();
    int part = part_with_ability(ORDER_SPAWN);
    CHECK(part >= 0);
    battle.combatants[0].body.slots[0].part = part;
    log_event(EVENT_SPAWN, 0, 0, 1, -1, 0);
    int n = sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    CHECK(n == 2);
    CHECK(cues[0].category == SOUND_ABILITY && cues[0].x == combatant_x(0));
    CHECK(strcmp(cues[1].id, data.cues.spawn_appear) == 0 && cues[1].x == combatant_x(1));
}

static void test_pickup_and_trap_play_their_sound_where_they_are(void)
{
    setup();
    CHECK(battle.pickup_count > 0 && battle.trap_count > 0);
    log_event(EVENT_PICKUP, 1, -1, -1, -1, 0);
    log_event(EVENT_TRAP, -1, -1, 1, ELEMENT_FIRE, 0);
    int n = sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    CHECK(n == 2);
    CHECK(strcmp(cues[0].id, data.pickups[battle.pickups[0].def].sound) == 0 && cues[0].x == combatant_x(1));
    CHECK(strcmp(cues[1].id, data.traps[battle.traps[0].def].sound) == 0);
    CHECK(cues[1].x == (float)battle.traps[0].x_millipx / MILLIPX_PER_PX);
}

static void test_event_cursor_consumes_once_and_reset_skips_history(void)
{
    setup();
    log_event(EVENT_DEATH, 0, -1, 1, ELEMENT_FIRE, 0);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 1);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
    log_event(EVENT_DEATH, 0, -1, 1, ELEMENT_FIRE, 0);
    sound_tracker_reset(&tracker, &battle);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
}

static void test_out_of_range_indices_are_ignored(void)
{
    setup();
    log_event(EVENT_DEATH, 0, -1, 999, ELEMENT_FIRE, 0);
    log_event(EVENT_PICKUP, -5, -1, -1, -1, 999);
    log_event(EVENT_TRAP, -1, -1, 1, ELEMENT_FIRE, 999);
    log_event(EVENT_HIT, 999, 999, 1, 99, 5);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
}

static void test_output_is_capped(void)
{
    setup();
    for (int i = 0; i < 5; i++) log_event(EVENT_DEATH, 0, -1, 1, ELEMENT_FIRE, 0);
    CHECK(sound_collect(&tracker, &battle, &data, cues, 3) == 3);
}

static void move_and_advance(int ticks, int dx)
{
    battle.tick += ticks;
    battle.combatants[0].x_millipx += dx;
}

static void test_steps_come_once_per_eight_ticks_of_movement(void)
{
    setup();
    int count = 0;
    for (int i = 0; i < 3; i++) {
        move_and_advance(3, 1000);
        count += sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    }
    CHECK(count == 1);
    CHECK(cues[0].category == SOUND_STEP && cues[0].id[0] != '\0' && cues[0].x == combatant_x(0));
}

static void test_frames_without_a_tick_keep_the_step_counter(void)
{
    setup();
    move_and_advance(5, 1000);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
    move_and_advance(3, 1000);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 1);
}

static void test_standing_still_makes_no_steps_and_resets_the_counter(void)
{
    setup();
    move_and_advance(5, 1000);
    sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    move_and_advance(10, 0);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
    move_and_advance(5, 1000);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
}

static void test_dead_combatants_make_no_steps(void)
{
    setup();
    battle.combatants[0].alive = false;
    move_and_advance(20, 1000);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 0);
}

static void test_chain_hits_of_one_action_play_the_ability_once(void)
{
    setup();
    int slot = slot_with_ability(0, ORDER_STRIKE);
    if (slot < 0) slot = slot_with_ability(0, ORDER_CAST);
    const Part *part = &data.parts[battle.combatants[0].body.slots[slot].part];
    int element = -1;
    for (int a = 0; a < part->ability_count; a++)
        if (part->abilities[a].order == ORDER_STRIKE || part->abilities[a].order == ORDER_CAST) element = part->abilities[a].element;
    log_event(EVENT_HIT, 0, slot, 1, element, 5);
    log_event(EVENT_HIT, 0, slot, 2, element, 5);
    int n = sound_collect(&tracker, &battle, &data, cues, OUT_MAX);
    CHECK(n == 3);
    CHECK(cues[0].category == SOUND_ABILITY && cues[0].x == combatant_x(0));
    CHECK(cues[1].x == combatant_x(1) && cues[2].x == combatant_x(2));
    battle.tick++;
    log_event(EVENT_HIT, 0, slot, 1, element, 5);
    battle.log[battle.log_count - 1].tick = battle.tick;
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 2);
}

static void test_cast_only_part_plays_its_cast_sound(void)
{
    setup();
    int cast_part = -1;
    for (int p = 0; p < data.part_count && cast_part < 0; p++) {
        bool only_cast = data.parts[p].ability_count > 0;
        for (int a = 0; a < data.parts[p].ability_count; a++) only_cast = only_cast && data.parts[p].abilities[a].order == ORDER_CAST;
        if (only_cast) cast_part = p;
    }
    CHECK(cast_part >= 0);
    battle.combatants[0].body.slots[0].part = cast_part;
    const Ability *cast = &data.parts[cast_part].abilities[0];
    log_event(EVENT_HIT, 0, 0, 1, cast->element, 5);
    CHECK(sound_collect(&tracker, &battle, &data, cues, OUT_MAX) == 2);
    CHECK(strcmp(cues[0].id, cast->sound) == 0);
}

void run_sound_tests(void)
{
    test_chain_hits_of_one_action_play_the_ability_once();
    test_cast_only_part_plays_its_cast_sound();
    test_place_centre_is_full_volume_and_centred();
    test_place_pan_follows_horizontal_offset_and_clamps();
    test_place_volume_is_full_inside_and_falls_off_to_zero_one_half_width_outside();
    test_place_volume_counts_vertical_distance_and_diagonals();
    test_zooming_out_hears_more_and_quieter_pan();
    test_gain_per_category_and_cue_volume();
    test_raylib_pan_is_centre_half_with_left_high();
    test_hit_plays_ability_at_actor_then_hit_sound_at_target();
    test_destroyed_part_adds_the_cue();
    test_death_and_effect_and_unknown_element();
    test_heal_plays_the_support_ability();
    test_spawn_plays_ability_then_appear_cue_at_the_minion();
    test_pickup_and_trap_play_their_sound_where_they_are();
    test_event_cursor_consumes_once_and_reset_skips_history();
    test_out_of_range_indices_are_ignored();
    test_output_is_capped();
    test_steps_come_once_per_eight_ticks_of_movement();
    test_frames_without_a_tick_keep_the_step_counter();
    test_standing_still_makes_no_steps_and_resets_the_counter();
    test_dead_combatants_make_no_steps();
}
