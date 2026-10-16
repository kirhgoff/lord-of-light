#include <string.h>
#include "check.h"
#include "../src/view/scene.h"

#define WIDTH 1280
#define HEIGHT 720
#define ERR_SIZE 256

static GameData data;
static Battle battle;

static void setup_battle(void)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    battle_setup(&battle, &data, 1);
}

static void test_atlas_reads_sidecar(void)
{
    Atlas atlas;
    char err[ERR_SIZE];
    CHECK(atlas_load(&atlas, "parts", "sword_hand", err, ERR_SIZE));
    CHECK(atlas.frame_w == 32 && atlas.frame_h == 32);
    CHECK(atlas.pivot_x == 5 && atlas.pivot_y == 24);
    CHECK(atlas.first[FRAMES_ACT] == 2 && atlas.count[FRAMES_ACT] == 3);
    CHECK(atlas.first[FRAMES_BROKEN] == 6);
    CHECK(atlas_load(&atlas, "effects", "fire", err, ERR_SIZE));
    CHECK(atlas.count[FRAMES_PLAY] == 5);
    CHECK(atlas_load(&atlas, "tiles", "floor", err, ERR_SIZE));
    CHECK(atlas.count[FRAMES_VARIANTS] == 4);
}

static void test_atlas_missing_names_the_path(void)
{
    Atlas atlas;
    char err[ERR_SIZE];
    CHECK(!atlas_load(&atlas, "parts", "no_such_part", err, ERR_SIZE));
    CHECK(strstr(err, "assets/parts/no_such_part.json") != NULL);
}

static void test_atlas_frame_wraps_within_the_kind(void)
{
    Atlas atlas = {.first = {[FRAMES_ACT] = 2}, .count = {[FRAMES_ACT] = 3}};
    CHECK(atlas_frame(&atlas, FRAMES_ACT, 0) == 2);
    CHECK(atlas_frame(&atlas, FRAMES_ACT, 4) == 3);
    CHECK(atlas_frame(&atlas, FRAMES_HURT, 7) == 0);
}

static void test_all_atlases_load_for_the_real_data(void)
{
    static Atlases atlases;
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    CHECK(atlases_load(&atlases, &data, err, ERR_SIZE));
    if (err[0]) printf("  %s\n", err);
}

static void test_viewport_starts_centred_and_clamped(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    CHECK(v.zoom == 2);
    CHECK(v.centre_x >= 320 && v.centre_x <= 448);
    CHECK(v.centre_y >= 180 && v.centre_y <= 332);
    viewport_zoom(&v, -1);
    CHECK(v.centre_x == 384 && v.centre_y == 256);
}

static void test_viewport_zoom_stays_within_limits(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    for (int i = 0; i < 10; i++) viewport_zoom(&v, 1);
    CHECK(v.zoom == ZOOM_MAX);
    for (int i = 0; i < 10; i++) viewport_zoom(&v, -1);
    CHECK(v.zoom == ZOOM_MIN);
}

static void test_viewport_pan_stops_at_the_arena_edge(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    viewport_pan(&v, 10000, 10000);
    CHECK(v.centre_x == 448 && v.centre_y == 332);
    viewport_pan(&v, -10000, -10000);
    CHECK(v.centre_x == 320 && v.centre_y == 180);
}

static void test_viewport_centre_maps_to_screen_centre(void)
{
    Viewport v;
    viewport_init(&v, WIDTH, HEIGHT);
    viewport_pan(&v, 40, -30);
    CHECK(viewport_screen_x(&v, v.centre_x) == WIDTH / 2);
    CHECK(viewport_screen_y(&v, v.centre_y) == HEIGHT / 2);
    CHECK(viewport_screen_x(&v, v.centre_x + 10) == WIDTH / 2 + 10 * v.zoom);
}

