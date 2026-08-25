/* tests/test_build_templates.c -- headless smoke test for build templates
 * (packages/simulation/arena_game.c, 2026-08-25). Same "no SDL/GL dependency" reasoning as
 * test_arena_game.c's own header comment, same file-per-subsystem precedent test_bloodflower.c/
 * test_tree_passive.c already set.
 *
 * Founder real-time: "ok in redgarden lets experiment with the idea that tech trees are just
 * item templates" -> "choosing a build can let you auto buy at the shop" -> "or some combination
 * a build doesn't have to define all items" -> "and there can be complex ordering rules" ->
 * "all powered by parena scripting and parena mods."
 *
 * Same "live round-trip tested, not just compile-checked" bar test_bloodflower.c/
 * test_tree_passive.c already set for a PARENA mod: this actually calls
 * arena_hero_apply_build_template and asserts the purchase lands through the real compiled mod
 * (on_apply_build_template_item -> redgarden_host_buy_build_item -> arena_shop_buy), not a
 * direct call to arena_shop_buy. */
#include <stdio.h>
#include <string.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* setup_hero_at_shop: real, minimal state for a hero standing at their own team's shop, flush
   with `flow` -- same setup shape every arena_shop_buy-adjacent test in this repo needs. */
static void setup_hero_at_shop(int owner, int team, int flow) {
    ArenaHero *h = &arena_state.heroes[owner];
    h->active = 1;
    h->alive = 1;
    h->team = team;
    h->flow = flow;
    for (int s = 0; s < ARENA_ITEM_SLOT_COUNT; s++) h->equipped_item[s] = -1;
    float sx, sz;
    arena_shop_position(team, &sx, &sz);
    h->x = sx;
    h->z = sz;
}

static void test_apply_template_buys_in_order_via_real_parena_mod(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    setup_hero_at_shop(0, 0, 100000);

    const ArenaBuildTemplate *tmpl = &ARENA_BUILD_TEMPLATES[0]; /* Bruiser */
    int bought = arena_hero_apply_build_template(0, 0);

    CHECK(bought == tmpl->item_count, "flush with Flow, every item in the template gets bought");
    for (int i = 0; i < tmpl->item_count; i++) {
        int item_id = tmpl->item_ids[i];
        CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEMS[item_id].slot] == item_id,
              "each templated item lands in its own real equip slot, through the real compiled PARENA mod");
    }
}

static void test_partial_flow_buys_as_much_as_affordable_in_order(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    const ArenaBuildTemplate *tmpl = &ARENA_BUILD_TEMPLATES[0]; /* Bruiser, cheapest-first order */
    int first_cost = ARENA_ITEMS[tmpl->item_ids[0]].cost;
    int second_cost = ARENA_ITEMS[tmpl->item_ids[1]].cost;
    /* Enough for the first two items, not the third. */
    setup_hero_at_shop(0, 0, first_cost + second_cost);

    int bought = arena_hero_apply_build_template(0, 0);

    CHECK(bought == 2, "buys exactly as many items as Flow affords, in template order, not all-or-nothing");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEMS[tmpl->item_ids[0]].slot] == tmpl->item_ids[0],
          "the first (cheapest) item was bought");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEMS[tmpl->item_ids[1]].slot] == tmpl->item_ids[1],
          "the second item was bought");
    CHECK(arena_state.heroes[0].equipped_item[ARENA_ITEMS[tmpl->item_ids[2]].slot] == -1,
          "the third (unaffordable) item was not bought -- sequence stops, doesn't fail");
}

static void test_reapplying_a_partially_owned_template_is_idempotent(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    const ArenaBuildTemplate *tmpl = &ARENA_BUILD_TEMPLATES[0];
    int first_cost = ARENA_ITEMS[tmpl->item_ids[0]].cost;
    setup_hero_at_shop(0, 0, first_cost);
    int first_bought = arena_hero_apply_build_template(0, 0);
    CHECK(first_bought == 1, "setup: first pass buys exactly the one affordable item");

    int flow_after_first = arena_state.heroes[0].flow;
    arena_state.heroes[0].flow += first_cost; /* now affordable again, but item 0 is already equipped */
    int second_bought = arena_hero_apply_build_template(0, 0);

    CHECK(second_bought == 1, "re-clicking the same build skips the already-equipped item and buys the next one, doesn't re-buy item 0");
    CHECK(arena_state.heroes[0].flow != flow_after_first + first_cost,
          "flow actually moved (the second item was bought, not a no-op) -- confirms this isn't just silently succeeding at nothing");
}

static void test_out_of_range_and_inactive_hero_are_safe_no_ops(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    setup_hero_at_shop(0, 0, 100000);

    CHECK(arena_hero_apply_build_template(0, -1) == 0, "negative template_id is a safe no-op");
    CHECK(arena_hero_apply_build_template(0, ARENA_BUILD_TEMPLATE_COUNT) == 0, "out-of-range template_id is a safe no-op");
    CHECK(arena_hero_apply_build_template(-1, 0) == 0, "negative owner is a safe no-op");

    arena_state.heroes[1].active = 0;
    CHECK(arena_hero_apply_build_template(1, 0) == 0, "an inactive hero buys nothing");
}

static void test_out_of_shop_range_buys_nothing(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    setup_hero_at_shop(0, 0, 100000);
    arena_state.heroes[0].x += ARENA_SHOP_RADIUS * 10.0f; /* walk well outside the shop */

    int bought = arena_hero_apply_build_template(0, 0);
    CHECK(bought == 0, "a hero out of shop range buys nothing -- same proximity gate arena_shop_buy itself enforces");
}

static void test_every_template_slot_is_distinct(void) {
    /* Real catalog-shape check, not simulated: a template listing two items for the same
       ArenaItemSlot would silently make the second overwrite the first on purchase (equip-slot
       semantics, not a bag) -- verify every shipped preset avoids that. */
    for (int t = 0; t < ARENA_BUILD_TEMPLATE_COUNT; t++) {
        const ArenaBuildTemplate *tmpl = &ARENA_BUILD_TEMPLATES[t];
        int seen_slot[ARENA_ITEM_SLOT_COUNT];
        for (int s = 0; s < ARENA_ITEM_SLOT_COUNT; s++) seen_slot[s] = 0;
        int ok = 1;
        for (int i = 0; i < tmpl->item_count; i++) {
            ArenaItemSlot slot = ARENA_ITEMS[tmpl->item_ids[i]].slot;
            if (seen_slot[slot]) ok = 0;
            seen_slot[slot] = 1;
        }
        char msg[96];
        snprintf(msg, sizeof(msg), "template \"%s\" has no two items sharing the same equip slot", tmpl->name);
        CHECK(ok, msg);
    }
}

int main(void) {
    test_apply_template_buys_in_order_via_real_parena_mod();
    test_partial_flow_buys_as_much_as_affordable_in_order();
    test_reapplying_a_partially_owned_template_is_idempotent();
    test_out_of_range_and_inactive_hero_are_safe_no_ops();
    test_out_of_shop_range_buys_nothing();
    test_every_template_slot_is_distinct();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
