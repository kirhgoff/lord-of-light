#include <string.h>
#include "check.h"
#include "../src/sim/movement.h"
#include "../src/sim/vision.h"

#define ERR_SIZE 256
#define PX MILLIPX_PER_PX
#define TILE TILE_MILLIPX

static GameData data;
static Battle battle;

static void open_field(void)
{
    char err[ERR_SIZE];
    LOAD_OR_EXIT(load_game_data(&data, "data", err, ERR_SIZE), err);
    battle_setup(&battle, &data, 3);
    memset(battle.obstacle, 0, sizeof battle.obstacle);
    battle.pickup_count = battle.trap_count = 0;
    for (int c = 0; c < battle.combatant_count; c++) {
        battle.combatants[c].x_millipx = (2 + c * 4) * TILE + TILE / 2;
        battle.combatants[c].y_millipx = 28 * TILE + TILE / 2;
    }
}

static void place(int c, int x_tile, int y_tile)
{
    battle.combatants[c].x_millipx = x_tile * TILE + TILE / 2;
    battle.combatants[c].y_millipx = y_tile * TILE + TILE / 2;
}

static void set_side(int c, Side side)
{
    battle.combatants[c].side = battle.combatants[c].charm_side = side;
}

static void send_to(int c, int x_millipx, int y_millipx)
{
    Combatant *who = &battle.combatants[c];
    who->parts[who->body.core].order = (Order){ORDER_MOVE_TO, -1, x_millipx, y_millipx};
}

static void run(int ticks)
{
    for (int t = 0; t < ticks; t++) battle_step(&battle, &data, NULL);
}

static void give_vision(int c, int close, int medium, int far)
{
    Combatant *who = &battle.combatants[c];
    for (int s = 0; s < who->body.slot_count; s++) {
        Part *part = &data.parts[who->body.slots[s].part];
        part->vision_close = part->vision_medium = part->vision_far = 0;
    }
    Part *core = &data.parts[who->body.slots[who->body.core].part];
    core->vision_close = close;
    core->vision_medium = medium;
    core->vision_far = far;
}

static void test_moves_at_part_speed(void)
{
    open_field();
    int speed = combatant_speed_px_per_second(&battle.combatants[0], &data);
    CHECK(speed > 0);
    place(0, 2, 2);
    battle.combatants[0].y_millipx = 3 * TILE;
    int x = battle.combatants[0].x_millipx;
    send_to(0, 40 * TILE, 3 * TILE);
    run(TICKS_PER_SECOND);
    CHECK(battle.combatants[0].x_millipx - x == speed * PX);
}

static void test_no_order_stands_still(void)
{
    open_field();
    int x = battle.combatants[0].x_millipx;
    run(20);
    CHECK(battle.combatants[0].x_millipx == x);
}

static void test_crippled_movers_stop_movement(void)
{
    open_field();
    Combatant *who = &battle.combatants[0];
    for (int s = 0; s < who->body.slot_count; s++)
        if (data.parts[who->body.slots[s].part].move_speed_px_per_second > 0) who->parts[s].integrity = 0;
    CHECK(combatant_speed_px_per_second(who, &data) == 0);
    send_to(0, 40 * TILE, 2 * TILE);
    int x = who->x_millipx;
    run(20);
    CHECK(who->x_millipx == x);
}

static void test_arena_wall_blocks(void)
{
    open_field();
    place(0, 1, 1);
    send_to(0, -50 * TILE, -50 * TILE);
    run(200);
    CHECK(battle.combatants[0].x_millipx >= COMBATANT_RADIUS_MILLIPX);
    CHECK(battle.combatants[0].y_millipx >= COMBATANT_RADIUS_MILLIPX);
}

static void test_obstacle_wall_is_routed_around(void)
{
    open_field();
    for (int row = 0; row < 12; row++) battle.obstacle[row][20] = 1;
    place(0, 10, 3);
    send_to(0, 30 * TILE + TILE / 2, 3 * TILE + TILE / 2);
    run(600);
    CHECK(battle.combatants[0].x_millipx == 30 * TILE + TILE / 2);
    CHECK(battle.combatants[0].y_millipx == 3 * TILE + TILE / 2);
}

