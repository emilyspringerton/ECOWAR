/* tests/test_combat_log.c -- headless smoke test for the real combat log
 * (packages/simulation/arena_game.c's ArenaCombatLogEntry ring buffer),
 * the host half of PARENA/stdlib/redgarden/combat_log_mod.prn --
 * docs/ARENA_API.md's own "NOT YET WIRED" gap, closed this pass.
 *
 * Same "no SDL/GL dependency" reasoning as test_arena_game.c's own header
 * comment, and same "drive the real log indirectly through an exported
 * entry point, not a direct static-function call" shape test_damage_log.c
 * already established -- arena_log_combat_event/redgarden_host_log_* are
 * all internal to arena_game.c; this file is a separate translation unit.
 *
 * Each test drives its event through the real, live call site named in
 * ArenaCombatLogEntry's own doc comment (arena_game.h), not a synthetic
 * direct log push -- same "real, live end-to-end" bar test_ecowar_cards.c
 * already holds itself to. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* most_recent_combat_log: same head-1-wrapped idiom test_damage_log.c's own
 * most_recent lookup already uses for damage_log. */
static const ArenaCombatLogEntry *most_recent_combat_log(void) {
    int idx = (arena_state.combat_log_head - 1 + ARENA_COMBAT_LOG_CAPACITY) % ARENA_COMBAT_LOG_CAPACITY;
    return &arena_state.combat_log[idx];
}

static void test_log_starts_empty(void) {
    arena_init();
    CHECK(arena_state.combat_log_count == 0, "combat log starts empty after arena_init");
    CHECK(arena_state.combat_log_head == 0, "combat log head starts at 0 after arena_init");
}

/* test_hero_kill_logs_with_real_attribution: same 1v1-duel-to-the-death
 * shape test_arena_game.c's own test_combat_and_win_condition already
 * uses to reach a real kill -- heroes[0]/[1] hardcoded team=0/1 (S170-46),
 * one held at 1hp so the very next attributed hit ends the match. */
static void test_hero_kill_logs_with_real_attribution(void) {
    arena_init();
    arena_bot_enabled = 0; /* same isolation reasoning test_damage_log.c's own duel test uses */
    /* Off-center, well clear of the Blacksmith node (0,0) and its own neutral guardian creep --
       test_damage_log.c's own (0,0)-based duel never noticed this (its heroes survive at hp=100,
       so an occasional unattributed creep hit never becomes the "most recent" entry it checks),
       but this test's hp=1 target dies to the FIRST hit landed by anyone -- including the
       guardian, whose kills are real but correctly unattributed (no hero on the other end),
       exactly the honest limitation ArenaCombatLogEntry's own doc comment already accepts. Moving
       the duel away from every node/fountain (see arena_fountain_position's own corner formula)
       keeps this test's hp=1 target's first hit the real hero-vs-hero one it means to check. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = -45.0f;
    arena_state.heroes[1].x = 0.5f; arena_state.heroes[1].z = -45.0f;
    arena_state.heroes[1].hp = 1; /* one more attributed melee hit finishes it */
    arena_set_move_target(0, 0.0f, -45.0f);

    int logged_before = arena_state.combat_log_count;
    for (int i = 0; i < 60 && arena_state.heroes[1].alive; i++) {
        arena_update(16);
    }

    CHECK(!arena_state.heroes[1].alive, "setup: the target hero actually died within the test's own tick budget");
    CHECK(arena_state.combat_log_count > logged_before, "a real attributed kill pushes a new combat log entry");
    const ArenaCombatLogEntry *e = most_recent_combat_log();
    CHECK(e->type == ARENA_LOG_EVENT_HERO_KILL, "the newly logged entry is a real HERO_KILL event");
    CHECK(e->a == (int)arena_state.heroes[1].hero_id, "logged victim (a) is the hero that actually died");
    CHECK(e->b == (int)arena_state.heroes[0].hero_id, "logged killer (b) is the hero that actually landed the hit");
}

static void test_item_purchase_logs_real_buyer_item_and_cost(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->active = 1;
    h->alive = 1;
    h->flow = 999999; /* affordability never blocks this test */
    float shop_x, shop_z;
    arena_shop_position(h->team, &shop_x, &shop_z);
    h->x = shop_x; h->z = shop_z; /* real proximity check must actually pass */

    int item_id = 0;
    int expected_cost = ARENA_ITEMS[item_id].cost;
    int ok = arena_shop_buy(0, item_id);

    CHECK(ok, "setup: the real purchase actually went through");
    const ArenaCombatLogEntry *e = most_recent_combat_log();
    CHECK(e->type == ARENA_LOG_EVENT_ITEM_PURCHASE, "a real purchase logs an ITEM_PURCHASE event");
    CHECK(e->a == 0, "logged buyer (a) is the real owner slot that bought it");
    CHECK(e->b == item_id, "logged item_id (b) is the real item bought");
    CHECK(e->c == expected_cost, "logged cost (c) is the item's real catalog cost");
}

