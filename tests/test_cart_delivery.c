/* tests/test_cart_delivery.c -- headless smoke test for arena_fibonacci/arena_marble_bag_pick
 * (S202-09/S202-42, NORTHSTAR's own long-documented "weighted marble-bag + Fibonacci-pity pull
 * algorithm," first real implementation anywhere in this repo) and the Cart's own delivery
 * mechanic that consumes it. Same "no SDL/GL dependency" reasoning as test_arena_game.c's own
 * header comment.
 *
 * Founder real-time: "REDGARDEN hero Cart -- AOE circle indicators, more impactful/powered-up
 * abilities, and a general random-buff system (e.g. a random hero occasionally gets a King
 * buff), driven by a weighted marble bag + Fibonacci-pity RNG." The random-buff ask is scoped
 * here to Cart's own existing delivery mechanic ("whoever steps into the zone first" already IS
 * the "occasionally, to a random hero" trigger) rather than a new, separate, undecided global
 * timer system -- see arena_game.h's own ARENA_CART_DELIVERY_* doc comment.
 *
 * cart_trigger_delivery/cart_apply_delivery_outcome are static to arena_game.c -- exercised
 * indirectly through the real entry points (arena_toggle_w/arena_cast_r + arena_update_teams),
 * same "test the real mechanic, not just hope RNG cooperates" discipline test_duck_smoke_bomb.c
 * and test_tree_passive.c already established for this file. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_fibonacci_sequence(void) {
    CHECK(arena_fibonacci(0) == 1, "fib(0) = 1 (not the textbook 0 -- a fresh pity counter still has real weight)");
    CHECK(arena_fibonacci(1) == 1, "fib(1) = 1");
    CHECK(arena_fibonacci(2) == 2, "fib(2) = 2");
    CHECK(arena_fibonacci(3) == 3, "fib(3) = 3");
    CHECK(arena_fibonacci(4) == 5, "fib(4) = 5");
    CHECK(arena_fibonacci(5) == 8, "fib(5) = 8");
    CHECK(arena_fibonacci(10) == 89, "fib(10) = 89 -- real growth, not capped internally (the cap lives in the caller, ARENA_MARBLE_BAG_MAX_PITY_TIER)");
}

static void test_marble_bag_rejects_bad_input(void) {
    int weights[3] = { 1, 1, 1 };
    int pity[3] = { 0, 0, 0 };
    CHECK(arena_marble_bag_pick(weights, pity, 0) == -1, "n<=0 returns -1, not garbage");
    CHECK(arena_marble_bag_pick(weights, pity, -1) == -1, "negative n returns -1");

    int zero_weights[3] = { 0, 0, 0 };
    CHECK(arena_marble_bag_pick(zero_weights, pity, 3) == -1, "every weight 0 returns -1, not a fake pick");
}

static void test_marble_bag_single_outcome_always_wins(void) {
    int weights[3] = { 0, 5, 0 };
    int pity[3] = { 0, 0, 0 };
    int all_correct = 1;
    for (int i = 0; i < 20; i++) {
        if (arena_marble_bag_pick(weights, pity, 3) != 1) all_correct = 0;
    }
    CHECK(all_correct, "the only nonzero-weight outcome always wins, every trial");
}

static void test_marble_bag_updates_pity_correctly(void) {
    int weights[2] = { 1, 1 };
    int pity[2] = { 0, 0 };
    int picked = arena_marble_bag_pick(weights, pity, 2);
    CHECK(pity[picked] == 0, "the winner's own pity resets to 0");
    CHECK(pity[1 - picked] == 1, "every other outcome's pity increments by 1");
}

/* The real, load-bearing behavior a "Fibonacci pity" system exists for: an outcome that keeps
 * losing must become genuinely more likely over time, not stay flatly improbable forever. */
static void test_marble_bag_pity_increases_selection_odds(void) {
    int weights[2] = { 1, 1 };
    int picks_of_1 = 0;
    const int trials = 2000;
    for (int i = 0; i < trials; i++) {
        int local_pity[2] = { 0, 9 }; /* fresh each trial -- measuring the odds AT this pity state, not letting resets bleed across trials */
        if (arena_marble_bag_pick(weights, local_pity, 2) == 1) picks_of_1++;
    }
    /* fib(9)=55 vs fib(0)=1 -- outcome 1 should win the overwhelming majority of trials, not
       just marginally more than 50%. A generous >80% bound avoids RNG-driven flakiness while
       still proving the pity mechanism is real, not a no-op. */
    CHECK(picks_of_1 > trials * 0.8, "heavy pity (9 misses) makes that outcome win the large majority of the time");
}

