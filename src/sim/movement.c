#include "movement.h"

#include <stdint.h>
#include <stdlib.h>

#define TILE_COUNT (ARENA_WIDTH_TILES * ARENA_HEIGHT_TILES)
#define STRAIGHT_COST 10
#define DIAGONAL_COST 14
#define NEIGHBOUR_COUNT 8
#define UNREACHED (INT32_MAX / 2)

static const int neighbour_dx[NEIGHBOUR_COUNT] = {0, 1, 0, -1, 1, 1, -1, -1};
static const int neighbour_dy[NEIGHBOUR_COUNT] = {-1, 0, 1, 0, -1, 1, 1, -1};

bool tile_is_solid(const Battle *battle, int column, int row)
{
    if (column < 0 || row < 0 || column >= ARENA_WIDTH_TILES || row >= ARENA_HEIGHT_TILES) return true;
    return battle->obstacle[row][column];
}

int combatant_speed_px_per_second(const Combatant *combatant, const GameData *data)
{
    bool working[MAX_SLOTS];
    body_working_parts(&combatant->body, combatant->parts, working);
    int speed = 0;
    for (int s = 0; s < combatant->body.slot_count; s++) {
        int part_speed = data->parts[combatant->body.slots[s].part].move_speed_px_per_second;
        if (working[s] && part_speed > speed) speed = part_speed;
    }
    return speed;
}

bool combatant_move_target(const Combatant *combatant, int *x_millipx, int *y_millipx)
{
    bool working[MAX_SLOTS];
    body_working_parts(&combatant->body, combatant->parts, working);
    for (int s = 0; s < combatant->body.slot_count; s++) {
        if (!working[s] || combatant->parts[s].order.kind != ORDER_MOVE_TO) continue;
        *x_millipx = combatant->parts[s].order.x_millipx;
        *y_millipx = combatant->parts[s].order.y_millipx;
        return true;
    }
    return false;
}

static int heuristic(int column, int row, int to_column, int to_row)
{
    int dx = abs(column - to_column), dy = abs(row - to_row);
    int diagonal = dx < dy ? dx : dy;
    return DIAGONAL_COST * diagonal + STRAIGHT_COST * (dx + dy - 2 * diagonal);
}

static bool footprint_is_blocked(const Battle *battle, int column, int row)
{
    return tile_is_solid(battle, column, row) || tile_is_solid(battle, column + 1, row) ||
           tile_is_solid(battle, column, row + 1) || tile_is_solid(battle, column + 1, row + 1);
}

static bool step_is_open(const Battle *battle, int column, int row, int dir)
{
    int nc = column + neighbour_dx[dir], nr = row + neighbour_dy[dir];
    if (footprint_is_blocked(battle, nc, nr)) return false;
    if (dir < 4) return true;
    return !footprint_is_blocked(battle, nc, row) && !footprint_is_blocked(battle, column, nr);
}

static int pop_best(int *open, int *open_count, const int *cost, const int *estimate)
{
    int best = 0;
    for (int i = 1; i < *open_count; i++) {
        int f = cost[open[i]] + estimate[open[i]], best_f = cost[open[best]] + estimate[open[best]];
        if (f < best_f || (f == best_f && open[i] < open[best])) best = i;
    }
    int node = open[best];
    open[best] = open[--*open_count];
    return node;
}

static int closest_reachable_tile(const Battle *battle, int start, int to_column, int to_row)
{
    int queue[TILE_COUNT], head = 0, tail = 0, best = start;
    bool reached[TILE_COUNT] = {false};
    int best_estimate = heuristic(start % ARENA_WIDTH_TILES, start / ARENA_WIDTH_TILES, to_column, to_row);
    queue[tail++] = start;
    reached[start] = true;
    while (head < tail) {
        int node = queue[head++];
        int column = node % ARENA_WIDTH_TILES, row = node / ARENA_WIDTH_TILES;
        int estimate = heuristic(column, row, to_column, to_row);
        if (estimate < best_estimate || (estimate == best_estimate && node < best)) best = node, best_estimate = estimate;
        for (int dir = 0; dir < NEIGHBOUR_COUNT; dir++) {
            int next = (row + neighbour_dy[dir]) * ARENA_WIDTH_TILES + column + neighbour_dx[dir];
            if (!step_is_open(battle, column, row, dir) || reached[next]) continue;
            reached[next] = true;
            queue[tail++] = next;
        }
    }
    return best;
}