static void test_position_interpolates_between_ticks(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    battle.combatants[0].x_millipx = 1000;
    battle.combatants[0].y_millipx = 4000;
    scene_before_step(&scene, &battle);
    battle.combatants[0].x_millipx = 3000;
    scene.alpha = 0.5f;
    float x, y;
    scene_position(&scene, &battle, 0, &x, &y);
    CHECK(x == 2.0f && y == 4.0f);
    scene.prev_count = 0;
    scene_position(&scene, &battle, 0, &x, &y);
    CHECK(x == 3.0f);
    CHECK(scene_time(&scene, &battle) == (float)battle.tick + 0.5f);
}

static void test_facing_follows_horizontal_movement(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    int pantheon = 0, horde = battle.combatant_count - 1;
    CHECK(!scene.facing_left[pantheon] && scene.facing_left[horde]);
    scene_before_step(&scene, &battle);
    battle.combatants[pantheon].x_millipx -= 500;
    battle.combatants[horde].x_millipx += 500;
    scene_after_step(&scene, &battle);
    CHECK(scene.facing_left[pantheon] && !scene.facing_left[horde]);
    scene_before_step(&scene, &battle);
    scene_after_step(&scene, &battle);
    CHECK(scene.facing_left[pantheon] && !scene.facing_left[horde]);
}

static Event hit_event(int tick, int actor, int actor_part, int target, int target_part)
{
    return (Event){.tick = tick, .kind = EVENT_HIT, .actor = actor, .actor_part = actor_part, .target = target,
                   .target_part = target_part, .element = ELEMENT_FIRE};
}

static int live_effects(const Scene *scene)
{
    int live = 0;
    for (int e = 0; e < MAX_EFFECTS; e++) live += scene->effects[e].since_tick != NEVER;
    return live;
}

static void test_hit_event_marks_actor_target_and_effect(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    Event hit = hit_event(7, 0, 1, 5, 2);
    scene_apply_event(&scene, &battle, &hit);
    CHECK(scene.act_since[0][1] == 7);
    CHECK(scene.hit_since[5][2] == 7 && scene.hit_element[5][2] == ELEMENT_FIRE);
    CHECK(live_effects(&scene) == 1 && scene.effects[0].element == ELEMENT_FIRE);
}

static void test_trap_hit_without_actor_marks_only_the_target(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    Event hit = hit_event(3, -1, -1, 5, 0);
    scene_apply_event(&scene, &battle, &hit);
    CHECK(scene.hit_since[5][0] == 3);
    CHECK(scene.act_since[0][0] == NEVER);
}

static void test_trap_event_marks_the_trap(void)
{
    setup_battle();
    CHECK(battle.trap_count > 0);
    Scene scene = {0};
    scene_reset(&scene, &battle);
    Event trap = {.tick = 9, .kind = EVENT_TRAP, .target = 0, .element = ELEMENT_FROST, .detail = 0};
    scene_apply_event(&scene, &battle, &trap);
    CHECK(scene.trap_since[0] == 9);
    CHECK(live_effects(&scene) == 1);
}

static void test_decision_and_death_events_change_nothing(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    Event decision = {.tick = 4, .kind = EVENT_DECISION, .actor = 0};
    Event death = {.tick = 4, .kind = EVENT_DEATH, .target = 0};
    scene_apply_event(&scene, &battle, &decision);
    scene_apply_event(&scene, &battle, &death);
    CHECK(live_effects(&scene) == 0 && scene.act_since[0][0] == NEVER && scene.hit_since[0][0] == NEVER);
}

static void test_after_step_consumes_only_new_events(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    CHECK(scene.seen_events == battle.log_count);
    battle_log_event(&battle, hit_event(1, 0, 0, 5, 0));
    scene_after_step(&scene, &battle);
    CHECK(scene.seen_events == battle.log_count && live_effects(&scene) == 1);
    scene_after_step(&scene, &battle);
    CHECK(live_effects(&scene) == 1);
}