static void test_sealed_target_brings_combatant_to_the_wall(void)
{
    open_field();
    for (int row = 0; row < ARENA_HEIGHT_TILES; row++) battle.obstacle[row][20] = 1;
    place(0, 10, 3);
    int x = battle.combatants[0].x_millipx;
    send_to(0, 30 * TILE, 3 * TILE);
    run(600);
    CHECK(battle.combatants[0].x_millipx > 18 * TILE);
    CHECK(battle.combatants[0].x_millipx < 20 * TILE);
    CHECK(x < 18 * TILE);
}

static void test_solid_target_is_approached(void)
{
    open_field();
    battle.obstacle[3][30] = 1;
    place(0, 10, 3);
    send_to(0, 30 * TILE + TILE / 2, 3 * TILE + TILE / 2);
    run(600);
    CHECK(battle.combatants[0].x_millipx > 28 * TILE);
    int column, row;
    CHECK(find_path(&battle, 10, 3, 30, 3, &column, &row) != PATH_NONE);
    CHECK(column == 11);
}

static void test_unreachable_target_falls_back_to_closest_tile(void)
{
    open_field();
    for (int row = 0; row < ARENA_HEIGHT_TILES; row++) battle.obstacle[row][20] = 1;
    int column, row;
    CHECK(find_path(&battle, 10, 3, 30, 3, &column, &row) != PATH_NONE);
    CHECK(column == 11 && row == 3);
}

static void test_slides_along_wall_on_x(void)
{
    open_field();
    place(0, 5, 0);
    battle.combatants[0].y_millipx = COMBATANT_RADIUS_MILLIPX;
    int y = battle.combatants[0].y_millipx, x = battle.combatants[0].x_millipx;
    send_to(0, x + 6 * PX, 0);
    run(20);
    CHECK(battle.combatants[0].x_millipx > x + 3 * PX);
    CHECK(battle.combatants[0].y_millipx == y);
}

static void test_slides_along_wall_on_y(void)
{
    open_field();
    place(0, 0, 5);
    battle.combatants[0].x_millipx = COMBATANT_RADIUS_MILLIPX;
    int x = battle.combatants[0].x_millipx, y = battle.combatants[0].y_millipx;
    send_to(0, 0, y + 6 * PX);
    run(20);
    CHECK(battle.combatants[0].y_millipx > y + 3 * PX);
    CHECK(battle.combatants[0].x_millipx == x);
}

static void test_move_order_on_destroyed_part_is_ignored(void)
{
    open_field();
    Combatant *who = &battle.combatants[0];
    for (int s = 0; s < who->body.slot_count; s++) who->parts[s].order.kind = ORDER_IDLE;
    send_to(0, 10 * TILE, 10 * TILE);
    int x, y;
    CHECK(combatant_move_target(who, &x, &y) && x == 10 * TILE && y == 10 * TILE);
    who->parts[who->body.core].integrity = 0;
    CHECK(!combatant_move_target(who, &x, &y));
}

static void test_combatants_do_not_overlap(void)
{
    open_field();
    place(0, 5, 5);
    place(1, 8, 5);
    send_to(0, 20 * TILE, 5 * TILE + TILE / 2);
    send_to(1, 5 * TILE, 5 * TILE + TILE / 2);
    run(200);
    int dx = battle.combatants[0].x_millipx - battle.combatants[1].x_millipx;
    int dy = battle.combatants[0].y_millipx - battle.combatants[1].y_millipx;
    CHECK((long long)dx * dx + (long long)dy * dy >= (long long)4 * COMBATANT_RADIUS_MILLIPX * COMBATANT_RADIUS_MILLIPX);
}

static void test_path_is_deterministic_and_shortest(void)
{
    open_field();
    int column, row;
    CHECK(find_path(&battle, 5, 5, 8, 5, &column, &row) == 30);
    CHECK(column == 6 && row == 5);
    battle.obstacle[5][7] = 1;
    CHECK(find_path(&battle, 5, 5, 8, 5, &column, &row) == 50);
    CHECK(column == 5 && row == 6);
}

static void wall_with_gap(int gap_tiles)
{
    for (int row = 0; row < ARENA_HEIGHT_TILES; row++) battle.obstacle[row][20] = 1;
    for (int row = 10; row < 10 + gap_tiles; row++) battle.obstacle[row][20] = 0;
}

