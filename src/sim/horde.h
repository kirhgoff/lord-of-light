#ifndef SIM_HORDE_H
#define SIM_HORDE_H

#include <stdint.h>

#include "data.h"

#define HORDE_MAX_SIZE 8

#define HORDE_MAX_ATTEMPTS 32

typedef struct {
    int plan;
    Body body;
} Monster;

typedef struct {
    Monster monsters[HORDE_MAX_SIZE];
    int count;
    long power;
} Horde;

Horde horde_attempt(const GameData *data, long target_power, uint32_t *rng);
Horde generate_horde(const GameData *data, long target_power, uint32_t *rng);

#endif