static void test_effect_ring_overwrites_the_oldest(void)
{
    setup_battle();
    Scene scene = {0};
    scene_reset(&scene, &battle);
    for (int i = 0; i < MAX_EFFECTS + 3; i++) {
        Event hit = hit_event(i, 0, 0, 5, 0);
        scene_apply_event(&scene, &battle, &hit);
    }
    CHECK(live_effects(&scene) == MAX_EFFECTS);
    CHECK(scene.effects[0].since_tick == MAX_EFFECTS && scene.effects[3].since_tick == 3);
}

static Atlas test_atlas(void)
{
    return (Atlas){.first = {[FRAMES_IDLE] = 0, [FRAMES_ACT] = 2, [FRAMES_HURT] = 5, [FRAMES_BROKEN] = 6},
                   .count = {[FRAMES_IDLE] = 2, [FRAMES_ACT] = 3, [FRAMES_HURT] = 1, [FRAMES_BROKEN] = 1}};
}

static void test_part_frame_priorities(void)
{
    Atlas atlas = test_atlas();
    Combatant who = {0};
    who.parts[0].integrity = 10;
    Part mover = {.move_speed_px_per_second = 40};
    Part still = {0};
    CHECK(part_frame(&atlas, &who, 0, &still, false, NEVER, NEVER, 0, 100) == 0);
    CHECK(part_frame(&atlas, &who, 0, &still, false, NEVER, NEVER, 0, 100 + IDLE_FRAME_TICKS) == 1);
    CHECK(part_frame(&atlas, &who, 0, &still, false, NEVER, 99, 0, 100) == 5);
    CHECK(part_frame(&atlas, &who, 0, &still, false, 98, NEVER, 0, 100) == 3);
    who.parts[0].order.kind = ORDER_BLOCK;
    CHECK(part_frame(&atlas, &who, 0, &still, false, NEVER, NEVER, 0, 100) == 3);
    who.parts[0].order.kind = ORDER_MOVE_TO;
    CHECK(part_frame(&atlas, &who, 0, &mover, true, NEVER, NEVER, 0, WALK_FRAME_TICKS) == 3);
    CHECK(part_frame(&atlas, &who, 0, &still, true, NEVER, NEVER, 0, WALK_FRAME_TICKS) == 0);
    who.parts[0].integrity = 0;
    CHECK(part_frame(&atlas, &who, 0, &still, false, 99, 99, 0, 100) == 6);
}

static void test_effect_frame_expires(void)
{
    Effect effect = {0, 0, ELEMENT_FIRE, 10};
    CHECK(effect_frame(&effect, 10) == 0);
    CHECK(effect_frame(&effect, 10 + EFFECT_TICKS - 1) == 4);
    CHECK(effect_frame(&effect, 10 + EFFECT_TICKS) == -1);
}

static int index_of(const Drawable *list, int count, DrawableKind kind, int index)
{
    for (int i = 0; i < count; i++)
        if (list[i].kind == kind && list[i].index == index) return i;
    return -1;
}

static void test_collect_sorts_by_y_and_skips_dead_and_respawning(void)
{
    setup_battle();
    battle.pickup_count = 2;
    battle.pickups[0] = (FloorObject){0, 50000, 110000, 0};
    battle.pickups[1] = (FloorObject){0, 50000, 120000, 5};
    battle.remains_count = 1;
    battle.remains[0] = (Remains){60000, 90000, 0, {0}};
    battle.combatants[0].x_millipx = 70000;
    battle.combatants[0].y_millipx = 100000;
    battle.combatants[1].alive = false;
    Scene scene = {0};
    scene_reset(&scene, &battle);
    Drawable list[MAX_DRAWABLES];
    int count = scene_collect(&scene, &battle, list);
    int remains = index_of(list, count, DRAW_REMAINS, 0);
    int combatant = index_of(list, count, DRAW_COMBATANT, 0);
    int pickup = index_of(list, count, DRAW_PICKUP, 0);
    CHECK(remains >= 0 && remains < combatant && combatant < pickup);
    CHECK(index_of(list, count, DRAW_COMBATANT, 1) == -1);
    CHECK(index_of(list, count, DRAW_PICKUP, 1) == -1);
    int obstacles = 0, cells = 0;
    for (int i = 0; i < count; i++) obstacles += list[i].kind == DRAW_OBSTACLE;
    for (int r = 0; r < ARENA_HEIGHT_TILES; r++)
        for (int c = 0; c < ARENA_WIDTH_TILES; c++) cells += battle.obstacle[r][c] != 0;
    CHECK(obstacles == cells);
    for (int i = 1; i < count; i++) CHECK(list[i - 1].y <= list[i].y);
}

