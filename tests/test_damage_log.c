/* tests/test_damage_log.c -- headless smoke test for the real combat
 * damage log (packages/simulation/arena_game.c, S189-01, "go ahead and
 * add the damage log to REDGARDEN"). Same "no SDL/GL dependency" reasoning
 * as test_arena_game.c's own header comment.
 *
 * Deliberately a separate file, same precedent test_bloodflower.c already
 * set the same day for a similarly self-contained new subsystem.
 *
 * apply_damage/apply_damage_ex/arena_log_damage/resolve_combat are all
 * `static` in arena_game.c (internal linkage) -- this test file is a
 * separate translation unit and cannot call them directly, so every check
 * here drives the real log indirectly through arena_update(), the same
 * exported entry point test_arena_game.c's own test_combat_and_win_
 * condition already uses to exercise resolve_combat.
 *
 * HONEST COVERAGE LIMITATION: this only exercises the direct hero-vs-hero
 * duel path (resolve_combat via apply_damage_ex), which is the one path
 * upgraded to real attacker attribution -- see ArenaDamageLogEntry's own
 * doc comment in arena_game.h. The unattributed default path (apply_damage's
 * ~48 other real call sites -- ability/creep/tower/King damage) is not
 * independently exercised here; its correctness rests on direct code
 * reading (apply_damage passes ARENA_HERO_COUNT as source_hero_id, verified
 * inline), not a dedicated runtime check, since none of those call sites has
 * as simple an exported-function setup path as the 1v1 duel does. Flagged
 * as a real gap, not silently left uncovered. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_log_starts_empty(void) {
    arena_init();
    CHECK(arena_state.damage_log_count == 0, "damage log starts empty after arena_init");
    CHECK(arena_state.damage_log_head == 0, "damage log head starts at 0 after arena_init");
}

static void test_direct_duel_logs_with_real_attribution(void) {
    arena_init();
    /* Disable the bot AI's own ability casts (tick_hero_kit runs AFTER resolve_combat inside
       arena_update, same call) -- without this, an ability hit lands in the same tick and its
       own unattributed log entry becomes the "most recent" one this test checks, not the
       melee duel hit it actually means to verify. Same isolation
       test_movement_reaches_target already uses for the same reason (different symptom). */
    arena_bot_enabled = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 0.5f; arena_state.heroes[1].z = 0;
    arena_set_move_target(0, 0.0f, 0.0f); /* player holds position, same as test_combat_and_win_condition */

    /* Just enough ticks for the attack cooldown to fire at least once, well short of a kill. */
    for (int i = 0; i < 60 && arena_state.winner == 0; i++) {
        arena_update(16);
    }

    CHECK(arena_state.damage_log_count > 0, "at least one real damage event logged from direct combat");
    if (arena_state.damage_log_count > 0) {
        int most_recent = (arena_state.damage_log_head - 1 + ARENA_DAMAGE_LOG_CAPACITY) % ARENA_DAMAGE_LOG_CAPACITY;
        const ArenaDamageLogEntry *e = &arena_state.damage_log[most_recent];
        CHECK(e->amount > 0, "logged entry has a real positive damage amount");
        CHECK(e->source_hero_id < ARENA_HERO_COUNT,
              "direct duel damage is real-attributed (source is a real hero, not the unattributed sentinel)");
        CHECK(e->source_hero_id == arena_state.heroes[0].hero_id || e->source_hero_id == arena_state.heroes[1].hero_id,
              "attributed source is genuinely one of the two dueling heroes, not garbage");
        CHECK(e->target_hero_id == arena_state.heroes[0].hero_id || e->target_hero_id == arena_state.heroes[1].hero_id,
              "target is genuinely one of the two dueling heroes");
        CHECK(e->source_hero_id != e->target_hero_id, "a hero is never logged as damaging itself");
    }
}

static void test_ring_buffer_caps_and_wraps(void) {
    arena_init();
    arena_bot_enabled = 0; /* same isolation reasoning as test_direct_duel_logs_with_real_attribution */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 0.5f; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].hp = 999999; arena_state.heroes[1].max_hp = 999999; /* survive long enough to log many hits */
    arena_state.heroes[0].hp = 999999; arena_state.heroes[0].max_hp = 999999;
    arena_set_move_target(0, 0.0f, 0.0f);

    /* Enough ticks for well over ARENA_DAMAGE_LOG_CAPACITY real attack-cooldown cycles from
       both heroes combined (ARENA_ATTACK_COOLDOWN_MS = 700ms, 16ms/tick -- a few thousand
       ticks is comfortably enough for 2x the log capacity of real hits). */
    for (int i = 0; i < 3000 && arena_state.winner == 0; i++) {
        arena_update(16);
    }

    CHECK(arena_state.damage_log_count == ARENA_DAMAGE_LOG_CAPACITY,
          "log count caps at ARENA_DAMAGE_LOG_CAPACITY once more real hits land than the buffer holds");
    CHECK(arena_state.damage_log_head >= 0 && arena_state.damage_log_head < ARENA_DAMAGE_LOG_CAPACITY,
          "head index stays in-bounds after wrapping past capacity");
}

int main(void) {
    test_log_starts_empty();
    test_direct_duel_logs_with_real_attribution();
    test_ring_buffer_caps_and_wraps();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