int find_path(const Battle *battle, int from_column, int from_row, int to_column, int to_row, int *next_column, int *next_row)
{
    if (footprint_is_blocked(battle, from_column, from_row)) return PATH_NONE;
    int start = from_row * ARENA_WIDTH_TILES + from_column;
    int goal = closest_reachable_tile(battle, start, to_column, to_row);
    if (goal == start) return PATH_NONE;
    to_column = goal % ARENA_WIDTH_TILES;
    to_row = goal / ARENA_WIDTH_TILES;
    int cost[TILE_COUNT], estimate[TILE_COUNT], parent[TILE_COUNT], open[TILE_COUNT], open_count = 0;
    bool closed[TILE_COUNT] = {false};
    for (int i = 0; i < TILE_COUNT; i++) cost[i] = UNREACHED, parent[i] = PATH_NONE;
    cost[start] = 0;
    estimate[start] = heuristic(from_column, from_row, to_column, to_row);
    open[open_count++] = start;
    while (open_count > 0) {
        int node = pop_best(open, &open_count, cost, estimate);
        if (node == goal) break;
        closed[node] = true;
        int column = node % ARENA_WIDTH_TILES, row = node / ARENA_WIDTH_TILES;
        for (int dir = 0; dir < NEIGHBOUR_COUNT; dir++) {
            if (!step_is_open(battle, column, row, dir)) continue;
            int nc = column + neighbour_dx[dir], nr = row + neighbour_dy[dir], next = nr * ARENA_WIDTH_TILES + nc;
            int new_cost = cost[node] + (dir < 4 ? STRAIGHT_COST : DIAGONAL_COST);
            if (closed[next] || new_cost >= cost[next]) continue;
            bool queued = cost[next] != UNREACHED;
            cost[next] = new_cost;
            estimate[next] = heuristic(nc, nr, to_column, to_row);
            parent[next] = node;
            if (!queued) open[open_count++] = next;
        }
    }
    int node = goal;
    while (node != start && parent[node] != start) node = parent[node];
    *next_column = node % ARENA_WIDTH_TILES;
    *next_row = node / ARENA_WIDTH_TILES;
    return cost[goal];
}

static int64_t squared(int64_t value)
{
    return value * value;
}

static int64_t isqrt(int64_t value)
{
    int64_t root = 0, bit = (int64_t)1 << 62;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= root + bit) value -= root + bit, root = (root >> 1) + bit;
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static bool overlaps_tile(const Battle *battle, int x_millipx, int y_millipx, int column, int row)
{
    if (!tile_is_solid(battle, column, row)) return false;
    int left = column * TILE_MILLIPX, top = row * TILE_MILLIPX;
    int nearest_x = clamp(x_millipx, left, left + TILE_MILLIPX), nearest_y = clamp(y_millipx, top, top + TILE_MILLIPX);
    return squared(x_millipx - nearest_x) + squared(y_millipx - nearest_y) < squared(COMBATANT_RADIUS_MILLIPX);
}

static bool hits_terrain(const Battle *battle, int x_millipx, int y_millipx)
{
    const int r = COMBATANT_RADIUS_MILLIPX;
    if (x_millipx < r || y_millipx < r || x_millipx > ARENA_WIDTH_MILLIPX - r || y_millipx > ARENA_HEIGHT_MILLIPX - r) return true;
    for (int row = (y_millipx - r) / TILE_MILLIPX; row <= (y_millipx + r) / TILE_MILLIPX; row++)
        for (int column = (x_millipx - r) / TILE_MILLIPX; column <= (x_millipx + r) / TILE_MILLIPX; column++)
            if (overlaps_tile(battle, x_millipx, y_millipx, column, row)) return true;
    return false;
}

static int64_t distance_squared(int ax, int ay, int bx, int by)
{
    return squared(ax - bx) + squared(ay - by);
}

static bool hits_combatant(const Battle *battle, int self, int x_millipx, int y_millipx)
{
    const Combatant *me = &battle->combatants[self];
    int64_t touching = squared(2 * COMBATANT_RADIUS_MILLIPX);
    for (int c = 0; c < battle->combatant_count; c++) {
        const Combatant *other = &battle->combatants[c];
        if (c == self || !other->alive) continue;
        int64_t after = distance_squared(x_millipx, y_millipx, other->x_millipx, other->y_millipx);
        int64_t before = distance_squared(me->x_millipx, me->y_millipx, other->x_millipx, other->y_millipx);
        if (after < touching && after < before) return true;
    }
    return false;
}

static bool try_move(Battle *battle, int self, int dx, int dy)
{
    Combatant *me = &battle->combatants[self];
    int x = me->x_millipx + dx, y = me->y_millipx + dy;
    if (hits_terrain(battle, x, y) || hits_combatant(battle, self, x, y)) return false;
    me->x_millipx = x;
    me->y_millipx = y;
    return true;
}