static void test_agni_feet_sit_33_px_below_the_origin(void)
{
    static Atlases atlases;
    char err[ERR_SIZE];
    setup_battle();
    CHECK(atlases_load(&atlases, &data, err, ERR_SIZE));
    int found = 0;
    for (int p = 0; p < data.pantheon_count; p++)
        for (int d = 0; d < DEITIES_PER_PANTHEON; d++)
            if (strcmp(data.pantheons[p].deities[d].name, "Agni") == 0) {
                Body body = data.pantheons[p].deities[d].body;
                CHECK(body_feet_offset(&body, &atlases) == 33);
                body.slots[body.slot_count] = body.slots[0];
                body.slots[body.slot_count].internal = true;
                body.slots[body.slot_count].anchor_y = 200;
                body.slot_count++;
                CHECK(body_feet_offset(&body, &atlases) == 33);
                found++;
            }
    CHECK(found == 1);
    Body empty = {0};
    CHECK(body_feet_offset(&empty, &atlases) == 32);
}

static void test_tile_hash_covers_every_variant(void)
{
    int variants[4] = {0}, pillars = 0, rocks = 0;
    for (int r = 0; r < ARENA_HEIGHT_TILES; r++)
        for (int c = 0; c < ARENA_WIDTH_TILES; c++) {
            int h = tile_hash(c, r);
            CHECK(h >= 0 && h < 256);
            variants[h % 4]++;
            if (h % 3 == 0) pillars++; else rocks++;
        }
    for (int v = 0; v < 4; v++) CHECK(variants[v] > 0);
    CHECK(pillars > 0 && rocks > 0);
}

static void test_remains_offset_is_bounded_and_deterministic(void)
{
    int distinct = 0, first_dx = 0, first_dy = 0;
    remains_offset(0, 0, &first_dx, &first_dy);
    for (int r = 0; r < 8; r++)
        for (int p = 0; p < 8; p++) {
            int dx, dy, again_dx, again_dy;
            remains_offset(r, p, &dx, &dy);
            remains_offset(r, p, &again_dx, &again_dy);
            CHECK(dx == again_dx && dy == again_dy);
            CHECK(dx >= -REMAINS_SPREAD_PX && dx <= REMAINS_SPREAD_PX && dy >= -4 && dy <= 6);
            distinct += dx != first_dx || dy != first_dy;
        }
    CHECK(distinct > 30);
}

void run_view_tests(void)
{
    test_collect_sorts_by_y_and_skips_dead_and_respawning();
    test_agni_feet_sit_33_px_below_the_origin();
    test_tile_hash_covers_every_variant();
    test_remains_offset_is_bounded_and_deterministic();
    test_hit_event_marks_actor_target_and_effect();
    test_trap_hit_without_actor_marks_only_the_target();
    test_trap_event_marks_the_trap();
    test_decision_and_death_events_change_nothing();
    test_after_step_consumes_only_new_events();
    test_effect_ring_overwrites_the_oldest();
    test_part_frame_priorities();
    test_effect_frame_expires();
    test_position_interpolates_between_ticks();
    test_facing_follows_horizontal_movement();
    test_viewport_starts_centred_and_clamped();
    test_viewport_zoom_stays_within_limits();
    test_viewport_pan_stops_at_the_arena_edge();
    test_viewport_centre_maps_to_screen_centre();
    test_atlas_reads_sidecar();
    test_atlas_missing_names_the_path();
    test_atlas_frame_wraps_within_the_kind();
    test_all_atlases_load_for_the_real_data();
}
