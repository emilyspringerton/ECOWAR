/* tests/test_tree_passive.c -- headless smoke test for the ARENA_HERO_TREE passive
 * (packages/simulation/arena_game.c, 2026-08-25). Same "no SDL/GL dependency" reasoning as
 * test_arena_game.c's own header comment, same file-per-subsystem precedent test_bloodflower.c
 * already set.
 *
 * Founder real-time: "can you add a passive to tree that when he is close enough to a tree to
 * auto attack it he auto attacks it and slowly regenerates health?" -> "the tree he attacks never
 * does or anything have it jiggle animate extra squishy" -> "as a parena first mod led dev
 * cycle."
 *
 * Same "live round-trip tested, not just compile-checked" bar test_bloodflower.c already set for
 * a PARENA mod: this actually calls arena_hero_tree_passive and asserts the strike lands through
 * the real compiled mod (on_tree_passive_strike -> redgarden_host_tree_passive_strike), not a
 * direct call to the host function. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* Finds the index of the first ARENA_OBSTACLE_TREE in the reset layout -- real positions, not
   assumed, so this test stays correct if arena_obstacles_reset_layout's own table ever changes. */
static int first_tree_obstacle_index(void) {
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
        if (arena_state.obstacles[i].kind == ARENA_OBSTACLE_TREE) return i;
    }
    return -1;
}

static void test_reset_layout_gives_trees_real_hp_rocks_stay_zero(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    arena_obstacles_reset_layout();

    int saw_tree = 0, saw_rock = 0;
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
        ArenaObstacle *o = &arena_state.obstacles[i];
        if (o->kind == ARENA_OBSTACLE_TREE) {
            saw_tree = 1;
            CHECK(o->hp == ARENA_TREE_HP && o->max_hp == ARENA_TREE_HP, "a tree obstacle starts at full ARENA_TREE_HP");
        } else {
            saw_rock = 1;
            CHECK(o->hp == 0 && o->max_hp == 0, "a rock obstacle's hp/max_hp stay 0/unused");
        }
    }
    CHECK(saw_tree && saw_rock, "setup: layout has both trees and rocks to actually exercise the branch");
}

static void test_tree_passive_strikes_nearest_tree_via_real_parena_mod(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    arena_obstacles_reset_layout();
    int ti = first_tree_obstacle_index();
    CHECK(ti >= 0, "setup: a tree obstacle exists");

    ArenaHero *h = &arena_state.heroes[0];
    h->active = 1;
    h->alive = 1;
    h->hero_id = ARENA_HERO_TREE;
    h->team = 0;
    h->x = arena_state.obstacles[ti].x;
    h->z = arena_state.obstacles[ti].z; /* standing right on top of it -- well within ARENA_ATTACK_RANGE */
    h->hp = 10;
    h->max_hp = 1000;

    int hp_before = arena_state.obstacles[ti].hp;
    arena_hero_tree_passive(16);

    CHECK(arena_state.obstacles[ti].hp == hp_before - ARENA_TREE_PASSIVE_DAMAGE,
          "strike lands through the real compiled PARENA mod and applies ARENA_TREE_PASSIVE_DAMAGE");
    CHECK(h->hp == 10 + ARENA_TREE_PASSIVE_HEAL_PER_HIT, "hero self-heals ARENA_TREE_PASSIVE_HEAL_PER_HIT on a successful strike");
    CHECK(h->attack_cooldown_ms > 0, "strike sets the hero's shared attack_cooldown_ms");
}

static void test_tree_passive_gated_to_tree_hero_only(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    arena_obstacles_reset_layout();
    int ti = first_tree_obstacle_index();

    ArenaHero *h = &arena_state.heroes[0];
    h->active = 1;
    h->alive = 1;
    h->hero_id = ARENA_HERO_FLAMEL; /* any non-Tree hero */
    h->team = 0;
    h->x = arena_state.obstacles[ti].x;
    h->z = arena_state.obstacles[ti].z;
    h->hp = 10;
    h->max_hp = 1000;

    int hp_before = arena_state.obstacles[ti].hp;
    arena_hero_tree_passive(16);

    CHECK(arena_state.obstacles[ti].hp == hp_before, "a non-Tree hero standing on a tree never triggers the passive");
    CHECK(h->hp == 10, "and gains no heal");
}

static void test_tree_passive_yields_to_a_closer_enemy_hero(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    arena_obstacles_reset_layout();
    int ti = first_tree_obstacle_index();

    ArenaHero *h = &arena_state.heroes[0];
    h->active = 1;
    h->alive = 1;
    h->hero_id = ARENA_HERO_TREE;
    h->team = 0;
    h->x = arena_state.obstacles[ti].x;
    h->z = arena_state.obstacles[ti].z;
    h->hp = 500;
    h->max_hp = 1000;

    ArenaHero *foe = &arena_state.heroes[1];
    foe->active = 1;
    foe->alive = 1;
    foe->hero_id = ARENA_HERO_FLAMEL;
    foe->team = 1; /* enemy */
    foe->x = h->x;
    foe->z = h->z; /* same spot -- well within ARENA_ATTACK_RANGE */
    foe->hp = 500;
    foe->max_hp = 500;

    int hp_before = arena_state.obstacles[ti].hp;
    arena_hero_tree_passive(16);

    CHECK(arena_state.obstacles[ti].hp == hp_before, "an enemy hero in range takes precedence over the tree passive, same as camp minions");
}