static void test_one_tile_gap_is_too_narrow_to_path_through(void)
{
    open_field();
    wall_with_gap(1);
    int column, row;
    CHECK(find_path(&battle, 18, 10, 30, 10, &column, &row) == PATH_NONE);
    place(0, 10, 10);
    send_to(0, 30 * TILE + TILE / 2, 10 * TILE + TILE / 2);
    run(1200);
    CHECK(battle.combatants[0].x_millipx < 20 * TILE - COMBATANT_RADIUS_MILLIPX + 1);
}

static void test_two_tile_gap_is_wide_enough(void)
{
    open_field();
    wall_with_gap(2);
    int column, row;
    CHECK(find_path(&battle, 18, 10, 30, 10, &column, &row) != PATH_NONE);
    place(0, 10, 10);
    send_to(0, 30 * TILE + TILE / 2, 11 * TILE);
    run(1200);
    CHECK(battle.combatants[0].x_millipx == 30 * TILE + TILE / 2);
}

static void test_blocked_by_ally_slides_around(void)
{
    open_field();
    for (int c = 2; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    place(0, 5, 10);
    place(1, 8, 10);
    set_side(0, SIDE_PANTHEON);
    set_side(1, SIDE_PANTHEON);
    send_to(0, 12 * TILE + TILE / 2, 10 * TILE + TILE / 2);
    run(300);
    CHECK(battle.combatants[0].x_millipx == 12 * TILE + TILE / 2);
    CHECK(battle.combatants[0].y_millipx == 10 * TILE + TILE / 2);
}

static void test_allies_swap_sides_through_a_two_tile_gap(void)
{
    open_field();
    for (int c = 2; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    for (int row = 0; row < ARENA_HEIGHT_TILES; row++) battle.obstacle[row][15] = row == 10 || row == 11 ? 0 : 1;
    set_side(0, SIDE_PANTHEON);
    set_side(1, SIDE_PANTHEON);
    battle.combatants[0].x_millipx = 168 * PX;
    battle.combatants[0].y_millipx = 176 * PX;
    battle.combatants[1].x_millipx = 320 * PX;
    battle.combatants[1].y_millipx = 176 * PX;
    send_to(0, 400 * PX, 176 * PX);
    send_to(1, 80 * PX, 176 * PX);
    run(600);
    CHECK(battle.combatants[0].x_millipx > 16 * TILE);
    CHECK(battle.combatants[1].x_millipx < 15 * TILE);
}

static void test_combatant_beside_obstacle_corner_still_moves(void)
{
    open_field();
    battle.obstacle[10][10] = 1;
    place(0, 0, 0);
    battle.combatants[0].x_millipx = battle.combatants[0].y_millipx = 152000;
    send_to(0, 40000, 40000);
    run(200);
    CHECK(battle.combatants[0].x_millipx == 40000 && battle.combatants[0].y_millipx == 40000);
    battle.combatants[0].x_millipx = battle.combatants[0].y_millipx = 183999;
    send_to(0, 400000, 400000);
    run(400);
    CHECK(battle.combatants[0].x_millipx == 400000 && battle.combatants[0].y_millipx == 400000);
}

static void overlap_pair_for_one_tick(int first_x, int second_x, int y)
{
    battle.combatants[0].x_millipx = first_x;
    battle.combatants[1].x_millipx = second_x;
    battle.combatants[0].y_millipx = battle.combatants[1].y_millipx = y;
    run(1);
}

static void test_push_apart_never_pushes_into_terrain(void)
{
    open_field();
    overlap_pair_for_one_tick(COMBATANT_RADIUS_MILLIPX, COMBATANT_RADIUS_MILLIPX + 12 * PX, 100 * PX);
    CHECK(battle.combatants[0].x_millipx >= COMBATANT_RADIUS_MILLIPX);
    open_field();
    battle.obstacle[10][10] = 1;
    int edge = 10 * TILE;
    overlap_pair_for_one_tick(edge - COMBATANT_RADIUS_MILLIPX - 12 * PX, edge - COMBATANT_RADIUS_MILLIPX, 10 * TILE + TILE / 2);
    CHECK(battle.combatants[1].x_millipx <= edge - COMBATANT_RADIUS_MILLIPX);
}

static void test_combatants_at_the_same_spot_are_pushed_apart(void)
{
    open_field();
    place(0, 5, 5);
    place(1, 5, 5);
    run(1);
    int dx = battle.combatants[0].x_millipx - battle.combatants[1].x_millipx;
    int dy = battle.combatants[0].y_millipx - battle.combatants[1].y_millipx;
    CHECK((long long)dx * dx + (long long)dy * dy >= (long long)22 * PX * 22 * PX);
}

static void test_obstacle_blocks_line_of_sight(void)
{
    open_field();
    CHECK(line_of_sight(&battle, 2 * TILE, 5 * TILE, 20 * TILE, 5 * TILE));
    battle.obstacle[5][10] = 1;
    CHECK(!line_of_sight(&battle, 2 * TILE, 5 * TILE + TILE / 2, 20 * TILE, 5 * TILE + TILE / 2));
    CHECK(line_of_sight(&battle, 2 * TILE, 8 * TILE, 20 * TILE, 8 * TILE));
}

static void test_line_of_sight_blocked_at_diagonal_joint(void)
{
    open_field();
    battle.obstacle[10][10] = 1;
    battle.obstacle[11][11] = 1;
    CHECK(!line_of_sight(&battle, 146000, 206000, 206000, 146000));
    CHECK(!line_of_sight(&battle, 206000, 146000, 146000, 206000));
}

static void test_line_of_sight_blocked_when_either_corner_neighbour_is_solid(void)
{
    open_field();
    battle.obstacle[10][10] = 1;
    CHECK(!line_of_sight(&battle, 170000, 182000, 182000, 170000));
    battle.obstacle[10][10] = 0;
    battle.obstacle[11][11] = 1;
    CHECK(!line_of_sight(&battle, 170000, 182000, 182000, 170000));
    battle.obstacle[11][11] = 0;
    CHECK(line_of_sight(&battle, 170000, 182000, 182000, 170000));
}

static void test_line_of_sight_blocked_by_clipped_corner(void)
{
    open_field();
    battle.obstacle[10][10] = 1;
    CHECK(!line_of_sight(&battle, 150000, 171000, 171000, 150000));
    CHECK(line_of_sight(&battle, 150000, 180000, 171000, 150000 + 40000));
}

static void test_line_of_sight_reaches_tile_corner_endpoint(void)
{
    open_field();
    CHECK(line_of_sight(&battle, 2 * TILE, 2 * TILE, 7 * TILE, 5 * TILE));
    CHECK(line_of_sight(&battle, 7 * TILE, 5 * TILE, 2 * TILE, 2 * TILE));
    CHECK(line_of_sight(&battle, 5 * TILE + 1, 5 * TILE + 1, 5 * TILE + 1, 5 * TILE + 1));
}

static void test_combatant_bands(void)
{
    open_field();
    give_vision(0, 48, 96, 160);
    place(0, 5, 5);
    place(1, 5, 5);
    battle.combatants[1].x_millipx += 40 * PX;
    battle.combatants[2].x_millipx = battle.combatants[0].x_millipx + 90 * PX;
    battle.combatants[2].y_millipx = battle.combatants[0].y_millipx;
    battle.combatants[3].x_millipx = battle.combatants[0].x_millipx + 150 * PX;
    battle.combatants[3].y_millipx = battle.combatants[0].y_millipx;
    battle.combatants[4].x_millipx = battle.combatants[0].x_millipx + 120 * PX;
    battle.combatants[4].y_millipx = battle.combatants[0].y_millipx;
    battle.combatants[5].x_millipx = battle.combatants[0].x_millipx + 170 * PX;
    battle.combatants[5].y_millipx = battle.combatants[0].y_millipx;
    battle.combatants[6].alive = false;
    battle.combatants[7].alive = false;
    for (int c = 1; c < 6; c++) battle.combatants[c].y_millipx = battle.combatants[0].y_millipx;
    set_side(1, SIDE_PANTHEON);
    set_side(2, SIDE_PANTHEON);
    for (int c = 3; c < 6; c++) set_side(c, SIDE_HORDE);
    battle.combatants[2].vitality = battle.combatants[2].vitality_max / 2;

    static Perception seen;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 4);
    for (int i = 0; i < seen.seen_count; i++) {
        const Perceived *p = &seen.seen[i];
        const Combatant *seen_combatant = &battle.combatants[p->index];
        CHECK(p->x_millipx == seen_combatant->x_millipx && p->y_millipx == seen_combatant->y_millipx);
        CHECK((p->band == BAND_FAR) == (p->name == NULL));
        if (p->name) CHECK(strcmp(p->name, seen_combatant->name) == 0);
        if (p->index == 1) CHECK(p->band == BAND_CLOSE && p->side == SIDE_PANTHEON && p->health_quarters == 4);
        if (p->index == 2) CHECK(p->band == BAND_MEDIUM && p->side == SIDE_PANTHEON && p->health_quarters == 2);
        if (p->index == 3) CHECK(p->band == BAND_FAR && p->side == UNKNOWN && p->health_quarters == UNKNOWN);
        if (p->index == 4) CHECK(p->band == BAND_FAR);
        CHECK(p->index != 5);
    }
}

static void test_far_ally_is_not_perceived(void)
{
    open_field();
    give_vision(0, 48, 96, 160);
    place(0, 5, 5);
    place(1, 5, 5);
    battle.combatants[1].x_millipx = battle.combatants[0].x_millipx + 150 * PX;
    battle.combatants[1].y_millipx = battle.combatants[0].y_millipx;
    for (int c = 2; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    static Perception seen;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 0);
}

static void test_charmed_viewer_sees_far_enemies_by_charm_side(void)
{
    open_field();
    give_vision(0, 48, 96, 160);
    place(0, 5, 5);
    for (int c = 1; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    set_side(0, SIDE_PANTHEON);
    battle.combatants[0].charm_side = SIDE_HORDE;
    battle.combatants[1].alive = true;
    battle.combatants[1].x_millipx = battle.combatants[0].x_millipx + 150 * PX;
    battle.combatants[1].y_millipx = battle.combatants[0].y_millipx;
    static Perception seen;
    set_side(1, SIDE_HORDE);
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 0);
    set_side(1, SIDE_PANTHEON);
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 1);
}

