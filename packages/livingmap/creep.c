/* packages/livingmap/creep.c -- see creep.h for the real design rationale. */
#include "creep.h"

#include <string.h>

void creep_registry_init(CreepRegistry *reg) {
    memset(reg, 0, sizeof(*reg));
    reg->creep_count = 0;
}

static int creep_spawn_internal(CreepRegistry *reg, LivingMapEventLog *log, HexCoord home,
                                 int faction_owner, int max_hp, int passive) {
    if (reg->creep_count >= LIVING_MAP_CREEP_MAX_COUNT) return -1;

    int id = reg->creep_count;
    LivingMapCreep *c = &reg->creeps[id];
    c->active = 1;
    c->alive = 1;
    c->home = home;
    c->pos = home;
    c->faction_owner = faction_owner;
    c->hp = max_hp;
    c->max_hp = max_hp;
    c->state = LIVING_MAP_CREEP_IDLE;
    c->chase_target = -1;
    c->attack_cooldown_ms = 0;
    c->move_timer_ms = passive ? LIVING_MAP_COW_WANDER_INTERVAL_MS : LIVING_MAP_CREEP_MOVE_INTERVAL_MS;
    c->passive = passive;
    c->wander_step_count = 0;

    reg->creep_count = id + 1;
    living_map_emit(log, LIVING_MAP_EVENT_CREEP_SPAWNED, id, faction_owner, max_hp);
    return id;
}

int creep_spawn(CreepRegistry *reg, LivingMapEventLog *log, HexCoord home, int faction_owner, int max_hp) {
    return creep_spawn_internal(reg, log, home, faction_owner, max_hp, 0);
}

int creep_spawn_cow(CreepRegistry *reg, LivingMapEventLog *log, HexCoord home) {
    return creep_spawn_internal(reg, log, home, LIVING_MAP_COW_FACTION_OWNER, LIVING_MAP_COW_HP, 1);
}

static int creep_is_hostile_target(const LivingMapCreep *self, const LivingMapCreep *other) {
    return other->active && other->alive && other->faction_owner != self->faction_owner;
}

/* Nearest hostile creep within real aggro range, or -1. Ties (equal distance) resolve to the
 * lowest creep id -- deterministic, same "same inputs, same decision" bar every mod in this
 * package already holds itself to. */
