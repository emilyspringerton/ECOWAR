/* tests/test_shadow_step.c -- headless smoke test for Bacon+Puck's Shadow Step (W)
 * (packages/simulation/arena_game.c, S202-40, 2026-08-26). Same "no SDL/GL dependency"
 * reasoning as test_arena_game.c's own header comment, same file-per-feature precedent
 * test_abraham_fireball.c already set.
 *
 * Founder real-time: "make bacon buck w instead of a toggle have it turn into sghadow step use
 * the targeting system you had for abraham fireball before we changed it" -> "but have it click
 * on a hero to teleport roughly behind them" -> "add a sense of hero direction i guess so we can
 * actually teleport behind them" -> "hive it a generous range but not crazy like give it the
 * same range as the ranged auto attacks".
 *
 * Real, direct calls through arena_toggle_w's own ARENA_HERO_BACON_PUCK case -- no PARENA mod
 * involved here (this ability is plain host C, same as Blink Dagger's own arena_use_blink,
 * unlike Abraham/Duck/Tree/etc's own mod-driven abilities). */
#include <stdio.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_shadow_step_lands_behind_the_targets_own_facing(void) {
    arena_init_with_heroes(ARENA_HERO_BACON_PUCK, ARENA_HERO_UNICORN);
    arena_bot_enabled = 0;
    ArenaHero *bp = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = bp->x + 5.0f; foe->z = bp->z;
    foe->facing_rad = 0.0f; /* facing "north" (+z), matching atan2f(dx, dz)'s own convention */
    bp->mp = 999;

    arena_set_hover_target(0, 1);
    arena_toggle_w(0);

    CHECK(fabsf(bp->x - foe->x) < 0.01f, "lands at the target's own x (facing straight +z, no x offset)");
    CHECK(fabsf(bp->z - (foe->z + ARENA_BACON_PUCK_W_BEHIND_OFFSET)) < 0.01f,
          "lands past the target along their own real facing_rad, not the caster's approach angle");
    CHECK(bp->w_cooldown_ms == ARENA_BACON_PUCK_W_COOLDOWN_MS, "cooldown is spent on a successful blink");
    CHECK(bp->mp == 999 - ARENA_MP_COST_W, "mana is spent on a successful blink");
}

static void test_shadow_step_respects_the_targets_own_facing_direction(void) {
    arena_init_with_heroes(ARENA_HERO_BACON_PUCK, ARENA_HERO_UNICORN);
    arena_bot_enabled = 0;
    ArenaHero *bp = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = bp->x + 5.0f; foe->z = bp->z;
    foe->facing_rad = 3.14159265f; /* facing "south" (-z) -- "behind" should now be -z, not +z. A plain literal, not M_PI -- undeclared under strict -std=c99 without a feature-test macro this test build doesn't set. */
    bp->mp = 999;

    arena_set_hover_target(0, 1);
    arena_toggle_w(0);

    CHECK(bp->z < foe->z, "a target facing the opposite direction sends the blink the opposite way -- real facing, not a fixed offset");
}

static void test_shadow_step_no_hover_target_is_a_real_noop(void) {
    arena_init_with_heroes(ARENA_HERO_BACON_PUCK, ARENA_HERO_UNICORN);
    ArenaHero *bp = &arena_state.heroes[0];
    float x_before = bp->x, z_before = bp->z;
    bp->mp = 999;
    /* no arena_set_hover_target call -- hover_target[0] stays -1 */

    arena_toggle_w(0);

    CHECK(bp->x == x_before && bp->z == z_before, "no hovered hero at all is a real no-op, caster doesn't move");
    CHECK(bp->w_cooldown_ms == 0, "no cooldown spent on the no-op case");
    CHECK(bp->mp == 999, "no mana spent on the no-op case -- \"dont blow the cooldown and do nothing\" applies here too");
}

static void test_shadow_step_out_of_range_is_a_real_noop(void) {
    arena_init_with_heroes(ARENA_HERO_BACON_PUCK, ARENA_HERO_UNICORN);
    ArenaHero *bp = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = bp->x + ARENA_BACON_PUCK_W_RANGE + 10.0f; foe->z = bp->z;
    float x_before = bp->x;
    bp->mp = 999;

    arena_set_hover_target(0, 1);
    arena_toggle_w(0);

    CHECK(bp->x == x_before, "a target past ARENA_BACON_PUCK_W_RANGE (\"same range as the ranged auto attacks\") is a real no-op");
    CHECK(bp->w_cooldown_ms == 0, "no cooldown spent whiffing out of range");
}

static void test_shadow_step_cannot_target_an_ally(void) {
    arena_init_with_heroes(ARENA_HERO_BACON_PUCK, ARENA_HERO_UNICORN);
    ArenaHero *bp = &arena_state.heroes[0];
    ArenaHero *ally = &arena_state.heroes[1];
    ally->team = bp->team; /* force onto the same team */
    ally->x = bp->x + 3.0f; ally->z = bp->z;
    float x_before = bp->x;
    bp->mp = 999;

    arena_set_hover_target(0, 1);
    arena_toggle_w(0);

    CHECK(bp->x == x_before, "hovering an ally instead of an enemy is a real no-op, not a friendly blink");
}

static void test_shadow_step_gated_by_cooldown_and_mana(void) {
    arena_init_with_heroes(ARENA_HERO_BACON_PUCK, ARENA_HERO_UNICORN);
    ArenaHero *bp = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = bp->x + 3.0f; foe->z = bp->z;
    bp->mp = 999;
    bp->w_cooldown_ms = 5000;
    float x_before = bp->x;

    arena_set_hover_target(0, 1);
    arena_toggle_w(0);
    CHECK(bp->x == x_before, "a blink blocked by cooldown never moves the caster");

    bp->w_cooldown_ms = 0;
    bp->mp = 0;
    arena_toggle_w(0);
    CHECK(bp->x == x_before, "a blink blocked by insufficient mana never moves the caster");
}

/* Ask Again Later (S202-40 redesign, see bacon_puck_cast_q's own doc comment): Q's own
   intangible duration no longer varies with the old W toggle (which no longer exists) --
   always the base duration now, a real, accepted, honest simplification. */
static void test_bacon_puck_q_always_uses_the_base_intangible_duration(void) {
    arena_init_with_heroes(ARENA_HERO_BACON_PUCK, ARENA_HERO_UNICORN);
    ArenaHero *bp = &arena_state.heroes[0];
    bp->mp = 999;

    arena_cast_q(0);

    CHECK(bp->intangible_ms == ARENA_BACON_PUCK_Q_INTANGIBLE_MS,
          "Q always uses the base intangible duration now -- the old 'watching' bonus is dead along with the W toggle it depended on");
}

int main(void) {
    test_shadow_step_lands_behind_the_targets_own_facing();
    test_shadow_step_respects_the_targets_own_facing_direction();
    test_shadow_step_no_hover_target_is_a_real_noop();
    test_shadow_step_out_of_range_is_a_real_noop();
    test_shadow_step_cannot_target_an_ally();
    test_shadow_step_gated_by_cooldown_and_mana();
    test_bacon_puck_q_always_uses_the_base_intangible_duration();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