static void test_respawning_floor_objects_are_hidden(void)
{
    open_field();
    give_vision(0, 48, 96, 160);
    place(0, 5, 5);
    for (int c = 1; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    int x = battle.combatants[0].x_millipx + 20 * PX, y = battle.combatants[0].y_millipx;
    battle.pickups[0] = (FloorObject){0, x, y, 0};
    battle.pickup_count = 1;
    battle.traps[0] = (FloorObject){0, x, y, 0};
    battle.trap_count = 1;
    static Perception seen;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 2);
    battle.pickups[0].respawn_in = 5;
    battle.traps[0].respawn_in = 5;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 0);
}

static void test_charmed_monster_is_an_ally_to_the_pantheon(void)
{
    open_field();
    give_vision(0, 48, 96, 160);
    place(0, 5, 5);
    for (int c = 1; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    set_side(0, SIDE_PANTHEON);
    battle.combatants[1].alive = true;
    set_side(1, SIDE_HORDE);
    battle.combatants[1].x_millipx = battle.combatants[0].x_millipx + 150 * PX;
    battle.combatants[1].y_millipx = battle.combatants[0].y_millipx;
    static Perception seen;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 1);
    battle.combatants[1].charm_side = SIDE_PANTHEON;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 0);
    battle.combatants[1].x_millipx = battle.combatants[0].x_millipx + 40 * PX;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 1 && seen.seen[0].side == SIDE_PANTHEON);
}