static int creep_find_aggro_target(CreepRegistry *reg, int self_id) {
    LivingMapCreep *self = &reg->creeps[self_id];
    int best = -1;
    int best_dist = LIVING_MAP_CREEP_AGGRO_RANGE + 1;
    for (int i = 0; i < reg->creep_count; i++) {
        if (i == self_id) continue;
        LivingMapCreep *other = &reg->creeps[i];
        if (!creep_is_hostile_target(self, other)) continue;
        int d = hex_distance(self->pos, other->pos);
        if (d <= LIVING_MAP_CREEP_AGGRO_RANGE && d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

static void creep_give_up(LivingMapCreep *c) {
    c->state = LIVING_MAP_CREEP_RETURNING;
    c->chase_target = -1;
}

/* creep_tick_wander -- passive creeps only (cows). Deliberately NOT a random walk: wander_step_count
 * just increments every real step and picks direction (wander_step_count % 6), a plain,
 * deterministic function of "how many times has this creep wandered" -- same "same inputs, same
 * decision" bar every deterministic mod/system in this package already holds itself to, and it
 * avoids needing any PRNG/seed plumbing for a purely cosmetic wander. If the next step would
 * exceed LIVING_MAP_COW_WANDER_RADIUS from home, it steps toward home instead that time (a real,
 * simple bounce, not a hard wall) -- home itself still counts toward wander_step_count either way,
 * so a creep pinned right at its own radius doesn't get stuck retrying the same rejected step
 * forever. */
static void creep_tick_wander(LivingMapCreep *c, LivingMapEventLog *log, int creep_id, unsigned int dt_ms) {
    c->move_timer_ms -= (int)dt_ms;
    if (c->move_timer_ms > 0) return;
    c->move_timer_ms = LIVING_MAP_COW_WANDER_INTERVAL_MS;

    c->wander_step_count++;
    int dir = c->wander_step_count % 6;
    HexCoord candidate = hex_neighbor(c->pos, dir);
    HexCoord next = (hex_distance(c->home, candidate) <= LIVING_MAP_COW_WANDER_RADIUS)
        ? candidate
        : hex_step_toward(c->pos, c->home);

    if (hex_coord_equal(next, c->pos)) return; /* already home and about to step "toward home" again -- a real no-op, no event */

    c->pos = next;
    living_map_emit(log, LIVING_MAP_EVENT_CREEP_WANDERED, creep_id, c->pos.q, c->pos.r);
}

static void creep_tick_idle(CreepRegistry *reg, LivingMapEventLog *log, int creep_id, unsigned int dt_ms) {
    LivingMapCreep *c = &reg->creeps[creep_id];

    if (c->passive) {
        creep_tick_wander(c, log, creep_id, dt_ms);
        return;
    }

    int target = creep_find_aggro_target(reg, creep_id);
    if (target == -1) return;

    c->state = LIVING_MAP_CREEP_CHASING;
    c->chase_target = target;
    living_map_emit(log, LIVING_MAP_EVENT_CREEP_AGGRO, creep_id, target, 0);
}

static void creep_tick_chasing(CreepRegistry *reg, LivingMapEventLog *log, int creep_id, unsigned int dt_ms) {
    LivingMapCreep *c = &reg->creeps[creep_id];
    LivingMapCreep *target = &reg->creeps[c->chase_target];

    if (!target->active || !target->alive) {
        creep_give_up(c);
        return;
    }
    if (hex_distance(c->home, c->pos) > LIVING_MAP_CREEP_LEASH_RANGE) {
        creep_give_up(c);
        return;
    }

    int dist_to_target = hex_distance(c->pos, target->pos);
    if (dist_to_target <= LIVING_MAP_CREEP_ATTACK_RANGE) {
        c->attack_cooldown_ms -= (int)dt_ms;
        if (c->attack_cooldown_ms > 0) return;

        target->hp -= LIVING_MAP_CREEP_ATTACK_DAMAGE;
        c->attack_cooldown_ms = LIVING_MAP_CREEP_ATTACK_COOLDOWN_MS;
        living_map_emit(log, LIVING_MAP_EVENT_CREEP_ATTACK, creep_id, c->chase_target, LIVING_MAP_CREEP_ATTACK_DAMAGE);

        if (target->hp <= 0) {
            target->alive = 0;
            living_map_emit(log, LIVING_MAP_EVENT_CREEP_KILLED, c->chase_target, creep_id, 0);
            creep_give_up(c);
        }
        return;
    }

    /* Not in range yet -- close the gap, respecting the real per-creep move pace. */
    c->move_timer_ms -= (int)dt_ms;
    if (c->move_timer_ms > 0) return;
    c->pos = hex_step_toward(c->pos, target->pos);
    c->move_timer_ms = LIVING_MAP_CREEP_MOVE_INTERVAL_MS;

    if (hex_distance(c->home, c->pos) > LIVING_MAP_CREEP_LEASH_RANGE) {
        creep_give_up(c);
    }
}

static void creep_tick_returning(CreepRegistry *reg, LivingMapEventLog *log, int creep_id, unsigned int dt_ms) {
    LivingMapCreep *c = &reg->creeps[creep_id];

    if (hex_coord_equal(c->pos, c->home)) {
        c->hp = c->max_hp;
        c->state = LIVING_MAP_CREEP_IDLE;
        living_map_emit(log, LIVING_MAP_EVENT_CREEP_RESET, creep_id, 0, 0);
        return;
    }

    c->move_timer_ms -= (int)dt_ms;
    if (c->move_timer_ms > 0) return;
    c->pos = hex_step_toward(c->pos, c->home);
    c->move_timer_ms = LIVING_MAP_CREEP_MOVE_INTERVAL_MS;

    if (hex_coord_equal(c->pos, c->home)) {
        c->hp = c->max_hp;
        c->state = LIVING_MAP_CREEP_IDLE;
        living_map_emit(log, LIVING_MAP_EVENT_CREEP_RESET, creep_id, 0, 0);
    }
}

void creep_tick(CreepRegistry *reg, LivingMapEventLog *log, int creep_id, unsigned int dt_ms) {
    if (creep_id < 0 || creep_id >= reg->creep_count) return;
    LivingMapCreep *c = &reg->creeps[creep_id];
    if (!c->active || !c->alive) return;

    switch (c->state) {
        case LIVING_MAP_CREEP_IDLE:
            creep_tick_idle(reg, log, creep_id, dt_ms);
            break;
        case LIVING_MAP_CREEP_CHASING:
            creep_tick_chasing(reg, log, creep_id, dt_ms);
            break;
        case LIVING_MAP_CREEP_RETURNING:
            creep_tick_returning(reg, log, creep_id, dt_ms);
            break;
    }
}

void creep_tick_all(CreepRegistry *reg, LivingMapEventLog *log, unsigned int dt_ms) {
    int count = reg->creep_count;
    for (int i = 0; i < count; i++) {
        creep_tick(reg, log, i, dt_ms);
    }
}