static void move_towards(Battle *battle, int self, int goal_x, int goal_y, int step)
{
    const Combatant *me = &battle->combatants[self];
    int dx = goal_x - me->x_millipx, dy = goal_y - me->y_millipx;
    int64_t distance = isqrt(squared(dx) + squared(dy));
    if (distance == 0) return;
    if (distance > step) dx = (int)(dx * step / distance), dy = (int)(dy * step / distance);
    if (try_move(battle, self, dx, dy)) return;
    if (!try_move(battle, self, dx, 0)) try_move(battle, self, 0, dy);
}

static int anchor_of(int millipx, int last)
{
    return clamp((millipx + TILE_MILLIPX / 2) / TILE_MILLIPX - 1, 0, last);
}

static void free_anchor_near(const Battle *battle, int x_millipx, int y_millipx, int *column, int *row)
{
    int nearest_column = anchor_of(x_millipx, ARENA_WIDTH_TILES - 1), nearest_row = anchor_of(y_millipx, ARENA_HEIGHT_TILES - 1);
    *column = nearest_column;
    *row = nearest_row;
    if (!footprint_is_blocked(battle, nearest_column, nearest_row)) return;
    int64_t best = INT64_MAX;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int c = clamp(nearest_column + dx, 0, ARENA_WIDTH_TILES - 1), r = clamp(nearest_row + dy, 0, ARENA_HEIGHT_TILES - 1);
            int64_t away = distance_squared(x_millipx, y_millipx, (c + 1) * TILE_MILLIPX, (r + 1) * TILE_MILLIPX);
            if (footprint_is_blocked(battle, c, r) || away >= best) continue;
            best = away, *column = c, *row = r;
        }
}

static void advance(Battle *battle, int self, int target_x, int target_y, int step)
{
    const Combatant *me = &battle->combatants[self];
    target_x = clamp(target_x, 0, ARENA_WIDTH_MILLIPX - 1);
    target_y = clamp(target_y, 0, ARENA_HEIGHT_MILLIPX - 1);
    int column, row;
    free_anchor_near(battle, me->x_millipx, me->y_millipx, &column, &row);
    int target_column = anchor_of(target_x, ARENA_WIDTH_TILES - 1), target_row = anchor_of(target_y, ARENA_HEIGHT_TILES - 1);
    int next_column = target_column, next_row = target_row;
    bool same_anchor = column == target_column && row == target_row;
    if (!same_anchor && find_path(battle, column, row, target_column, target_row, &next_column, &next_row) == PATH_NONE) return;
    bool final_leg = same_anchor || (next_column == target_column && next_row == target_row);
    int goal_x = final_leg ? target_x : (next_column + 1) * TILE_MILLIPX;
    int goal_y = final_leg ? target_y : (next_row + 1) * TILE_MILLIPX;
    move_towards(battle, self, goal_x, goal_y, step);
}

static void push_apart(Battle *battle, int first, int second)
{
    Combatant *a = &battle->combatants[first], *b = &battle->combatants[second];
    int64_t dx = b->x_millipx - a->x_millipx, dy = b->y_millipx - a->y_millipx;
    int64_t touching = 2 * COMBATANT_RADIUS_MILLIPX, gap = isqrt(squared(dx) + squared(dy));
    if (gap >= touching) return;
    int64_t shift = (touching - gap) / 2 + 2;
    int sx = gap ? (int)(dx * shift / gap) : (int)shift, sy = gap ? (int)(dy * shift / gap) : 0;
    if (!hits_terrain(battle, a->x_millipx - sx, a->y_millipx - sy)) a->x_millipx -= sx, a->y_millipx -= sy;
    if (!hits_terrain(battle, b->x_millipx + sx, b->y_millipx + sy)) b->x_millipx += sx, b->y_millipx += sy;
}

static void separate_combatants(Battle *battle)
{
    for (int a = 0; a < battle->combatant_count; a++)
        for (int b = a + 1; b < battle->combatant_count; b++)
            if (battle->combatants[a].alive && battle->combatants[b].alive) push_apart(battle, a, b);
}

void battle_move_combatants(Battle *battle, const GameData *data)
{
    separate_combatants(battle);
    for (int c = 0; c < battle->combatant_count; c++) {
        const Combatant *me = &battle->combatants[c];
        int target_x, target_y;
        if (!me->alive || !combatant_move_target(me, &target_x, &target_y)) continue;
        int step = combatant_speed_px_per_second(me, data) * MILLIPX_PER_PX / TICKS_PER_SECOND;
        if (me->slow_ticks > 0) step = step * (100 - me->slow_percent) / 100;
        if (step > 0) advance(battle, c, target_x, target_y, step);
    }
}