static void test_obstacle_hides_combatant(void)
{
    open_field();
    give_vision(0, 48, 96, 160);
    place(0, 5, 5);
    place(1, 9, 5);
    for (int c = 2; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    static Perception seen;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 1);
    battle.obstacle[5][7] = 1;
    perceive(&battle, &data, 0, &seen);
    CHECK(seen.seen_count == 0);
}

static void test_pickups_and_traps_by_band(void)
{
    open_field();
    give_vision(0, 48, 96, 160);
    place(0, 5, 5);
    for (int c = 1; c < battle.combatant_count; c++) battle.combatants[c].alive = false;
    int healing = -1, other = -1;
    for (int d = 0; d < data.pickup_count; d++) {
        if (data.pickups[d].heal > 0 && healing < 0) healing = d;
        if (data.pickups[d].heal <= 0 && other < 0) other = d;
    }
    CHECK(healing >= 0 && data.trap_count > 0);
    int y = battle.combatants[0].y_millipx, x = battle.combatants[0].x_millipx;
    battle.pickups[0] = (FloorObject){healing, x + 150 * PX, y, 0};
    battle.pickups[1] = (FloorObject){healing, x + 90 * PX, y, 0};
    battle.pickup_count = 2;
    battle.traps[0] = (FloorObject){0, x + 40 * PX, y, 0};
    battle.traps[1] = (FloorObject){0, x + 90 * PX, y, 0};
    battle.trap_count = 2;
    static Perception seen;
    perceive(&battle, &data, 0, &seen);
    int pickups = 0, traps = 0;
    for (int i = 0; i < seen.seen_count; i++) {
        const Perceived *p = &seen.seen[i];
        if (p->kind == PERCEIVED_TRAP) {
            traps++;
            CHECK(p->band == BAND_CLOSE && p->index == 0);
        }
        if (p->kind == PERCEIVED_PICKUP && p->index == 0) CHECK(p->band == BAND_FAR && p->def == UNKNOWN);
        if (p->kind == PERCEIVED_PICKUP && p->index == 1) CHECK(p->band == BAND_MEDIUM && p->def == healing);
        if (p->kind == PERCEIVED_PICKUP) pickups++;
    }
    CHECK(pickups == 2 && traps == 1);
    if (other >= 0) {
        battle.pickups[0].def = other;
        perceive(&battle, &data, 0, &seen);
        int far_pickups = 0;
        for (int i = 0; i < seen.seen_count; i++)
            if (seen.seen[i].kind == PERCEIVED_PICKUP && seen.seen[i].band == BAND_FAR) far_pickups++;
        CHECK(far_pickups == 0);
    }
}

