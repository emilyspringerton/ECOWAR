/* tests/test_item_curriculum.c -- headless smoke test for the item curriculum generation
 * primitive (packages/simulation/arena_game.c, 2026-08-25). Same "no SDL/GL dependency"
 * reasoning as test_arena_game.c's own header comment, same file-per-subsystem precedent
 * test_bloodflower.c/test_tree_passive.c/test_build_templates.c already set.
 *
 * Founder real-time: "continue the exotic auto curriculum redgarden work" -> "training" ->
 * "parena mod driven first". NORTHSTAR.md §26.3.2's own honesty note applies here too: this
 * tests the GENERATION primitive (blend two catalog items into a runtime curriculum slot),
 * not an evaluation loop deciding whether a generated item actually counters a dominant
 * team composition -- that half isn't built yet, see arena_game.h's "Item curriculum"
 * section doc comment.
 *
 * Same "live round-trip tested, not just compile-checked" bar the other three mod tests set:
 * this calls on_generate_counter_item (item_curriculum_mod_host.h) directly, the real
 * PARENA-compiled entry point, not redgarden_host_item_curriculum_generate_counter_item
 * itself -- so a regression in the generated glue code (item_curriculum_mod.c) would fail
 * this test even if the host function's own logic were untouched. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"
#include "../packages/simulation/item_curriculum_mod_host.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_blend_via_real_parena_mod_lands_in_the_named_slot(void) {
    int id = on_generate_counter_item(5 /* Wanecall Grimoire */, 8 /* Splinterfang */, 1);
    CHECK(id == ARENA_ITEM_COUNT + 1, "generated item id is ARENA_ITEM_COUNT + slot_index, through the real compiled mod");

    const ArenaItemDef *got = redgarden_host_item_curriculum_get(1);
    CHECK(got != NULL, "the generated slot is readable back");
    CHECK(strncmp(got->name, "Curriculum:", 11) == 0, "generated item is named as a curriculum blend, not a placeholder");
    CHECK(got->tier == ARENA_ITEM_TIER_WEIRD, "generated items are tagged WEIRD -- an unusual, generated stat shape");
    CHECK(got->slot == ARENA_ITEMS[5].slot, "generated item equips into base_item_a's own slot");
}

static void test_blended_stats_stay_in_a_plausible_range_of_the_two_bases(void) {
    /* Bruiser weapon (Ironbark Plate, id 5 in the catalog block... use two real, distinct
       weapon-slot items with well-separated stat values so a wildly-out-of-range blend would
       be obvious, not masked by two nearly-identical inputs. */
    int a_id = 8;  /* Splinterfang: weapon, 900, +30ad */
    int b_id = 4;  /* Wanecall Grimoire: weapon, 950, +25ad +60mp */
    on_generate_counter_item(a_id, b_id, 0);
    const ArenaItemDef *got = redgarden_host_item_curriculum_get(0);
    const ArenaItemDef *a = &ARENA_ITEMS[a_id];
    const ArenaItemDef *b = &ARENA_ITEMS[b_id];

    int ad_lo = (a->bonus_ad < b->bonus_ad ? a->bonus_ad : b->bonus_ad) * 80 / 100;
    int ad_hi = (a->bonus_ad > b->bonus_ad ? a->bonus_ad : b->bonus_ad) * 120 / 100;
    CHECK(got->bonus_ad >= ad_lo && got->bonus_ad <= ad_hi,
          "blended bonus_ad stays within +/-20% of the two base items' own range, not an outlier");

    int cost_lo = (a->cost < b->cost ? a->cost : b->cost) * 80 / 100;
    int cost_hi = (a->cost > b->cost ? a->cost : b->cost) * 120 / 100;
    CHECK(got->cost >= cost_lo && got->cost <= cost_hi,
          "blended cost stays within +/-20% of the two base items' own range");
}

static void test_generation_is_deterministic(void) {
    on_generate_counter_item(3, 7, 2);
    ArenaItemDef first = *redgarden_host_item_curriculum_get(2);

    on_generate_counter_item(3, 7, 2);
    const ArenaItemDef *second = redgarden_host_item_curriculum_get(2);

    CHECK(first.bonus_ad == second->bonus_ad && first.cost == second->cost &&
          first.bonus_max_hp == second->bonus_max_hp,
          "the same pair of base items always generates the same blend -- reproducible, not rand()-driven");
}

static void test_distinct_slots_do_not_collide(void) {
    on_generate_counter_item(1, 2, 0);
    on_generate_counter_item(9, 10, 3);
    const ArenaItemDef *s0 = redgarden_host_item_curriculum_get(0);
    const ArenaItemDef *s3 = redgarden_host_item_curriculum_get(3);
    CHECK(strcmp(s0->name, s3->name) != 0, "two different curriculum slots hold two independently-generated items");
}

static void test_invalid_inputs_are_rejected(void) {
    CHECK(on_generate_counter_item(-1, 2, 0) == -1, "a negative base item index is rejected");
    CHECK(on_generate_counter_item(2, ARENA_ITEM_COUNT, 0) == -1, "a base item index past the real catalog is rejected");
    CHECK(on_generate_counter_item(2, 3, ARENA_ITEM_CURRICULUM_SLOT_COUNT) == -1, "an out-of-range slot index is rejected");
    CHECK(on_generate_counter_item(2, 3, -1) == -1, "a negative slot index is rejected");
}

static void test_get_out_of_range_slot_returns_null(void) {
    CHECK(redgarden_host_item_curriculum_get(ARENA_ITEM_CURRICULUM_SLOT_COUNT) == NULL,
          "reading an out-of-range curriculum slot returns NULL, not garbage");
    CHECK(redgarden_host_item_curriculum_get(-1) == NULL, "reading a negative slot index returns NULL");
}

int main(void) {
    test_blend_via_real_parena_mod_lands_in_the_named_slot();
    test_blended_stats_stay_in_a_plausible_range_of_the_two_bases();
    test_generation_is_deterministic();
    test_distinct_slots_do_not_collide();
    test_invalid_inputs_are_rejected();
    test_get_out_of_range_slot_returns_null();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
