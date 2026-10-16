#include "briefing.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "combat.h"
#include "decide.h"
#include "vision.h"

#define SENTENCE_MAX 256
#define LIST_MAX 160

static const char *const HURT_WORDS[HURT_COUNT] = {"unhurt", "wounded", "badly wounded", "near death"};
static const char *const BAND_WORDS[3] = {"close", "near", "far"};
static const char *const ELEMENT_WORDS[ELEMENT_COUNT] = {"Kinetic", "Fire", "Frost", "Lightning", "Radiance", "Venom", "Mind", "Void"};

typedef struct {
    char *out;
    size_t len;
    size_t limit;
    bool full;
} Writer;

typedef struct {
    char text[NAME_LEN + 8];
} Subject;

typedef struct {
    const Perceived *seen;
    int64_t key;
    int64_t tie;
} Ranked;

typedef struct {
    OrderKind order;
    Element element;
} AttackKind;

static Subject subject_of(const char *name)
{
    Subject subject;
    bool is_monster = islower((unsigned char)name[0]);
    snprintf(subject.text, sizeof subject.text, is_monster ? "the %s" : "%s", name);
    for (char *c = subject.text; is_monster && *c; c++)
        if (*c == '_') *c = ' ';
    return subject;
}

static void say(Writer *writer, const char *format, ...)
{
    char sentence[SENTENCE_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(sentence, sizeof sentence, format, args);
    va_end(args);
    sentence[0] = (char)toupper((unsigned char)sentence[0]);
    size_t needed = writer->len + (writer->len ? 1 : 0) + strlen(sentence);
    if (writer->full || needed > writer->limit) {
        writer->full = true;
        return;
    }
    char *at = writer->out + writer->len;
    if (writer->len) *at++ = ' ';
    memcpy(at, sentence, strlen(sentence) + 1);
    writer->len = needed;
}

static const char *compass(const Combatant *me, const Perceived *seen)
{
    int64_t dx = (int64_t)seen->x_millipx - me->x_millipx, dy = (int64_t)seen->y_millipx - me->y_millipx;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) return dx > 0 ? "east" : "west";
    return dy > 0 ? "south" : "north";
}

static int64_t distance_squared(const Combatant *me, const Perceived *seen)
{
    int64_t dx = (int64_t)seen->x_millipx - me->x_millipx, dy = (int64_t)seen->y_millipx - me->y_millipx;
    return dx * dx + dy * dy;
}

static int compare_ranked(const void *a, const void *b)
{
    const Ranked *left = a, *right = b;
    if (left->key != right->key) return left->key < right->key ? -1 : 1;
    if (left->tie != right->tie) return left->tie < right->tie ? -1 : 1;
    return left->seen->index - right->seen->index;
}

static int working_attacks(const Combatant *me, const GameData *data, AttackKind attacks[MAX_SLOTS * MAX_ABILITIES])
{
    bool working[MAX_SLOTS];
    int count = 0;
    body_working_parts(&me->body, me->parts, working);
    for (int s = 0; s < me->body.slot_count; s++) {
        const Part *part = &data->parts[me->body.slots[s].part];
        for (int a = 0; working[s] && a < part->ability_count; a++) {
            const Ability *ability = &part->abilities[a];
            if (ability->order != ORDER_STRIKE && ability->order != ORDER_CAST) continue;
            bool known = false;
            for (int k = 0; k < count; k++) known |= attacks[k].order == ability->order && attacks[k].element == ability->element;
            if (!known) attacks[count++] = (AttackKind){ability->order, ability->element};
        }
    }
    return count;
}

static int distinct_attack_elements(const AttackKind *attacks, int count, Element elements[ELEMENT_COUNT])
{
    int distinct = 0;
    for (int k = 0; k < count; k++) {
        bool known = false;
        for (int e = 0; e < distinct; e++) known |= elements[e] == attacks[k].element;
        if (!known) elements[distinct++] = attacks[k].element;
    }
    return distinct;
}

static void join_list(char *out, const char (*items)[LIST_MAX], int count)
{
    out[0] = '\0';
    for (int i = 0; i < count; i++) {
        const char *separator = i == 0 ? "" : i == count - 1 ? " and " : ", ";
        strcat(out, separator);
        strcat(out, items[i]);
    }
}

static void say_abilities(Writer *writer, const Combatant *me, const GameData *data, const Subject *self)
{
    AttackKind attacks[MAX_SLOTS * MAX_ABILITIES];
    int count = working_attacks(me, data, attacks);
    if (count == 0) {
        say(writer, "%s cannot attack.", self->text);
        return;
    }
    char items[MAX_SLOTS * MAX_ABILITIES][LIST_MAX], list[MAX_SLOTS * MAX_ABILITIES * LIST_MAX];
    for (int k = 0; k < count; k++)
        snprintf(items[k], LIST_MAX, attacks[k].order == ORDER_STRIKE ? "strike with %s" : "cast %s", ELEMENT_WORDS[attacks[k].element]);
    join_list(list, (const char (*)[LIST_MAX])items, count);
    say(writer, "%s can %s.", self->text, list);
}

