#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/battle.h"
#include "sim/decide.h"
#include "version.h"

#define ERR_SIZE 256
#define DEFAULT_TICKS 4000

static char *read_text_file(const char *path)
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
    fprintf(stderr, "usage: headless --seed N [--load state.json] [--ticks T] [--dump out.json]\n");
    return 2;
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
    for (int i = 1; i < argc; i++) {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--seed") == 0 && has_value) {
            if (!parse_seed(argv[++i], &seed)) return fprintf(stderr, "seed must be 0..%u\n", UINT32_MAX), 2;
        }
        else if (strcmp(argv[i], "--ticks") == 0 && has_value) ticks = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--load") == 0 && has_value) load_path = argv[++i];
        else if (strcmp(argv[i], "--dump") == 0 && has_value) dump_path = argv[++i];
        else return usage();
    }

    static GameData data;
    static Battle battle;
    char err[ERR_SIZE];
    if (!load_game_data(&data, "data", err, ERR_SIZE)) {
        fprintf(stderr, "data: %s\n", err);
        return 1;
    }
    if (load_path) {
        char *json = read_text_file(load_path);
        if (!json || !battle_load_json(&battle, &data, json, err, ERR_SIZE)) {
            fprintf(stderr, "load %s: %s\n", load_path, json ? err : "cannot read file");
            return 1;
        }
        free(json);
    } else {
        battle_setup(&battle, &data, seed);
    }

    Decider reflex = reflex_decider();
    for (long t = 0; t < ticks && battle_winner(&battle) < 0; t++) battle_step(&battle, &data, &reflex);

    if (dump_path) {
        char *json = battle_save_json(&battle, &data);
        bool written = write_text_file(dump_path, json);
        free(json);
        if (!written) {
            fprintf(stderr, "cannot write %s\n", dump_path);
            return 1;
        }
    }
    battle_free(&battle);
    printf("Winner: %s after %d ticks\n", winner_name(battle_winner(&battle)), battle.tick);
    return 0;
}