static void test_marble_bag_pity_tier_is_capped(void) {
    int weights[2] = { 1, 1 };
    int wins_at_cap = 0, wins_past_cap = 0;
    const int trials = 3000;
    for (int i = 0; i < trials; i++) {
        int a[2] = { 0, ARENA_MARBLE_BAG_MAX_PITY_TIER };
        int b[2] = { 0, ARENA_MARBLE_BAG_MAX_PITY_TIER + 50 };
        if (arena_marble_bag_pick(weights, a, 2) == 1) wins_at_cap++;
        if (arena_marble_bag_pick(weights, b, 2) == 1) wins_past_cap++;
    }
    float rate_at_cap = (float)wins_at_cap / trials;
    float rate_past_cap = (float)wins_past_cap / trials;
    CHECK(fabsf(rate_at_cap - rate_past_cap) < 0.08f,
          "pity tiers out at ARENA_MARBLE_BAG_MAX_PITY_TIER -- going further past it doesn't keep increasing the odds");
}

/* Delivery outcome classification, for the real end-to-end tests below: exactly one of these
 * fields changes per real trigger, so which one changed tells us which outcome landed. */
typedef enum { OUT_NONE, OUT_HEAL, OUT_MANA, OUT_SLOW, OUT_FLOW, OUT_KING_BUFF } DeliveryOutcomeSeen;

static DeliveryOutcomeSeen classify_and_reset(ArenaHero *target) {
    /* Thresholds well above anything passive HP/MP regen could produce over one real 50ms tick
       (a real outcome moves hp/mp by ARENA_CART_DELIVERY_HEAL_PCT/MANA_PCT = 35% of max) --
       without this, ordinary passive regen falsely classifies as a delivery outcome on nearly
       every single trial, an early real bug this test itself caught while being written. */
    DeliveryOutcomeSeen seen = OUT_NONE;
    if (target->hp > target->max_hp / 2 + target->max_hp / 10) seen = OUT_HEAL;
    else if (target->mp > target->max_mp / 2 + target->max_mp / 10) seen = OUT_MANA;
    else if (target->slowed_ms > 0) seen = OUT_SLOW;
    else if (target->flow > 0) seen = OUT_FLOW;
    else if (target->king_growth_stacks > 0) seen = OUT_KING_BUFF;
    /* Reset every field a delivery outcome could have touched, back to the same fixed
       mid-range baseline every trial starts from. */
    target->hp = target->max_hp / 2;
    target->mp = target->max_mp / 2;
    target->slowed_ms = 0;
    target->flow = 0;
    target->king_growth_stacks = 0;
    target->king_growth_ms = 0;
    return seen;
}

/* Fires Cart's delivery `trials` times via the real slot (`slot` 1=W, 2=R) and tallies which
 * outcome landed each time -- a real, live round-trip through arena_toggle_w/arena_cast_r ->
 * tick_hero_kit -> cart_trigger_delivery -> arena_marble_bag_pick -> cart_apply_delivery_outcome,
 * not a direct call into any of those static functions. */
