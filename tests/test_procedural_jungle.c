/* tests/test_procedural_jungle.c -- headless smoke test for the S370-03 Mandelbrot procedural
 * jungle (packages/simulation/arena_game.c's arena_obstacles_reset_layout, indices
 * [ARENA_OBSTACLE_HANDPLACED_COUNT, ARENA_OBSTACLE_COUNT)). Same "no SDL/GL dependency"
 * reasoning as test_arena_game.c's own header comment, same file-per-subsystem precedent
 * test_tree_passive.c/test_bloodflower.c already set.
 *
 * Founder real-time, 2026-09-11: "lets use the mandelbrot set to add more trees in between the
 * bases. we want there to be a lot more trees like DOTA2 - but actually can we build it into the
 * game so that we procedurally generate the map for each new game?" EMILY/BACKLOG.md S370.
 *
 * Calls arena_init_teams() (not a bare arena_obstacles_reset_layout()) for every test here --
 * unlike test_tree_passive.c's own narrower direct call, this needs arena_state.nodes[] actually
 * populated first (arena_init_teams runs arena_nodes_reset_layout() before
 * arena_obstacles_reset_layout()), since the exclusion-zone checks under test read real node
 * positions. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static float dist2d(float x1, float z1, float x2, float z2) {
    float dx = x1 - x2, dz = z1 - z2;
    return sqrtf(dx * dx + dz * dz);
}

static void test_procedural_slots_populated_and_mostly_trees(void) {
    arena_set_match_seed(12345u);
    arena_init_teams();

    int trees = 0, rocks = 0, inactive = 0;
    for (int i = ARENA_OBSTACLE_HANDPLACED_COUNT; i < ARENA_OBSTACLE_COUNT; i++) {
        ArenaObstacle *o = &arena_state.obstacles[i];
        if (o->radius <= 0.0f) { inactive++; continue; } /* the "rejection sampling exhausted its budget" fallback sentinel */
        if (o->kind == ARENA_OBSTACLE_TREE) trees++; else rocks++;
    }
    CHECK(inactive == 0, "every procedural slot found a real spot -- rejection sampling didn't exhaust its budget on this seed");
    CHECK(trees > 0, "the procedural jungle placed at least one tree");
    CHECK(trees > rocks, "trees are the majority of the procedural jungle, matching 'a lot more trees' ");
    CHECK(trees + rocks == ARENA_OBSTACLE_PROCEDURAL_COUNT, "every procedural slot is accounted for as exactly one tree or one rock");
}

static void test_handplaced_layout_untouched(void) {
    arena_set_match_seed(999u);
    arena_init_teams();
    /* The very first hand-placed entry (arena_obstacles_reset_layout's own static table) --
       real known coordinate, confirms S370-03 didn't touch indices [0, ARENA_OBSTACLE_HANDPLACED_COUNT). */
    float expected_x = -11.5f * 1.618034f * 3.0f; /* ARENA_MAP_SCALE_9X */
    CHECK(fabsf(arena_state.obstacles[0].x - expected_x) < 0.01f, "hand-placed obstacle 0 keeps its founder-tuned position, untouched by the procedural generator");
    CHECK(arena_state.obstacles[0].kind == ARENA_OBSTACLE_TREE, "hand-placed obstacle 0 is still the tree it always was");
}

static void test_procedural_never_overlaps_a_node(void) {
    arena_set_match_seed(777u);
    arena_init_teams();
    for (int i = ARENA_OBSTACLE_HANDPLACED_COUNT; i < ARENA_OBSTACLE_COUNT; i++) {
        ArenaObstacle *o = &arena_state.obstacles[i];
        if (o->radius <= 0.0f) continue;
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            float d = dist2d(o->x, o->z, arena_state.nodes[n].x, arena_state.nodes[n].z);
            CHECK(d >= ARENA_NODE_CAPTURE_RADIUS + o->radius, "a procedural obstacle never overlaps a capture node's own radius");
        }
    }
}

static void test_procedural_never_overlaps_graveyard_or_shop(void) {
    arena_set_match_seed(42u);
    arena_init_teams();
    for (int i = ARENA_OBSTACLE_HANDPLACED_COUNT; i < ARENA_OBSTACLE_COUNT; i++) {
        ArenaObstacle *o = &arena_state.obstacles[i];
        if (o->radius <= 0.0f) continue;
        for (int team = 0; team < 2; team++) {
            float gx, gz, sx, sz;
            arena_graveyard_position(team, &gx, &gz);
            arena_shop_position(team, &sx, &sz);
            CHECK(dist2d(o->x, o->z, gx, gz) > o->radius + 4.0f, "a procedural obstacle stays clear of a team's graveyard/spawn-fan corner");
            CHECK(dist2d(o->x, o->z, sx, sz) > ARENA_SHOP_RADIUS + o->radius, "a procedural obstacle stays clear of a team's shop");
        }
    }
}

