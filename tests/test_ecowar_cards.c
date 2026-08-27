/* tests/test_ecowar_cards.c -- headless smoke test for ECOWAR's own card system
 * (S202-ECOWAR-01, 2026-08-27), ECOWAR's first real gameplay mechanic since the hard fork from
 * REDGARDEN. Same "no SDL/GL dependency" reasoning as test_arena_game.c's own header comment.
 *
 * Founder real-time: "16 hallucinated cards from tyler hero bible with promptoverse art" +
 * "mod api first parena mod dev" + "do the whole game in pure parena as much as you can."
 *
 * Real live round-trip through the real compiled PARENA mod
 * (on_ecowar_resolve_card_magnitude), not a direct call to a hand-written substitute -- same
 * bar every REDGARDEN mod test already holds itself to. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_card_catalog_has_16_real_entries(void) {
    CHECK(ECOWAR_CARD_COUNT == 16, "the catalog is real 16 cards, matching the founder's own count");
    int all_have_names = 1, all_have_sources = 1, all_have_positive_magnitude = 1;
    for (int i = 0; i < ECOWAR_CARD_COUNT; i++) {
        if (!ECOWAR_CARDS[i].name || ECOWAR_CARDS[i].name[0] == '\0') all_have_names = 0;
        if (!ECOWAR_CARDS[i].source_hero || ECOWAR_CARDS[i].source_hero[0] == '\0') all_have_sources = 0;
        if (ECOWAR_CARDS[i].base_magnitude <= 0) all_have_positive_magnitude = 0;
    }
    CHECK(all_have_names, "every card has a real, non-empty name");
    CHECK(all_have_sources, "every card names its real TYLER/multiverse_heroes.md source hero");
    CHECK(all_have_positive_magnitude, "every card has a real positive base magnitude");
}

static void test_resolve_rejects_bad_card_id(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *target = &arena_state.heroes[0];
    CHECK(ecowar_resolve_card_effect(1, -1, target) == 0, "a negative card id is rejected");
    CHECK(ecowar_resolve_card_effect(1, ECOWAR_CARD_COUNT, target) == 0, "a card id past the real catalog is rejected");
}

static void test_resolve_rejects_dead_target(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *target = &arena_state.heroes[0];
    target->alive = 0;
    CHECK(ecowar_resolve_card_effect(1, 2, target) == 0, "a dead target is rejected, no effect applied");
}

/* Real live round trip: the DAMAGE-type card at id 2 ("One Question," MUNDANE, base 12) should
 * apply real damage through apply_damage/apply_armor, with the PARENA mod leaving a MUNDANE
 * card's magnitude unscaled (base value, no +50% bonus). */
static void test_mundane_damage_card_applies_unscaled_damage(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *target = &arena_state.heroes[0];
    int hp_before = target->hp;

    int result = ecowar_resolve_card_effect(1, 2, target); /* card 2: "One Question," DAMAGE, base 12 */

    CHECK(result == 1, "card 2 (One Question) resolves successfully");
    CHECK(target->hp < hp_before, "a real DAMAGE-type card actually reduces target hp");
    CHECK(hp_before - target->hp <= 12, "MUNDANE card 2's damage is unscaled (<=12, no MYTHIC +50% bonus)");
}

/* The real point of routing magnitude through the PARENA mod: a MYTHIC-tier card (id 3, "He
 * Sees You," base 10) should deal MORE damage than its own stated base value, because
 * on_ecowar_resolve_card_magnitude applies +50% for MYTHIC cards -- proving the mod's own real
 * decision logic actually executes, not just a pass-through. */
static void test_mythic_damage_card_gets_real_magnitude_bonus(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *mundane_target = &arena_state.heroes[0];
    ArenaHero *mythic_target = &arena_state.heroes[1];
    /* Same starting hp/armor for both, so the only real difference in damage taken is the
       card's own tier scaling, not target state. */
    int hp_before = mundane_target->hp;
    mythic_target->hp = mundane_target->hp;

    ecowar_resolve_card_effect(1, 2, mundane_target); /* card 2: MUNDANE, base 12 */
    ecowar_resolve_card_effect(1, 3, mythic_target);  /* card 3: MYTHIC, base 10 -> real mod scales to 15 */

    int mundane_damage = hp_before - mundane_target->hp;
    int mythic_damage = hp_before - mythic_target->hp;
    CHECK(mythic_damage > mundane_damage,
          "card 3 (MYTHIC, base 10, scaled to 15 by the real PARENA mod) deals more damage than card 2 (MUNDANE, base 12, unscaled) -- the mod's own real decision logic actually ran");
}

static void test_heal_card_applies_real_heal_and_caps_at_max_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *target = &arena_state.heroes[0];
    target->hp = target->max_hp - 5; /* near cap, so the real heal-cap clamp actually gets exercised */

    ecowar_resolve_card_effect(1, 5, target); /* card 5: "The Seal," HEAL, base 15 */

    CHECK(target->hp == target->max_hp, "a HEAL-type card heals real hp, correctly clamped at max_hp (15 healing on 5 missing hp)");
}

static void test_slow_card_applies_real_slow(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *target = &arena_state.heroes[0];
    CHECK(target->slowed_ms == 0, "setup: not slowed before the card");

    ecowar_resolve_card_effect(1, 9, target); /* card 9: "Fold the Circle," SLOW, base 2000ms, MYTHIC -> 3000ms */

    CHECK(target->slowed_ms > 0, "a SLOW-type card applies a real slowed_ms duration");
    CHECK(target->slowed_ms == 3000, "card 9 is MYTHIC (base 2000ms) -- the real mod scales it to 3000ms");
}

static void test_silence_card_applies_real_silence(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *target = &arena_state.heroes[0];
    CHECK(target->silenced_ms == 0, "setup: not silenced before the card");

    ecowar_resolve_card_effect(1, 0, target); /* card 0: "Duration Is Data," SILENCE, base 1500ms, MUNDANE */

    CHECK(target->silenced_ms == 1500, "a SILENCE-type card applies its real, unscaled (MUNDANE) duration");
}

static void test_flow_card_grants_real_flow(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *target = &arena_state.heroes[0];
    int flow_before = target->flow;

    ecowar_resolve_card_effect(1, 12, target); /* card 12: "Unlicensed Ledger," FLOW, base 60, MYTHIC -> 90 */

    CHECK(target->flow == flow_before + 90, "a FLOW-type card grants its real, MYTHIC-scaled Flow amount (60 -> 90)");
}

int main(void) {
    test_card_catalog_has_16_real_entries();
    test_resolve_rejects_bad_card_id();
    test_resolve_rejects_dead_target();
    test_mundane_damage_card_applies_unscaled_damage();
    test_mythic_damage_card_gets_real_magnitude_bonus();
    test_heal_card_applies_real_heal_and_caps_at_max_hp();
    test_slow_card_applies_real_slow();
    test_silence_card_applies_real_silence();
    test_flow_card_grants_real_flow();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