static void test_tree_regen_ticks_toward_max_and_caps(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    arena_obstacles_reset_layout();
    int ti = first_tree_obstacle_index();
    arena_state.obstacles[ti].hp = ARENA_TREE_HP - 50;

    arena_tick_obstacles(1000); /* 1 real second */
    CHECK(arena_state.obstacles[ti].hp == ARENA_TREE_HP - 50 + ARENA_TREE_REGEN_PER_SEC,
          "arena_tick_obstacles regenerates a damaged tree at ARENA_TREE_REGEN_PER_SEC");

    arena_state.obstacles[ti].hp = ARENA_TREE_HP - 1;
    arena_tick_obstacles(1000);
    CHECK(arena_state.obstacles[ti].hp == ARENA_TREE_HP, "regen clamps at max_hp, never overshoots");

    arena_tick_obstacles(1000);
    CHECK(arena_state.obstacles[ti].hp == ARENA_TREE_HP, "a fully-regenerated tree stays at max_hp, no further change");
}

static void test_tree_never_destroyed_stays_a_real_obstacle(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    arena_obstacles_reset_layout();
    int ti = first_tree_obstacle_index();
    arena_state.obstacles[ti].hp = 1;

    ArenaHero *h = &arena_state.heroes[0];
    h->active = 1;
    h->alive = 1;
    h->hero_id = ARENA_HERO_TREE;
    h->team = 0;
    h->x = arena_state.obstacles[ti].x;
    h->z = arena_state.obstacles[ti].z;
    h->hp = 10;
    h->max_hp = 1000;

    arena_hero_tree_passive(16); /* would overkill a 1-hp tree if it could die */

    CHECK(arena_state.obstacles[ti].hp == 0, "hp clamps at 0, never negative");
    CHECK(arena_state.obstacles[ti].kind == ARENA_OBSTACLE_TREE, "the obstacle itself is never despawned/changed -- a permanent resource, not a kill target");
}

/* Real, live production bug found and fixed 2026-08-25 (founder, real-time: "im playing
   redgarden on latest and the tree is not generating health from auto attacking the other
   trees ensure the server knows about that and its all wired up to work"): every test above
   calls arena_hero_tree_passive/arena_tick_obstacles DIRECTLY, never through the real top-level
   arena_update()/arena_update_teams() tick functions -- so none of them would have caught the
   actual bug, which was that arena_update() (the 1v1 tick, apps/arena_server/src/main.c's own
   `if (lobby_size == 2) arena_update(...) else arena_update_teams(...)` branch) never called
   arena_tick_obstacles/arena_hero_tree_passive at all, only arena_update_teams did. The
   founder's own 1v1 matchmaker (:7779, lobby-size 2) runs exclusively through arena_update, so
   the passive silently never fired there. This test exercises the real top-level function a
   live 1v1 match actually calls, closing that gap. */
static void test_tree_passive_fires_through_the_real_1v1_arena_update(void) {
    arena_init_with_heroes(ARENA_HERO_TREE, ARENA_HERO_DUCK);
    int ti = first_tree_obstacle_index();
    ArenaHero *h = &arena_state.heroes[0];
    h->x = arena_state.obstacles[ti].x;
    h->z = arena_state.obstacles[ti].z; /* well within ARENA_ATTACK_RANGE, far from hero 1 (Duck at x=6) */
    h->hp = 50;
    h->max_hp = 100;
    int obstacle_hp_before = arena_state.obstacles[ti].hp;
    int hero_hp_before = h->hp;

    arena_update(16); /* the REAL 1v1 tick function -- not a direct call to the mechanic itself */

    CHECK(arena_state.obstacles[ti].hp == obstacle_hp_before - ARENA_TREE_PASSIVE_DAMAGE,
          "arena_update() (the 1v1 tick) now damages the nearest tree obstacle, same as arena_update_teams() already did");
    CHECK(h->hp == hero_hp_before + ARENA_TREE_PASSIVE_HEAL_PER_HIT,
          "arena_update() (the 1v1 tick) now heals the Tree hero from the strike, same as arena_update_teams() already did");
}

int main(void) {
    test_reset_layout_gives_trees_real_hp_rocks_stay_zero();
    test_tree_passive_strikes_nearest_tree_via_real_parena_mod();
    test_tree_passive_gated_to_tree_hero_only();
    test_tree_passive_yields_to_a_closer_enemy_hero();
    test_tree_regen_ticks_toward_max_and_caps();
    test_tree_never_destroyed_stays_a_real_obstacle();
    test_tree_passive_fires_through_the_real_1v1_arena_update();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