static void test_vision_ranges_take_max_of_working_parts(void)
{
    open_field();
    give_vision(0, 10, 20, 30);
    Combatant *who = &battle.combatants[0];
    int range[3];
    combatant_vision_ranges(who, &data, range);
    CHECK(range[BAND_CLOSE] == 10 * PX && range[BAND_MEDIUM] == 20 * PX && range[BAND_FAR] == 30 * PX);
    who->parts[who->body.core].integrity = 0;
    combatant_vision_ranges(who, &data, range);
    CHECK(range[BAND_FAR] == 0);
}

void run_movement_tests(void)
{
    test_moves_at_part_speed();
    test_no_order_stands_still();
    test_crippled_movers_stop_movement();
    test_arena_wall_blocks();
    test_obstacle_wall_is_routed_around();
    test_sealed_target_brings_combatant_to_the_wall();
    test_solid_target_is_approached();
    test_unreachable_target_falls_back_to_closest_tile();
    test_slides_along_wall_on_x();
    test_slides_along_wall_on_y();
    test_move_order_on_destroyed_part_is_ignored();
    test_combatants_do_not_overlap();
    test_path_is_deterministic_and_shortest();
    test_one_tile_gap_is_too_narrow_to_path_through();
    test_two_tile_gap_is_wide_enough();
    test_combatants_at_the_same_spot_are_pushed_apart();
    test_blocked_by_ally_slides_around();
    test_allies_swap_sides_through_a_two_tile_gap();
    test_combatant_beside_obstacle_corner_still_moves();
    test_push_apart_never_pushes_into_terrain();
    test_obstacle_blocks_line_of_sight();
    test_line_of_sight_blocked_at_diagonal_joint();
    test_line_of_sight_blocked_when_either_corner_neighbour_is_solid();
    test_line_of_sight_blocked_by_clipped_corner();
    test_line_of_sight_reaches_tile_corner_endpoint();
    test_combatant_bands();
    test_far_ally_is_not_perceived();
    test_charmed_viewer_sees_far_enemies_by_charm_side();
    test_respawning_floor_objects_are_hidden();
    test_charmed_monster_is_an_ally_to_the_pantheon();
    test_obstacle_hides_combatant();
    test_pickups_and_traps_by_band();
    test_vision_ranges_take_max_of_working_parts();
    battle_free(&battle);
}