/* test_node_capture_then_uncapture_log_real_events: exercises BOTH real
 * call sites in one flow (capture, then a rival channel starting on the
 * same node) -- an uncapture is only ever reachable from an already-
 * captured node, same real sequencing arena_tick_nodes itself enforces. */
static void test_node_capture_then_uncapture_log_real_events(void) {
    arena_init();
    arena_bot_enabled = 0;
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaHero *a = &arena_state.heroes[0]; /* team 0 */
    ArenaHero *b = &arena_state.heroes[1]; /* team 1 */
    ArenaNode *node = &arena_state.nodes[0];
    arena_state.towers[0].alive = 0; /* the tower gate is a real, separate mechanic -- not what this test exercises */
    /* The node's own neutral guardian creep (arena_tick_creeps) spawns the instant its tower is
       gone -- also a real, separate mechanic, but one that would otherwise interfere here: a
       hero standing on the node takes creep damage, and "damage interrupts the capture" is
       arena_tick_nodes' own real, intentional rule (see its header comment) -- found the hard
       way when the uncapture half of this test kept silently landing in the interrupt branch
       instead. Deactivating it isolates the ownership-transfer mechanic this test actually means
       to exercise, same "isolate what you're testing" reasoning arena_bot_enabled=0 above uses. */
    arena_state.creeps[0].alive = 0;

    /* -- Capture: team 0 alone in radius, channel already almost done. -- */
    a->x = node->x; a->z = node->z;
    b->x = node->x + 100.0f; b->z = node->z + 100.0f; /* well outside ARENA_NODE_CAPTURE_RADIUS */
    node->capturing_team = 0;
    node->capture_progress_ms = ARENA_NODE_CAPTURE_CHANNEL_MS - 16; /* one real tick from completion */
    node->owner = 0;

    arena_update(16);

    CHECK(node->owner == 1, "setup: the node actually finished capturing for team 0 (owner encodes 1=team0)");
    const ArenaCombatLogEntry *cap = most_recent_combat_log();
    CHECK(cap->type == ARENA_LOG_EVENT_NODE_CAPTURE, "a completed channel logs a real NODE_CAPTURE event");
    CHECK(cap->a == 0, "logged node_id (a) is the real node that was captured");
    CHECK(cap->b == 0, "logged team (b) is the real capturing team (0)");

    /* -- Uncapture: team 1 alone in radius now, starting a rival channel
       on the node team 0 just captured. -- */
    a->x = node->x + 100.0f; a->z = node->z + 100.0f;
    b->x = node->x; b->z = node->z;
    arena_state.creeps[0].alive = 0; /* a fresh guardian may have spawned/respawned during the capture tick above -- re-neutralize before the uncapture tick for the same reason */

    arena_update(16);

    const ArenaCombatLogEntry *uncap = most_recent_combat_log();
    CHECK(uncap->type == ARENA_LOG_EVENT_NODE_UNCAPTURE, "team 1 starting a rival channel logs a real NODE_UNCAPTURE event");
    CHECK(uncap->a == 0, "logged node_id (a) is the same real node");
    CHECK(uncap->b == 0, "logged team (b) is the team that actually lost it (0), not the team now channeling");
}

static void test_king_spawn_logs_real_camp_id(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0; /* no hero interaction needed for a spawn */
    arena_state.king_spawn_timer_ms[2] = ARENA_KING_SPAWN_DELAY_MS - 16; /* camp 2, one real tick from its first spawn */

    arena_update_teams(16);

    CHECK(arena_state.kings[2].active, "setup: the King actually spawned within this one real tick");
    const ArenaCombatLogEntry *e = most_recent_combat_log();
    CHECK(e->type == ARENA_LOG_EVENT_KING_SPAWN, "a real King spawn logs a KING_SPAWN event");
    CHECK(e->a == 2, "logged camp_id (a) is the real camp that just spawned its King");
}

int main(void) {
    test_log_starts_empty();
    test_hero_kill_logs_with_real_attribution();
    test_item_purchase_logs_real_buyer_item_and_cost();
    test_node_capture_then_uncapture_log_real_events();
    test_king_spawn_logs_real_camp_id();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