static void say_resistances(Writer *writer, const GameData *data, const Combatant *me, const Combatant *focus)
{
    AttackKind attacks[MAX_SLOTS * MAX_ABILITIES];
    Element elements[ELEMENT_COUNT];
    int distinct = distinct_attack_elements(attacks, working_attacks(me, data, attacks), elements);
    if (distinct < 2) return;
    int high = 0, low = 0, high_resistance = 0, low_resistance = 0;
    for (int e = 0; e < distinct; e++) {
        int resistance = combatant_resistance_percent(focus, data, elements[e], elements[e] == ELEMENT_RADIANCE);
        if (e == 0 || resistance > high_resistance) high = e, high_resistance = resistance;
        if (e == 0 || resistance < low_resistance) low = e, low_resistance = resistance;
    }
    Subject who = subject_of(focus->name);
    if (high_resistance == low_resistance) return;
    if (high_resistance > 0) say(writer, "%s resists %s and is weak to %s.", who.text, ELEMENT_WORDS[elements[high]], ELEMENT_WORDS[elements[low]]);
    else say(writer, "%s is weak to %s.", who.text, ELEMENT_WORDS[elements[low]]);
}

static int rank(const Combatant *me, const Perception *perception, PerceivedKind kind, bool allies, Ranked ranked[MAX_PERCEIVED])
{
    int count = 0;
    for (int i = 0; i < perception->seen_count; i++) {
        const Perceived *seen = &perception->seen[i];
        if (seen->kind != kind) continue;
        if (kind == PERCEIVED_COMBATANT && perceived_is_ally(me, seen) != allies) continue;
        if (kind == PERCEIVED_COMBATANT && !allies && !perceived_is_enemy(me, seen)) continue;
        int64_t distance = distance_squared(me, seen);
        ranked[count++] = allies ? (Ranked){seen, seen->health_quarters, distance} : (Ranked){seen, distance, 0};
    }
    qsort(ranked, (size_t)count, sizeof ranked[0], compare_ranked);
    return count;
}

static void say_enemies(Writer *writer, const Battle *battle, const GameData *data, const Combatant *me, const Perception *perception)
{
    Ranked ranked[MAX_PERCEIVED];
    int count = rank(me, perception, PERCEIVED_COMBATANT, false, ranked);
    for (int i = 0; i < count; i++) {
        const Perceived *seen = ranked[i].seen;
        if (seen->band == BAND_FAR) {
            say(writer, i == 0 ? "An enemy is far to the %s." : "Another enemy is far to the %s.", compass(me, seen));
            continue;
        }
        Subject who = subject_of(seen->name);
        say(writer, "%s, an enemy, is %s to the %s and %s.", who.text, BAND_WORDS[seen->band], compass(me, seen),
            HURT_WORDS[hurt_level_from_quarters(seen->health_quarters)]);
        if (i == 0 && seen->band == BAND_CLOSE) say_resistances(writer, data, me, &battle->combatants[seen->index]);
    }
}

static void say_allies(Writer *writer, const Battle *battle, const GameData *data, const Combatant *me, const Perception *perception)
{
    Ranked ranked[MAX_PERCEIVED];
    int count = rank(me, perception, PERCEIVED_COMBATANT, true, ranked);
    for (int i = 0; i < count; i++) {
        const Perceived *seen = ranked[i].seen;
        Subject who = subject_of(seen->name);
        say(writer, "%s, an ally, is %s to the %s and %s.", who.text, BAND_WORDS[seen->band], compass(me, seen),
            HURT_WORDS[hurt_level_from_quarters(seen->health_quarters)]);
        if (combatant_has_working_support(&battle->combatants[seen->index], data)) say(writer, "%s can heal.", who.text);
    }
}

static void lower_case(char *text)
{
    for (; *text; text++) *text = (char)tolower((unsigned char)*text);
}

static void say_pickups(Writer *writer, const GameData *data, const Combatant *me, const Perception *perception)
{
    Ranked ranked[MAX_PERCEIVED];
    int count = rank(me, perception, PERCEIVED_PICKUP, false, ranked);
    for (int i = 0; i < count; i++) {
        const Perceived *seen = ranked[i].seen;
        if (seen->band == BAND_FAR || data->pickups[seen->def].heal > 0) {
            say(writer, "A healing pickup is %s to the %s.", BAND_WORDS[seen->band], compass(me, seen));
            continue;
        }
        char name[NAME_LEN];
        snprintf(name, sizeof name, "%s", data->pickups[seen->def].name);
        lower_case(name);
        say(writer, "A %s pickup is %s to the %s.", name, BAND_WORDS[seen->band], compass(me, seen));
    }
}

static void say_traps(Writer *writer, const GameData *data, const Combatant *me, const Perception *perception)
{
    Ranked ranked[MAX_PERCEIVED];
    int count = rank(me, perception, PERCEIVED_TRAP, false, ranked);
    for (int i = 0; i < count; i++) {
        char name[NAME_LEN];
        snprintf(name, sizeof name, "%s", data->traps[ranked[i].seen->def].name);
        lower_case(name);
        say(writer, "A %s trap is close to the %s.", name, compass(me, ranked[i].seen));
    }
}

int briefing_write(const Battle *battle, const GameData *data, int combatant, char *out, size_t size)
{
    const Combatant *me = &battle->combatants[combatant];
    Perception perception;
    size_t limit = size > BRIEFING_MAX ? BRIEFING_MAX : size - 1;
    Writer writer = {out, 0, limit, false};
    Subject self = subject_of(me->name);
    out[0] = '\0';
    perceive(battle, data, combatant, &perception);
    say(&writer, "%s is %s.", self.text, HURT_WORDS[hurt_level_from_quarters(combatant_health_quarters(me))]);
    say_abilities(&writer, me, data, &self);
    if (combatant_has_working_support(me, data)) say(&writer, "%s can heal an ally.", self.text);
    say_enemies(&writer, battle, data, me, &perception);
    say_allies(&writer, battle, data, me, &perception);
    say_pickups(&writer, data, me, &perception);
    say_traps(&writer, data, me, &perception);
    return (int)writer.len;
}