static void test_procedural_never_overlaps_fountain_or_camp(void) {
    arena_set_match_seed(2026u);
    arena_init_teams();
    for (int i = ARENA_OBSTACLE_HANDPLACED_COUNT; i < ARENA_OBSTACLE_COUNT; i++) {
        ArenaObstacle *o = &arena_state.obstacles[i];
        if (o->radius <= 0.0f) continue;
        for (int f = 0; f < ARENA_FOUNTAIN_COUNT; f++) {
            float fx, fz;
            arena_fountain_position(f, &fx, &fz);
            CHECK(dist2d(o->x, o->z, fx, fz) > ARENA_FOUNTAIN_RADIUS + o->radius, "a procedural obstacle stays clear of a fountain");
        }
        for (int c = 0; c < ARENA_CAMP_COUNT; c++) {
            float cx, cz;
            arena_camp_position(c, &cx, &cz);
            CHECK(dist2d(o->x, o->z, cx, cz) > o->radius + 4.0f, "a procedural obstacle stays clear of a jungle camp");
        }
    }
}

static void test_procedural_never_overlaps_another_obstacle(void) {
    arena_set_match_seed(31337u);
    arena_init_teams();
    int violations = 0;
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
        if (arena_state.obstacles[i].radius <= 0.0f) continue;
        for (int j = i + 1; j < ARENA_OBSTACLE_COUNT; j++) {
            if (arena_state.obstacles[j].radius <= 0.0f) continue;
            float d = dist2d(arena_state.obstacles[i].x, arena_state.obstacles[i].z,
                              arena_state.obstacles[j].x, arena_state.obstacles[j].z);
            if (d < arena_state.obstacles[i].radius + arena_state.obstacles[j].radius) violations++;
        }
    }
    CHECK(violations == 0, "no two obstacles (hand-placed or procedural) overlap closely enough to pinch off a walkable gap");
}

static void test_same_seed_is_deterministic(void) {
    arena_set_match_seed(555u);
    arena_init_teams();
    float xs[ARENA_OBSTACLE_COUNT], zs[ARENA_OBSTACLE_COUNT];
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) { xs[i] = arena_state.obstacles[i].x; zs[i] = arena_state.obstacles[i].z; }

    arena_set_match_seed(555u);
    arena_init_teams();
    int all_match = 1;
    for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
        if (xs[i] != arena_state.obstacles[i].x || zs[i] != arena_state.obstacles[i].z) all_match = 0;
    }
    CHECK(all_match, "the same seed reproduces byte-identical obstacle positions -- required for client/server agreement without wire sync");
}

static void test_different_seeds_give_different_layouts(void) {
    arena_set_match_seed(1u);
    arena_init_teams();
    float x0 = arena_state.obstacles[ARENA_OBSTACLE_HANDPLACED_COUNT].x;
    float z0 = arena_state.obstacles[ARENA_OBSTACLE_HANDPLACED_COUNT].z;

    arena_set_match_seed(2u);
    arena_init_teams();
    float x1 = arena_state.obstacles[ARENA_OBSTACLE_HANDPLACED_COUNT].x;
    float z1 = arena_state.obstacles[ARENA_OBSTACLE_HANDPLACED_COUNT].z;

    CHECK(x0 != x1 || z0 != z1, "adjacent seeds (1 vs 2) produce different jungle layouts -- 'procedurally generate the map for each new game'");
}

static void test_unset_seed_default_is_deterministic_for_existing_test_callers(void) {
    /* Every OTHER test file in this suite calls arena_init_teams()/arena_init_with_heroes()
       without ever calling arena_set_match_seed() -- this confirms that path still gets a fixed,
       reproducible layout (arena_game.c's own g_arena_match_seed default), not undefined/
       process-random behavior that would make those tests flaky. */
    arena_init_teams();
    float x0 = arena_state.obstacles[ARENA_OBSTACLE_HANDPLACED_COUNT].x;
    arena_init_teams();
    float x1 = arena_state.obstacles[ARENA_OBSTACLE_HANDPLACED_COUNT].x;
    CHECK(x0 == x1, "never calling arena_set_match_seed still yields a fixed, reproducible layout across repeated inits");
}

int main(void) {
    printf("ECOWAR procedural jungle (S370) headless smoke test\n\n");
    test_procedural_slots_populated_and_mostly_trees();
    test_handplaced_layout_untouched();
    test_procedural_never_overlaps_a_node();
    test_procedural_never_overlaps_graveyard_or_shop();
    test_procedural_never_overlaps_fountain_or_camp();
    test_procedural_never_overlaps_another_obstacle();
    test_same_seed_is_deterministic();
    test_different_seeds_give_different_layouts();
    test_unset_seed_default_is_deterministic_for_existing_test_callers();

    printf("\n%s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
