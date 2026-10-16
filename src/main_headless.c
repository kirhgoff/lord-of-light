#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "decider_setup.h"
#include "sim/battle.h"
#include "sim/decide.h"
#include "sim/movement.h"
#include "version.h"

#define ERR_SIZE 256
#define DEFAULT_TICKS 4000

static bool write_text_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fputs(text, file);
    return fclose(file) == 0;
}

static bool parse_seed(const char *text, uint32_t *seed)
{
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || end == text || *end || text[0] == '-' || value > UINT32_MAX) return false;
    *seed = (uint32_t)value;
    return true;
}

static int usage(void)
{
    fprintf(stderr, "usage: headless [--seed N] [--load state.json] [--ticks T] [--dump out.json] [--decider laya|reflex] [--replay dump.json]\n");
    return 2;
}

static void wait_one_tick(void)
{
    struct timespec tick = {0, 1000000000L / TICKS_PER_SECOND};
    nanosleep(&tick, NULL);
}

static bool same_battle(const Battle *a, const Battle *b, const GameData *data)
{
    char *left = battle_save_json(a, data), *right = battle_save_json(b, data);
    bool same = strcmp(left, right) == 0;
    free(left);
    free(right);
    return same;
}

static void print_decision_stats(const Battle *battle, int truncated)
{
    DecisionStats stats = battle_decision_stats(battle);
    printf("Decisions: %d Laya, %d Reflex (unreachable %d, timeout %d, malformed %d, low confidence %d, waiting %d); "
           "mean confidence %d.%03d; mean latency %d ms; truncated %d\n",
           stats.laya, stats.reflex, stats.fallback[FALLBACK_UNREACHABLE], stats.fallback[FALLBACK_TIMEOUT],
           stats.fallback[FALLBACK_MALFORMED], stats.fallback[FALLBACK_LOW_CONFIDENCE], stats.fallback[FALLBACK_WAITING],
           stats.mean_confidence_permille / 1000, stats.mean_confidence_permille % 1000, stats.mean_latency_ms, truncated);
}

static const char *winner_name(int winner)
{
    return winner == SIDE_PANTHEON ? "Pantheon" : winner == SIDE_HORDE ? "Horde" : "none";
}

int main(int argc, char **argv)
{
    uint32_t seed = 1;
    long ticks = DEFAULT_TICKS;
    const char *load_path = NULL, *dump_path = NULL;
    static DeciderSetup setup;
    for (int i = 1; i < argc; i++) {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--seed") == 0 && has_value) {
            if (!parse_seed(argv[++i], &seed)) return fprintf(stderr, "seed must be 0..%u\n", UINT32_MAX), 2;
        }
        else if (strcmp(argv[i], "--ticks") == 0 && has_value) ticks = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--load") == 0 && has_value) load_path = argv[++i];
        else if (strcmp(argv[i], "--dump") == 0 && has_value) dump_path = argv[++i];
        else if (!decider_setup_parse(&setup, argc, argv, &i)) return usage();
    }

    static GameData data;
    static Battle battle;
    char err[ERR_SIZE];
    if (!load_game_data(&data, "data", err, ERR_SIZE)) {
        fprintf(stderr, "data: %s\n", err);
        return 1;
    }
    if (!decider_setup_open(&setup, &data, &battle, err, ERR_SIZE)) {
        fprintf(stderr, "decider: %s\n", err);
        return 1;
    }
    if (setup.replay_path) {
        ticks = setup.source.tick;
    } else if (load_path) {
        char *json = read_text_file(load_path);
        if (!json || !battle_load_json(&battle, &data, json, err, ERR_SIZE)) {
            fprintf(stderr, "load %s: %s\n", load_path, json ? err : "cannot read file");
            return 1;
        }
        free(json);
    } else {
        battle_setup(&battle, &data, seed);
    }

    bool real_time = setup.laya && !setup.replay_path;
    for (long t = 0; t < ticks && battle_winner(&battle) < 0; t++) {
        battle_step(&battle, &data, setup.decider);
        if (real_time) wait_one_tick();
    }
    bool replay_diverged = setup.replay_path && !same_battle(&battle, &setup.source, &data);

    if (dump_path) {
        char *json = battle_save_json(&battle, &data);
        bool written = write_text_file(dump_path, json);
        free(json);
        if (!written) {
            fprintf(stderr, "cannot write %s\n", dump_path);
            decider_setup_close(&setup);
            return 1;
        }
    }
    printf("Winner: %s after %d ticks\n", winner_name(battle_winner(&battle)), battle.tick);
    if (setup.replay_path) printf("Replay: %s\n", replay_diverged ? "diverged" : "identical");
    if (setup.laya || setup.replay_path) print_decision_stats(&battle, setup.laya_decider.truncated_count);
    decider_setup_close(&setup);
    battle_free(&battle);
    return replay_diverged ? 1 : 0;
}