static void run_cart_deliveries(int slot, int trials, int *out_counts /* size 5, indexed by DeliveryOutcomeSeen-1 */) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    ArenaHero *cart = &arena_state.heroes[0];
    cart->hero_id = ARENA_HERO_CART;
    cart->x = 0.0f; cart->z = 0.0f;
    /* The real zone-trigger loop (tick_hero_kit's own CART case) scans hero slots starting at
       index 0 and fires on the FIRST hittable hero found inside the zone -- "checks EVERY
       hero... including the Cart itself" is real, not just flavor text: with only the Cart
       itself active and standing on its own r_zone_x/z, the Cart always finds ITSELF first
       (distance 0). Rather than fight that with a second hero racing for the same slot 0, this
       test leans into it and observes the Cart's own stats directly -- still the real, live
       end-to-end mechanism (arena_toggle_w/arena_cast_r -> tick_hero_kit -> cart_trigger_
       delivery -> arena_marble_bag_pick -> cart_apply_delivery_outcome), just self-targeted. */
    cart->hp = cart->max_hp / 2;
    cart->mp = cart->max_mp / 2;

    int cast_cost = (slot == 1) ? ARENA_MP_COST_W : ARENA_MP_COST_R;
    memset(out_counts, 0, 5 * sizeof(int));
    for (int t = 0; t < trials; t++) {
        /* mp set to exactly cast_cost + 50 so it lands on the SAME real baseline (50, out of
           max_mp=100) after the cast spends its own cost, regardless of whether this trial is
           testing W (cost 20) or R (cost 45) -- a flat 999 here would swamp the classification
           threshold below and make every trial misclassify as a mana outcome whether one
           actually fired or not, a real bug this test caught while being written. */
        cart->mp = cast_cost + 50;
        cart->w_cooldown_ms = 0;
        cart->r_cooldown_ms = 0;
        /* Reset pity to 0 every trial (fib(0)=1 for every outcome) -- isolates the RAW base-
           weight ratio between W's and R's tables, which is what this function's own callers
           actually want to measure. Left accumulating across trials, pity's own self-balancing
           nature (a losing outcome gets more likely over time) would gradually wash out the
           very weight difference test_cart_r_delivery_weighted_better_than_w exists to prove --
           real, understood pity behavior, just not what a "does R weight King-buff higher than
           W" measurement wants. */
        memset(cart->cart_delivery_pity, 0, sizeof(cart->cart_delivery_pity));
        if (slot == 1) arena_toggle_w(0); else arena_cast_r(0);
        arena_update_teams(50); /* real tick -- the zone check + cart_trigger_delivery both live in tick_hero_kit, called from here */
        DeliveryOutcomeSeen seen = classify_and_reset(cart);
        if (seen != OUT_NONE) out_counts[seen - 1]++;
    }
}

static void test_cart_w_delivery_covers_every_real_outcome(void) {
    int counts[5];
    run_cart_deliveries(1, 500, counts);
    int total = counts[0] + counts[1] + counts[2] + counts[3] + counts[4];
    CHECK(total > 450, "the real W-slot delivery lands a classifiable outcome almost every trigger");
    CHECK(counts[OUT_HEAL - 1] > 0, "W delivery: heal outcome occurs across real trials");
    CHECK(counts[OUT_MANA - 1] > 0, "W delivery: mana outcome occurs across real trials");
    CHECK(counts[OUT_SLOW - 1] > 0, "W delivery: slow outcome occurs across real trials");
    CHECK(counts[OUT_FLOW - 1] > 0, "W delivery: flow outcome occurs across real trials");
    CHECK(counts[OUT_KING_BUFF - 1] > 0, "W delivery: King's Growth buff outcome occurs across real trials -- the founder's own literal example, live end-to-end");
}

/* The real point of giving R its own weight table (ARENA_CART_Q_HEAL's own doc comment: "R has
 * bigger radius/BETTER-WEIGHTED outcomes," a stated intent this rework is the first to actually
 * build): the King-buff outcome should land noticeably more often through R than through W. */
static void test_cart_r_delivery_weighted_better_than_w(void) {
    int w_counts[5], r_counts[5];
    const int trials = 1500;
    run_cart_deliveries(1, trials, w_counts);
    run_cart_deliveries(2, trials, r_counts);
    float w_king_rate = (float)w_counts[OUT_KING_BUFF - 1] / trials;
    float r_king_rate = (float)r_counts[OUT_KING_BUFF - 1] / trials;
    CHECK(r_king_rate > w_king_rate * 2.0f,
          "R's King-buff outcome lands meaningfully more often than W's (real, distinct weighting, not the same rand()%4 both slots used to share)");
}

int main(void) {
    test_fibonacci_sequence();
    test_marble_bag_rejects_bad_input();
    test_marble_bag_single_outcome_always_wins();
    test_marble_bag_updates_pity_correctly();
    test_marble_bag_pity_increases_selection_odds();
    test_marble_bag_pity_tier_is_capped();
    test_cart_w_delivery_covers_every_real_outcome();
    test_cart_r_delivery_weighted_better_than_w();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
