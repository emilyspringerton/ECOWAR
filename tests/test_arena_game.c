/* tests/test_arena_game.c — headless smoke test for packages/simulation/
 * arena_game.c. No SDL/GL dependency on purpose: this box has no display
 * (no Xvfb), so the arena client itself can't be run interactively here,
 * but the sim logic underneath it has zero GL dependency and is fully
 * testable without one. Written to catch real bugs before the client is
 * ever visually confirmed working. */
#include <stdio.h>
#include <math.h>

#include "../packages/simulation/arena_game.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static ArenaProjectile *find_active_projectile(void) {
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        if (arena_state.projectiles[i].active) return &arena_state.projectiles[i];
    }
    return NULL;
}

static void test_movement_reaches_target(void) {
    arena_init();
    /* Hero0 starts at (-6,0); target is close (~4.2 units away, ~1s at
       ARENA_HERO_SPEED) so it arrives long before the bot (12 units away,
       chasing at the same speed) can close the gap into melee range and
       end the match early. Deliberately does NOT touch the bot's HP/alive
       state -- killing it mid-test would trigger the win condition and
       freeze arena_update() (it returns immediately once winner != 0),
       which is exactly the bug this test caught on the first pass. */
    arena_set_move_target(0, -3.0f, 3.0f);
    int ticks = 0;
    while (arena_state.heroes[0].moving && arena_state.winner == 0 && ticks < 500) {
        arena_update(16);
        ticks++;
    }
    float dx = arena_state.heroes[0].x - (-3.0f);
    float dz = arena_state.heroes[0].z - 3.0f;
    float dist = sqrtf(dx * dx + dz * dz);
    CHECK(dist < 0.1f, "hero reaches its move target");
    CHECK(arena_state.winner == 0, "match still in progress -- combat didn't interrupt a short move");
}

static void test_bounds_clamped(void) {
    arena_init();
    arena_set_move_target(0, 999.0f, -999.0f);
    CHECK(arena_state.heroes[0].target_x <= ARENA_HALF_EXTENT + 0.001f,
          "move target clamped to arena bounds (x)");
    CHECK(arena_state.heroes[0].target_z >= -ARENA_HALF_EXTENT - 0.001f,
          "move target clamped to arena bounds (z)");
}

static void test_combat_and_win_condition(void) {
    arena_init();
    /* Place the heroes already adjacent so combat starts immediately. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 0.5f; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].hp = 8; /* one hit from dying */
    arena_set_move_target(0, 0.0f, 0.0f); /* player holds position */

    int ticks = 0;
    while (arena_state.winner == 0 && ticks < 5000) {
        arena_update(16);
        ticks++;
    }
    CHECK(arena_state.winner != 0, "match reaches a winner instead of running forever");
    CHECK(arena_state.winner == 1, "player wins when bot's HP is set near zero");
    CHECK(!arena_state.heroes[1].alive, "loser is marked not-alive");
}

static void test_bot_steers_toward_player(void) {
    arena_init();
    arena_state.heroes[1].x = 6.0f; arena_state.heroes[1].z = 0.0f;
    arena_state.heroes[0].x = -6.0f; arena_state.heroes[0].z = 0.0f;
    arena_bot_tick(16);
    /* Bot is east of the player -- its steering target should move it west (toward smaller x). */
    CHECK(arena_state.heroes[1].target_x < arena_state.heroes[1].x,
          "bot brain steers toward the player, not away");
}

static void test_click_near_enemy_becomes_attack_move(void) {
    arena_init();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 5.0f; arena_state.heroes[1].z = 0.0f;
    /* Click 1 unit short of the bot -- inside the attack-move re-target
       radius (ARENA_ATTACK_RANGE * 3 = 4.8) but not yet in attack range. */
    arena_set_move_target(0, 4.0f, 0.0f);
    float bot_x_before = arena_state.heroes[1].x, bot_z_before = arena_state.heroes[1].z;
    arena_update(16);
    /* Compare against the bot's pre-tick position: the bot also steers and
       moves within this same update() call, so its post-tick position has
       already drifted a little (~0.06 units at one 16ms tick) from where
       the re-target snapped to. */
    float dx = arena_state.heroes[0].target_x - bot_x_before;
    float dz = arena_state.heroes[0].target_z - bot_z_before;
    CHECK(sqrtf(dx * dx + dz * dz) < 0.01f,
          "clicking near the bot re-targets onto the bot (attack-move), not the exact click point");
}

/* --- The Unicorn's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-18) --- */

static void test_unicorn_q_dashes_and_damages(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    float start_x = h->x;
    /* Move the foe adjacent to where the dash will land, in the direction
       of the hero's current move target, so the hit-radius check succeeds. */
    arena_set_move_target(0, h->x + 4.0f, h->z);
    foe->x = h->x + ARENA_UNICORN_Q_DASH_DIST;
    foe->z = h->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(0);

    CHECK(h->x > start_x, "Q dashes the hero forward");
    CHECK(foe->hp < foe_hp_before, "Q damages the foe when the dash lands in range");
    CHECK(h->q_cooldown_ms == ARENA_UNICORN_Q_COOLDOWN_MS, "Q starts on cooldown after cast");
}

static void test_unicorn_q_respects_cooldown(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    arena_set_move_target(0, h->x + 4.0f, h->z);
    arena_cast_q(0);
    float x_after_first = h->x;
    arena_cast_q(0); /* should no-op, still on cooldown */
    CHECK(h->x == x_after_first, "Q does not re-cast while on cooldown");
}

static void test_unicorn_w_regen_toggle(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->hp = 50; /* below max so regen has room to matter */
    arena_toggle_w(0);
    CHECK(h->w_active == 1, "W toggles on");
    arena_update(1000); /* 1 second of regen */
    CHECK(h->hp > 50, "W regenerates HP while active");
    arena_toggle_w(0);
    CHECK(h->w_active == 0, "W toggles back off");
}

static void test_mp_starts_full(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    CHECK(h->mp == ARENA_MP_MAX, "a fresh hero starts with a full mana pool");
    CHECK(h->max_mp == ARENA_MP_MAX, "max_mp is set on init, not left at zero");
}

static void test_mp_regenerates_over_time(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->mp = 0;

    arena_update(1000); /* 1 second of regen */

    CHECK(h->mp > 0, "mana regenerates every tick with no cast at all");
    CHECK(h->mp <= ARENA_MP_MAX, "regen never overshoots the pool's own max");
}

static void test_mp_deducted_on_landed_q_cast(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    arena_set_move_target(0, h->x + 4.0f, h->z);
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = h->x + ARENA_UNICORN_Q_DASH_DIST;
    foe->z = h->z;
    int mp_before = h->mp;

    arena_cast_q(0);

    CHECK(h->mp == mp_before - ARENA_MP_COST_Q, "a landed Q spends exactly its own flat mana cost");
}

static void test_mp_blocks_cast_when_insufficient(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    arena_set_move_target(0, h->x + 4.0f, h->z);
    ArenaHero *foe = &arena_state.heroes[1];
    foe->x = h->x + ARENA_UNICORN_Q_DASH_DIST;
    foe->z = h->z;
    h->mp = ARENA_MP_COST_Q - 1; /* one short */
    float x_before = h->x;
    int foe_hp_before = foe->hp;

    arena_cast_q(0);

    CHECK(h->x == x_before, "insufficient mana blocks the cast entirely -- no dash");
    CHECK(foe->hp == foe_hp_before, "insufficient mana blocks the cast entirely -- no damage either");
    CHECK(h->q_cooldown_ms == 0, "a cast blocked by mana never starts its cooldown, same as a whiff");
}

static void test_mp_toggle_w_charges_on_activate_free_on_deactivate(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    int mp_before = h->mp;

    arena_toggle_w(0);
    CHECK(h->w_active == 1, "W toggles on");
    CHECK(h->mp == mp_before - ARENA_MP_COST_W, "activating a toggle spends its flat mana cost");

    int mp_after_activate = h->mp;
    arena_toggle_w(0);
    CHECK(h->w_active == 0, "W toggles back off");
    CHECK(h->mp == mp_after_activate, "deactivating a toggle is free -- canceling isn't a new cast");
}

static void test_mp_toggle_w_blocked_when_insufficient(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    h->mp = ARENA_MP_COST_W - 1; /* one short */

    arena_toggle_w(0);

    CHECK(h->w_active == 0, "insufficient mana blocks activating a toggle, same as any other cast");
    CHECK(h->mp == ARENA_MP_COST_W - 1, "a blocked activation doesn't spend the mana it didn't have");
}

static void test_unicorn_r_doubles_armor_temporarily(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    float base_armor = arena_hero_armor(h);
    arena_cast_r(0);
    CHECK(arena_hero_armor(h) == base_armor * 2.0f, "R doubles armor while active");
    CHECK(h->r_cooldown_ms == ARENA_UNICORN_R_COOLDOWN_MS, "R starts on cooldown after cast");
    /* Advance past the buff's duration but not its cooldown. */
    arena_update(ARENA_UNICORN_R_DURATION_MS + 100);
    CHECK(arena_hero_armor(h) == base_armor, "R's armor buff expires after its duration");
}

static void test_unicorn_armor_reduces_incoming_damage(void) {
    arena_init();
    ArenaHero *h = &arena_state.heroes[0];
    ArenaHero *bot = &arena_state.heroes[1];
    /* Default roster is Unicorn (player) vs Duck (bot, S170-31) -- Duck has
       no passive armor, same numeric result as the old "plain melee" bot
       had, but for a different reason now (a real kit with zero armor, not
       an absence of a kit). Confirm the hero's armor actually reduces what
       it takes, not just that armor is nonzero. */
    CHECK(arena_hero_armor(h) > 0.0f, "The Unicorn has nonzero passive armor");
    CHECK(arena_hero_armor(bot) == 0.0f, "The Duck has no passive armor");
}

/* --- The Duck's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-31) --- */

static void test_duck_q_pulls_foe_and_damages(void) {
    arena_init(); /* player=Unicorn, bot=Duck */
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = duck->x + 4.0f; /* within ARENA_DUCK_Q_RANGE */
    foe->z = duck->z;
    float foe_x_before = foe->x;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->x < foe_x_before, "Q pulls the foe toward the Duck");
    CHECK(foe->hp < foe_hp_before, "Q damages the foe when the pull lands in range");
    CHECK(duck->q_cooldown_ms == ARENA_DUCK_Q_COOLDOWN_MS, "Q starts on cooldown after cast");
}

static void test_duck_q_out_of_range_whiffs(void) {
    arena_init();
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = duck->x + ARENA_DUCK_Q_RANGE + 5.0f; /* well beyond range */
    foe->z = duck->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does no damage");
    CHECK(duck->q_cooldown_ms == 0, "Q out of range does not consume its cooldown -- it whiffed, not cast");
}

static void test_duck_q_never_pulls_past_the_duck(void) {
    arena_init();
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    /* Foe closer than the pull distance -- must stop at the Duck, not fly past it. */
    foe->x = duck->x + 1.0f;
    foe->z = duck->z;

    arena_cast_q(1);

    CHECK(fabsf(foe->x - duck->x) < 0.01f, "a close foe is pulled to the Duck's position, not past it");
}

static void test_duck_r_bigger_pull_and_damage_than_q(void) {
    /* Both sides Duck (no armor on either), so the damage dealt isn't
       confounded by the default foe (Unicorn) having passive armor. */
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_DUCK);
    ArenaHero *duck = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = duck->x + 8.0f; /* within R's range, beyond Q's */
    foe->z = duck->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);

    CHECK(foe->hp == foe_hp_before - ARENA_DUCK_R_DAMAGE, "R deals its own (larger) damage amount");
    CHECK(duck->r_cooldown_ms == ARENA_DUCK_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_duck_has_no_w(void) {
    arena_init();
    ArenaHero *duck = &arena_state.heroes[1];
    /* Government Clearance needs objective structures that don't exist in
       this arena -- toggling W for a Duck must no-op, not crash or silently
       borrow Unicorn's regen-toggle behavior. */
    arena_toggle_w(1);
    CHECK(duck->w_active == 0, "toggling W for The Duck is a no-op -- it has no W in this arena");
}

static void test_hero_dispatch_is_by_hero_not_owner_slot(void) {
    /* S170-31's whole point: kit dispatch generalized away from S170-18's
       "owner 0 == Unicorn" hardcoding. Swap the roster and confirm Unicorn's
       kit still works correctly from owner slot 1. */
    arena_init_with_heroes(ARENA_HERO_DUCK, ARENA_HERO_UNICORN);
    ArenaHero *unicorn = &arena_state.heroes[1];
    float base_armor = arena_hero_armor(unicorn);
    arena_cast_r(1);
    CHECK(arena_hero_armor(unicorn) == base_armor * 2.0f,
          "Unicorn's R still doubles armor when Unicorn is in owner slot 1, not slot 0");
}

/* --- The Ghost's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-32) --- */

/* S170-140: Ghost's Q (Alien Frequency) converted from an instant hit to a
 * real projectile -- same test shape as Gary's Q (test_gary_q_*), plus one
 * extra check that its on-hit silence actually lands via the generic
 * on_hit_silence_ms field. */
static void test_ghost_q_cast_spawns_projectile_no_instant_effect(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before,
          "casting Q does not deal instant damage -- it fires a projectile instead");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == 0, "no instant silence either -- it lands on hit, not on cast");
    ArenaProjectile *p = find_active_projectile();
    CHECK(p != NULL, "a projectile is actually spawned on cast");
    CHECK(arena_state.heroes[0].q_cooldown_ms == ARENA_GHOST_Q_COOLDOWN_MS,
          "cooldown is spent on cast, regardless of the shot's eventual outcome");
}

static void test_ghost_q_out_of_range_whiffs_no_projectile(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE + 5.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);

    CHECK(find_active_projectile() == NULL, "no projectile spawns when no foe is in range at cast time");
    CHECK(arena_state.heroes[0].q_cooldown_ms == 0, "an out-of-range whiff doesn't consume the cooldown");
}

static void test_ghost_q_projectile_damages_and_silences_on_hit(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before, "a stationary target is hit once the projectile travels far enough to reach it");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == ARENA_GHOST_Q_SILENCE_MS, "Q silences the foe on a landed hit, carried by the projectile's on_hit_silence_ms");
    CHECK(find_active_projectile() == NULL, "the projectile deactivates on hit, doesn't linger");
}

static void test_ghost_q_projectile_misses_and_no_silence_if_target_steps_off_line(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GHOST_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f; /* real dodge -- steps off the firing line */
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "a target that dodges takes no damage");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == 0, "...and isn't silenced either -- the whole effect rides the hit, not the cast");
}

static void test_silenced_hero_cannot_cast(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *unicorn = &arena_state.heroes[0];
    unicorn->silenced_ms = 500;
    float x_before = unicorn->x;

    arena_cast_q(0); /* Unicorn's Q would normally dash it forward */

    CHECK(unicorn->x == x_before, "a silenced hero's Q cast is a no-op");
    CHECK(unicorn->q_cooldown_ms == 0, "the no-op cast does not consume a cooldown either");
}

static void test_ghost_w_grants_intangibility_and_expires(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];

    arena_toggle_w(1);
    CHECK(ghost->intangible_ms == ARENA_GHOST_W_INTANGIBLE_MS, "W grants intangibility");
    CHECK(ghost->w_cooldown_ms == ARENA_GHOST_W_COOLDOWN_MS, "W starts on its own cooldown");

    arena_update(ARENA_GHOST_W_INTANGIBLE_MS + 100);
    CHECK(ghost->intangible_ms == 0, "intangibility expires after its duration");
}

static void test_intangible_hero_cannot_be_hit(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *unicorn = &arena_state.heroes[0];
    ArenaHero *ghost = &arena_state.heroes[1];
    ghost->intangible_ms = 1000;
    /* Adjacent, in auto-attack range, so resolve_combat would normally hit. */
    ghost->x = unicorn->x + 0.5f;
    ghost->z = unicorn->z;
    int ghost_hp_before = ghost->hp;

    arena_update(16);

    CHECK(ghost->hp == ghost_hp_before, "an intangible hero takes no auto-attack damage");
}

/* S170-144: "ensure aoe damage spells hit creeps" -- AoE zone/aura ticks now hit jungle and
 * lane creeps too, not just heroes. */
static void test_ghost_r_zone_damages_enemy_jungle_creep(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    /* S170-161: team creeps now spawn at their team's graveyard and march
       toward any node their team doesn't own -- team 1 owns every node
       here so its creep has nowhere to march, staying put at the
       graveyard for the whole test. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 2; /* team 1 owns everything */
    arena_tick_creeps(16); /* spawn */
    float gx, gz;
    arena_graveyard_position(1, &gx, &gz);
    /* Within zone radius but outside melee attack range -- isolates this to
       the zone-damage path, distinct from the existing, separate
       arena_hero_attack_creeps melee mechanic (see the sibling
       "does not damage own team" test's own comment for why this matters). */
    arena_state.heroes[0].x = gx + 3.0f;
    arena_state.heroes[0].z = gz;
    int hp_before = arena_state.creeps[0].hp;

    arena_cast_r(0);
    arena_update_teams(1000); /* one full zone tick */

    CHECK(arena_state.creeps[0].hp < hp_before, "Ghost's R zone damages an enemy team-flavored jungle creep standing in it");
}

static void test_ghost_r_zone_does_not_damage_own_team_jungle_creep(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    /* S170-161: team 0 owns everything -- its creep has nowhere to march,
       stays at its graveyard spawn for the whole test. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything */
    arena_tick_creeps(16);
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    /* Positioned within the zone radius (ARENA_GHOST_R_RADIUS) but OUTSIDE
       melee attack range (ARENA_ATTACK_RANGE) of the creep -- isolates this
       to the zone-damage path specifically, since a hero standing directly
       on top of a jungle creep would also melee-auto-attack it via the
       existing, separate arena_hero_attack_creeps mechanic (which lets any
       hero attack any creep regardless of flavor; only the reward differs). */
    arena_state.heroes[0].x = gx + 3.0f;
    arena_state.heroes[0].z = gz;
    int hp_before = arena_state.creeps[0].hp;

    arena_cast_r(0);
    arena_update_teams(1000);

    CHECK(arena_state.creeps[0].hp == hp_before, "Ghost's R zone does not damage the caster's own team's jungle creep");
}

static void test_ghost_r_zone_damages_enemy_lane_creep(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    ArenaLaneCreep *lc = &arena_state.lane_creeps[0];
    lc->active = 1; lc->alive = 1; lc->team = 1; /* enemy lane creep */
    lc->hp = lc->max_hp = ARENA_LANE_CREEP_HP;
    /* Within zone radius but outside melee attack range -- isolates this to
       the zone-damage path, distinct from arena_hero_attack_lane_creeps. */
    lc->x = 3.0f; lc->z = 0;

    arena_cast_r(0);
    arena_update_teams(1000);

    CHECK(lc->hp < ARENA_LANE_CREEP_HP, "Ghost's R zone damages an enemy lane creep standing in it");
}

static void test_pizza_aura_damages_enemy_jungle_creep(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    /* S170-161: team 1 owns everything -- its creep has nowhere to march. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 2;
    arena_tick_creeps(16);
    float gx, gz;
    arena_graveyard_position(1, &gx, &gz);
    /* Within the aura radius (ARENA_PIZZA_AURA_RADIUS) but outside melee
       attack range -- isolates this to the aura-damage path, distinct from
       arena_hero_attack_creeps. */
    arena_state.heroes[0].x = gx + 3.0f;
    arena_state.heroes[0].z = gz;
    int hp_before = arena_state.creeps[0].hp;

    arena_update_teams(1000); /* Pizza's aura is always-on, no cast needed */

    CHECK(arena_state.creeps[0].hp < hp_before, "Pizza's always-on burn aura damages a nearby enemy jungle creep, not just heroes");
}

static void test_ghost_r_zone_damages_foe_over_time(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + 3.0f;
    foe->z = ghost->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);
    CHECK(ghost->r_active_ms == ARENA_GHOST_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(ghost->r_cooldown_ms == ARENA_GHOST_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    /* Ghost occupies owner slot 1 ("the bot"), so this same arena_update
       call also runs the bot brain, which may chase into melee range and
       land an auto-attack in the same tick -- an inequality, not exact
       equality, so this test isn't fragile against that separate, correct
       behavior. What it actually proves either way: the zone dealt at
       least its own DPS-worth of damage. */
    arena_update(1000); /* one full zone tick */
    CHECK(foe->hp <= foe_hp_before - ARENA_GHOST_R_DPS,
          "the zone deals at least one DPS-worth of damage per second the foe stands in it");
}

static void test_ghost_r_zone_stays_fixed_when_foe_moves_away(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + 3.0f;
    foe->z = ghost->z;

    arena_cast_r(1);
    /* Foe steps outside the zone radius right after the cast. */
    foe->x = ghost->x + ARENA_GHOST_R_RADIUS + 5.0f;
    int foe_hp_before = foe->hp;

    arena_update(1000);

    CHECK(foe->hp == foe_hp_before, "a foe standing outside the fixed zone takes no zone damage");
}

/* --- The Frog's kit (docs/HEROES_VS0.md, EMILY/BACKLOG.md S170-33) --- */

static void test_frog_q_rewinds_position_and_hp(void) {
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    /* Placed at opposite extremes so the bot-controlled foe's chase (S170-31's
       heuristic AI, unrelated to this test) can't close to melee range
       during the history-building window and confound the HP value. */
    frog->x = -12.0f; frog->z = 0.0f;
    foe->x = 12.0f; foe->z = 0.0f;

    /* Build more than 3s of loopback history at this position/HP. */
    for (int i = 0; i < 14; i++) arena_update(250); /* 14 * 250ms = 3500ms */
    float historical_x = frog->x;
    int historical_hp = frog->hp;

    /* Simulate a fight happening after the history was recorded. */
    frog->x = -2.0f;
    frog->hp = 30;

    arena_cast_q(0);

    CHECK(frog->hp == historical_hp, "Q restores HP from ~3s ago");
    CHECK(fabsf(frog->x - historical_x) < 0.01f, "Q restores position from ~3s ago");
    CHECK(frog->q_cooldown_ms == ARENA_FROG_Q_COOLDOWN_MS, "Q starts on cooldown after cast");
}

static void test_frog_q_uses_oldest_available_history_before_3s_elapsed(void) {
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];
    ArenaHero *foe = &arena_state.heroes[1];
    frog->x = -12.0f; foe->x = 12.0f;

    arena_update(250); /* exactly one sample, well under the 3s window */
    int historical_hp = frog->hp;

    frog->hp = 10; /* simulate damage */
    arena_cast_q(0);

    CHECK(frog->hp == historical_hp,
          "with less than 3s of history, Q rewinds to the oldest sample available instead of refusing to cast");
}

static void test_frog_r_vanishes(void) {
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];

    arena_cast_r(0);

    CHECK(frog->intangible_ms == ARENA_FROG_R_VANISH_MS, "R grants intangibility for the vanish duration");
    CHECK(frog->r_cooldown_ms == ARENA_FROG_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_frog_w_noop_in_1v1_no_ally(void) {
    /* Borrowed Time is wired for real now (S170-45, arena_nearest_ally) --
       this is no longer "unimplemented," it's a real no-op because 1v1
       genuinely has no teammate to target, same as Ghost's R ally-heal
       side and Doc Wheel's whole kit in this same mode. */
    arena_init_with_heroes(ARENA_HERO_FROG, ARENA_HERO_UNICORN);
    ArenaHero *frog = &arena_state.heroes[0];
    int cooldown_before = frog->w_cooldown_ms;

    arena_toggle_w(0);

    CHECK(frog->w_active == 0 && frog->intangible_ms == 0,
          "toggling W for The Frog leaves w_active/intangible_ms untouched -- Borrowed Time targets an ally, not self");
    CHECK(frog->w_cooldown_ms == cooldown_before,
          "no ally in 1v1 means the cast whiffs -- cooldown is not consumed");
}

/* Regression test, found live 2026-07-24 (NORTHSTAR §13, the MOBA-is-the-
 * product pivot): arena_bot_enabled was added to stop the internal bot from
 * *moving* owner 1 once a real second player connects (apps/arena_server),
 * but bot_cast_kit_if_ready (ability casts -- including Duck's Q, which
 * pulls the foe) was still being called unconditionally. A real second
 * player's hero would still get yanked around and attacked by the "disabled"
 * bot. Confirmed live against a real arena_server with zero clients
 * connected: owner 0 moved and took damage despite never sending a move
 * command, because Duck's Q kept firing. Fixed by gating the kit-cast call
 * the same way as the movement call. */
static void test_arena_bot_enabled_gates_kit_casts_too(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    arena_bot_enabled = 0;
    /* Put the Duck (owner 1) in range of the Unicorn (owner 0) with its Q
       off cooldown -- if kit-casting weren't gated, this alone would pull
       and damage owner 0 within a handful of ticks. z=15 keeps both heroes
       clear of every ArenaNode's jungle-creep aggro radius (S170-119: the
       map's center node now sits at (0,0), which this test used to use
       directly -- a creep spawning on the hero would confound this test's
       own signal with an unrelated system). */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 15.0f;
    arena_state.heroes[1].x = 3.0f; arena_state.heroes[1].z = 15.0f;
    int start_hp = arena_state.heroes[0].hp;
    float start_x = arena_state.heroes[0].x;

    for (int i = 0; i < 200; i++) arena_update(16); /* 3.2s of sim time */

    CHECK(arena_state.heroes[0].hp == start_hp,
          "with arena_bot_enabled=0, the bot's kit-cast AI never damages owner 0 (no real input sent)");
    CHECK(arena_state.heroes[0].x == start_x,
          "with arena_bot_enabled=0, owner 0 is never pulled/moved by the bot's kit AI either");
    arena_bot_enabled = 1; /* restore the default for any test run after this one */
}

/* ---- Team mode (10v10), 2026-07-24, NORTHSTAR §13 cont'd ---- */

static void test_arena_init_teams_sets_up_both_sides(void) {
    arena_init_teams();
    int team0 = 0, team1 = 0;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        CHECK(arena_state.heroes[i].active, "every one of the 20 slots is active in team mode");
        CHECK(arena_state.heroes[i].alive, "every slot starts alive");
        if (arena_state.heroes[i].team == 0) team0++; else team1++;
    }
    CHECK(team0 == ARENA_TEAM_SIZE && team1 == ARENA_TEAM_SIZE,
          "exactly ARENA_TEAM_SIZE heroes on each team");
}

static void test_nearest_enemy_finds_closest_on_other_team(void) {
    arena_init_teams();
    /* Owner 0 (team 0) -- put two team-1 heroes at different distances. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;     /* far */
    arena_state.heroes[ARENA_TEAM_SIZE + 1].x = 1; arena_state.heroes[ARENA_TEAM_SIZE + 1].z = 0; /* near */

    ArenaHero *nearest = arena_nearest_enemy(0);
    CHECK(nearest == &arena_state.heroes[ARENA_TEAM_SIZE + 1],
          "arena_nearest_enemy picks the closer of two enemies on the other team");
}

static void test_nearest_enemy_ignores_teammates_and_dead_heroes(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* A teammate right next to owner 0 should never be picked. */
    arena_state.heroes[1].x = 0.1f; arena_state.heroes[1].z = 0;
    /* The nearest enemy is dead -- should be skipped in favor of a living one further out. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 0;
    arena_state.heroes[ARENA_TEAM_SIZE + 1].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE + 1].z = 0;

    ArenaHero *nearest = arena_nearest_enemy(0);
    CHECK(nearest == &arena_state.heroes[ARENA_TEAM_SIZE + 1],
          "arena_nearest_enemy skips teammates entirely and dead heroes on the enemy team");
}

static void test_team_melee_converges_multiple_attackers_on_one_target(void) {
    arena_init_teams();
    /* Deactivate everyone except: owner 0 + owner 1 (team 0), and one lone
       team-1 hero within melee range of both -- a real "two attackers, one
       target" team-fight case the old 1v1 pairwise resolve_combat never had
       to express. */
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;

    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1.0f; arena_state.heroes[1].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_update_teams(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < 100,
          "the lone enemy hero takes damage from being in melee range of two attackers at once");
}

static void test_team_wipe_alone_does_not_win_the_match(void) {
    /* S170-153: team-wipe was the ORIGINAL win condition (S170-45) but was
       replaced by the Arathi-Basin-style resource race -- a wiped team can
       still come back via graveyard/node respawns and isn't eliminated
       just because every hero happens to be down or deactivated right now. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Only owner 0 (team 0) is left active and alive -- team 1 is wiped. */
    arena_state.heroes[1].active = 0;

    arena_update_teams(16);

    CHECK(arena_state.winner == 0, "team 1 being wiped no longer ends the match on its own -- resources decide it now");
}

/* S170-45: allies. arena_nearest_ally is the enabling primitive for every
 * ally-targeted kit piece previously skipped for having no target in 1v1
 * (Ghost's R heal side, Frog's W, Doc Wheel's entire kit). */

static void test_nearest_ally_finds_closest_teammate(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 5; arena_state.heroes[1].z = 0;  /* far teammate */
    arena_state.heroes[2].x = 1; arena_state.heroes[2].z = 0;  /* near teammate */

    ArenaHero *nearest = arena_nearest_ally(0);
    CHECK(nearest == &arena_state.heroes[2], "arena_nearest_ally picks the closer of two teammates");
}

static void test_nearest_ally_ignores_enemies_and_dead_teammates(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* An enemy right next to owner 0 should never be picked. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.1f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    /* The nearest teammate is dead -- should be skipped in favor of a living one further out. */
    arena_state.heroes[1].x = 0.5f; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].alive = 0;
    arena_state.heroes[2].x = 3.0f; arena_state.heroes[2].z = 0;

    ArenaHero *nearest = arena_nearest_ally(0);
    CHECK(nearest == &arena_state.heroes[2],
          "arena_nearest_ally skips enemies entirely and dead teammates");
}

static void test_nearest_ally_never_returns_self(void) {
    arena_init_teams();
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[1].x = 0; arena_state.heroes[1].z = 0; /* exactly on top of owner 0 */

    ArenaHero *nearest = arena_nearest_ally(0);
    CHECK(nearest == &arena_state.heroes[1] && nearest != &arena_state.heroes[0],
          "arena_nearest_ally never returns owner itself, even at distance 0 from another candidate");
}

static void test_nearest_ally_null_in_1v1(void) {
    /* 1v1 (arena_init_with_heroes) sets heroes[0].team=0, heroes[1].team=1 --
       no teammate exists at all, so every ally-targeted kit piece must
       degrade to a safe no-op here, not crash. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    CHECK(arena_nearest_ally(0) == NULL, "arena_nearest_ally returns NULL in 1v1 -- no teammate exists");
}

static void test_ghost_r_zone_heals_ally_in_team_mode(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 0; arena_state.heroes[1].z = 0; /* ally, inside the zone radius */
    arena_state.heroes[1].hp = 50; arena_state.heroes[1].max_hp = 100;

    arena_cast_r(0);
    /* One full 1000ms zone tick, via the public per-tick entry point --
       tick_hero_kit itself is static to arena_game.c. */
    arena_update_teams(1000);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_GHOST_R_DPS,
          "Recital's ally-heal side heals a teammate standing in the zone");
}

static void test_ghost_r_zone_does_not_heal_ally_outside_radius(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_GHOST;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = ARENA_GHOST_R_RADIUS * 3.0f; arena_state.heroes[1].z = 0; /* well outside */
    arena_state.heroes[1].hp = 50; arena_state.heroes[1].max_hp = 100;

    arena_cast_r(0);
    arena_update_teams(1000);

    CHECK(arena_state.heroes[1].hp == 50, "Recital's ally-heal side does not reach an ally outside the zone radius");
}

static void test_frog_w_refunds_ally_next_cast_cooldown(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FROG;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].hero_id = ARENA_HERO_UNICORN;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    /* Unicorn's Q dashes toward its move target if moving, else toward a
       foe -- neither exists by default in this isolated 2-hero setup, so
       give it a move target or the dash (and thus the cooldown-setting
       code path) never actually runs. */
    arena_state.heroes[1].moving = 1;
    arena_state.heroes[1].target_x = 5; arena_state.heroes[1].target_z = 0;

    arena_toggle_w(0); /* Frog's Borrowed Time on the nearest ally (owner 1) */
    CHECK(arena_state.heroes[1].next_cast_refund == 1,
          "Borrowed Time places the refund buff on the nearest ally, not the caster");

    arena_cast_q(1); /* Unicorn's Q would normally set a long cooldown */
    CHECK(arena_state.heroes[1].q_cooldown_ms == 0,
          "the buffed ally's next cast is refunded to zero cooldown");
    CHECK(arena_state.heroes[1].next_cast_refund == 0,
          "the refund buff is consumed after one cast, not reusable");
}

static void test_frog_w_whiffs_with_no_ally_cooldown_not_consumed(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FROG;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].w_cooldown_ms == 0,
          "Borrowed Time whiffs with no living ally -- cooldown is not consumed");
}

static void test_doc_wheel_q_heals_more_at_lower_hp(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].max_hp = 100;
    arena_state.heroes[1].hp = 95; /* near-full HP */

    arena_cast_q(0);
    int healed_near_full = arena_state.heroes[1].hp - 95;

    /* Reset and try again from low HP. */
    arena_state.heroes[0].q_cooldown_ms = 0;
    arena_state.heroes[1].hp = 10; /* near-empty HP */
    arena_cast_q(0);
    int healed_near_empty = arena_state.heroes[1].hp - 10;

    CHECK(healed_near_empty > healed_near_full,
          "Bedside Manner heals more the lower the target's current HP%% is");
}

static void test_doc_wheel_q_cleanses_silence(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 100;
    arena_state.heroes[1].silenced_ms = 2000;

    arena_cast_q(0);

    CHECK(arena_state.heroes[1].silenced_ms == 0, "Bedside Manner cleanses an active silence");
}

static void test_doc_wheel_q_whiffs_with_no_ally_cooldown_not_consumed(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;

    arena_cast_q(0);

    CHECK(arena_state.heroes[0].q_cooldown_ms == 0,
          "Bedside Manner whiffs with no living ally -- cooldown is not consumed");
}

static void test_doc_wheel_w_teleports_to_ally(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 7.5f; arena_state.heroes[1].z = -3.0f;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == 7.5f && arena_state.heroes[0].z == -3.0f,
          "House Call teleports Doc Wheel to the nearest ally's exact position");
}

static void test_doc_wheel_r_heals_allies_in_radius_only(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* in radius */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[2].x = ARENA_DOC_WHEEL_R_RADIUS * 3.0f; arena_state.heroes[2].z = 0; /* out of radius */
    arena_state.heroes[2].max_hp = 100; arena_state.heroes[2].hp = 50;

    arena_cast_r(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_DOC_WHEEL_R_HEAL,
          "No Combat Power heals an ally within radius");
    CHECK(arena_state.heroes[2].hp == 50,
          "No Combat Power does not reach an ally outside radius");
}

static void test_doc_wheel_r_consumes_cooldown_even_with_zero_allies(void) {
    /* A real ultimate commitment, not a whiff-refunded poke -- unlike Q,
       which no-ops (and doesn't spend its cooldown) with no ally, R always
       "lands" per its own header comment. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;

    arena_cast_r(0);

    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_DOC_WHEEL_R_COOLDOWN_MS,
          "No Combat Power consumes its cooldown even when zero allies are in range");
}

/* S170-46: territory/node system + Tree, Pizza, and merged Flamel (absorbed
 * former Druid). */

static void test_node_channel_starts_and_flips_node_neutral_immediately(void) {
    /* Node starts owned by team 1 (as if team 0 had already captured team
       1's home node in some earlier state) -- team 1 shows up alone and
       begins a channel. The node must go neutral the instant the channel
       starts, not stay owned by team 1 until the channel finishes -- this
       is the "neutral period... as you wait for it to finish capturing"
       the founder asked for. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 2;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].owner == 0,
          "a node flips to neutral the instant a lone team begins channeling it, before the channel finishes");
    CHECK(arena_state.nodes[0].capturing_team == 0, "the channel is now attributed to the team that started it");
}

static void test_node_channel_completes_to_capturing_team(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS - 1);
    CHECK(arena_state.nodes[0].owner == 0, "the node is still neutral one tick before the channel completes");

    arena_tick_nodes(1);
    CHECK(arena_state.nodes[0].owner == 1, "the node flips to the channeling team's ownership once the channel completes");
    CHECK(arena_state.nodes[0].capturing_team == -1, "the channel clears once it completes, ready for the next contest");
}

static void test_node_channel_interrupted_by_mixed_presence_loses_all_progress(void) {
    /* Team 0 channels alone for a while, then an enemy shows up --
       "interruptable": progress is lost entirely, and since the node had
       already flipped neutral, it STAYS neutral rather than reverting to
       whatever it was before -- the actual teeth behind "losing due to
       ignoring the objective." */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS / 2);
    CHECK(arena_state.nodes[0].capture_progress_ms > 0, "the channel has real progress partway through");

    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1, "an enemy showing up interrupts the channel");
    CHECK(arena_state.nodes[0].capture_progress_ms == 0, "all progress is lost on interrupt, not preserved");
    CHECK(arena_state.nodes[0].owner == 0,
          "the node stays neutral after an interrupt -- it is not handed back to the original owner for free");
}

static void test_node_channel_interrupted_when_capturing_team_leaves(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS / 2);
    CHECK(arena_state.nodes[0].capturing_team == 0, "team 0 is channeling");

    arena_state.heroes[0].x = 1000.0f; arena_state.heroes[0].z = 1000.0f;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1, "the channel is interrupted once the channeling team leaves");
    CHECK(arena_state.nodes[0].capture_progress_ms == 0, "leaving loses all progress, same as being contested");
}

static void test_node_already_owned_by_present_team_has_no_channel(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 1; /* already team 0's -- standing on your own node shouldn't spin up a channel */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1, "no channel runs on a node the present team already owns");
    CHECK(arena_state.nodes[0].owner == 1, "owner is unchanged since there was nothing to capture");
}

static void test_tree_doubles_channel_speed(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capture_progress_ms == (int)(1000.0f * ARENA_TREE_CHANNEL_SPEED_MULT),
          "Root Network: a Tree on the channeling team doubles capture progress this tick");
}

static void test_flamel_mark_speeds_up_channel_on_marked_ground(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.nodes[0].marked_by_team = 0;

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capture_progress_ms == 1000 + ARENA_FLAMEL_MARK_CHANNEL_BONUS_MS,
          "Overgrowth: capturing on ground marked by the capturing team's own Flamel finishes faster");
}

static void test_pizza_corrupts_any_channel_regardless_of_side(void) {
    /* A Pizza on the SAME team as the sole capturer still corrupts the
       attempt -- corruption doesn't pick a side, matching the doc's
       original "regardless of team composition" framing. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_tick_nodes(1000);
    CHECK(arena_state.nodes[0].capturing_team == 0, "team 0 channels normally with no Pizza around");

    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1,
          "a Pizza's presence corrupts the channel even on her own team's attempt");
}

static void test_tree_q_roots_and_damages_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor, so damage isn't reduced */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TREE_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_TREE_Q_DAMAGE,
          "Vine Lash damages an enemy in range");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_TREE_Q_ROOT_MS,
          "Vine Lash roots the enemy it hits");
}

static void test_tree_q_out_of_range_whiffs(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TREE_Q_RANGE * 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);

    CHECK(arena_state.heroes[0].q_cooldown_ms == 0, "Vine Lash whiffs out of range -- cooldown is not consumed");
}

static void test_tree_r_self_roots_grants_armor_and_heals(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[0].hp = 50; arena_state.heroes[0].max_hp = 100;

    arena_cast_r(0);

    CHECK(arena_state.heroes[0].rooted_ms == ARENA_TREE_R_ROOT_MS, "Grand Secret self-roots the Tree");
    CHECK(arena_state.heroes[0].hp == 50 + ARENA_TREE_R_HEAL, "Grand Secret heals the Tree");
    CHECK(arena_hero_armor(&arena_state.heroes[0]) == (float)ARENA_TREE_R_ARMOR_BONUS,
          "Grand Secret grants the armor bonus while active");
}

static void test_tree_r_makes_immune_to_duck_pull(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_TREE;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;
    arena_cast_r(ARENA_TEAM_SIZE); /* Tree self-roots first */
    float rooted_x = arena_state.heroes[ARENA_TEAM_SIZE].x;

    arena_cast_q(0); /* Duck's Telekinetic Yank */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].x == rooted_x,
          "Grand Secret's self-root makes the Tree immune to Duck's pull -- position unchanged");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < 100,
          "the pull is blocked but the Duck's damage still lands");
}

static void test_pizza_q_damages_and_applies_burn(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor, so damage isn't reduced */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_PIZZA_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_PIZZA_Q_DAMAGE,
          "Nobody Checked damages an enemy in range");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == ARENA_PIZZA_Q_BURN_MS,
          "Nobody Checked applies a burn DoT to the enemy it hits");
}

static void test_pizza_burn_ticks_damage_over_time(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].burning_ms = ARENA_PIZZA_Q_BURN_MS;
    arena_state.heroes[0].burn_dps = ARENA_PIZZA_Q_BURN_DPS;

    arena_update_teams(1000); /* one full 1000ms burn tick */

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_PIZZA_Q_BURN_DPS,
          "an active burn deals its DPS once per 1000ms tick");
}

static void test_pizza_passive_aura_damages_nearby_foe(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_PIZZA_AURA_RADIUS - 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_update_teams(1000);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_PIZZA_AURA_DPS,
          "Uninvestigated Fire's always-on aura damages a nearby enemy without any cast");
    CHECK(arena_state.heroes[0].hp == arena_state.heroes[0].max_hp,
          "Pizza is immune to its own burn aura");
}

static void test_pizza_r_prevents_death_for_duration(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_PIZZA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].hp = 1; arena_state.heroes[0].max_hp = 100; /* one hit from death */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_DUCK_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_r(0); /* Nobody Ever Checks */
    arena_cast_q(ARENA_TEAM_SIZE); /* Duck's Telekinetic Yank, would normally kill a 1-HP target */

    CHECK(arena_state.heroes[0].hp == 1, "the damage floor holds Pizza at 1 HP against lethal damage");
    CHECK(arena_state.heroes[0].alive, "Pizza survives what would otherwise be a killing blow");
}

static void test_flamel_q_roots_without_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_FLAMEL_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_FLAMEL_Q_ROOT_MS,
          "Vine Growth roots an enemy in range");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100,
          "Vine Growth is pure crowd control -- it deals no damage");
}

static void test_flamel_w_heals_allies_in_radius(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* in radius */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[2].x = ARENA_FLAMEL_W_RADIUS * 3.0f; arena_state.heroes[2].z = 0; /* out of radius */
    arena_state.heroes[2].max_hp = 100; arena_state.heroes[2].hp = 50;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_FLAMEL_W_HEAL_BASE,
          "Philosopher's Bloom heals an ally within radius at the base rate");
    CHECK(arena_state.heroes[2].hp == 50, "Philosopher's Bloom does not reach an ally outside radius");
}

static void test_flamel_w_heals_more_on_marked_ground(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = arena_state.nodes[0].x; arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[1].x = arena_state.nodes[0].x + 1.0f; arena_state.heroes[1].z = arena_state.nodes[0].z;
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.nodes[0].marked_by_team = arena_state.heroes[0].team; /* pre-marked, as if Flamel had stood here already */

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_FLAMEL_W_HEAL_MARKED,
          "Philosopher's Bloom heals for more when cast on Flamel's own marked ground");
}

static void test_flamel_r_roots_enemies_and_heals_allies_in_zone(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, inside the zone */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_FLAMEL_R_HEAL_PER_TICK,
          "Elixir of Wild Growth heals an ally standing in the zone");
    /* > 0, not an exact value: the root is applied mid-loop (hero 0's
       iteration) and then generically decremented by the same dt_ms during
       the target's OWN iteration later in the same arena_update_teams
       call -- an artifact of iteration order within one tick, same
       reasoning as why other status effects are asserted right after a
       standalone cast rather than after a full update tick. */
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms > 0,
          "Elixir of Wild Growth roots an enemy standing in the zone");
}

static void test_flamel_r_mass_marks_nodes_in_radius(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_FLAMEL;
    arena_state.heroes[0].x = arena_state.nodes[0].x; arena_state.heroes[0].z = arena_state.nodes[0].z;

    arena_cast_r(0);

    CHECK(arena_state.nodes[0].marked_by_team == arena_state.heroes[0].team,
          "Elixir of Wild Growth mass-marks nodes within radius at cast time");
}

static void test_rooted_hero_cannot_move(void) {
    arena_init();
    arena_state.heroes[0].rooted_ms = 1000;
    arena_set_move_target(0, 5.0f, 5.0f);
    float x0 = arena_state.heroes[0].x, z0 = arena_state.heroes[0].z;

    arena_update(16);

    CHECK(arena_state.heroes[0].x == x0 && arena_state.heroes[0].z == z0,
          "a rooted hero does not move even with a move command queued");
}

/* S170-47: Morrigan (TYLER #68) and Dagda (TYLER #69). */

static void test_morrigan_passive_grants_armor_on_contested_node(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.nodes[0].owner = 0; /* contested */

    CHECK(arena_hero_armor(&arena_state.heroes[0]) == (float)ARENA_MORRIGAN_PASSIVE_ARMOR_BONUS,
          "Contested Ground grants armor while standing on a contested node");

    arena_state.nodes[0].owner = 1; /* claimed by a team -- no longer contested */
    CHECK(arena_hero_armor(&arena_state.heroes[0]) == 0.0f,
          "Contested Ground grants no armor once the node is claimed");
}

static void test_morrigan_q_executes_harder_at_low_hp(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_MORRIGAN_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 95; /* near-full */

    arena_cast_q(0);
    int dmg_near_full = 95 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_state.heroes[0].q_cooldown_ms = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 10; /* near-empty */
    arena_cast_q(0);
    int dmg_near_empty = 10 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    CHECK(dmg_near_empty > dmg_near_full,
          "The Washer's Strike deals more damage the lower the target's current HP%% is");
}

static void test_morrigan_w_teleports_and_roots_nearest_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 9.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = -4.0f;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == 9.0f && arena_state.heroes[0].z == -4.0f,
          "Three Forms teleports Morrigan to the nearest enemy's exact position");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_MORRIGAN_W_ROOT_MS,
          "Three Forms roots the enemy on arrival");
}

static void test_morrigan_r_zone_executes_harder_at_low_hp(void) {
    /* Enemy positioned inside the R radius but outside melee attack range,
       so the zone tick's own damage isn't confounded by an auto-attack
       landing in the same update. Two separate setups (near-full vs
       near-empty target HP), comparing the tick's damage delta rather than
       an absolute post-tick HP -- same pattern as the Q execute test,
       avoiding HP-floor clamping at 0 for the near-empty case. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_MORRIGAN_R_RADIUS - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 950; /* near-full */

    arena_cast_r(0);
    arena_update_teams(1000);
    int dmg_near_full = 950 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MORRIGAN;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_MORRIGAN_R_RADIUS - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 1000;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = 50; /* near-empty */

    arena_cast_r(0);
    arena_update_teams(1000);
    int dmg_near_empty = 50 - arena_state.heroes[ARENA_TEAM_SIZE].hp;

    CHECK(dmg_near_empty > dmg_near_full,
          "The Crow Confirms It ticks harder against a near-dead enemy standing in the zone");
}

static void test_dagda_passive_regenerates_hp(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].hp = 50;

    arena_update_teams(1000);

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_DAGDA_PASSIVE_REGEN_PER_SEC,
          "The Undry passively regenerates HP every tick with no cast at all");
}

static void test_dagda_q_kills_when_enemy_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_DAGDA_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_DAGDA_Q_KILL_DAMAGE,
          "the club's killing end damages an enemy in range");
}

static void test_dagda_q_revives_when_only_hurt_ally_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0;
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;

    arena_cast_q(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_DAGDA_Q_REVIVE_HEAL,
          "the club's reviving end heals a hurt ally when no enemy is in range");
}

static void test_dagda_w_heals_allies_and_cc_enemies_at_once(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, in radius */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, in radius */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_DAGDA_W_ALLY_HEAL,
          "Uaithne's joy strain heals an ally in radius");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_DAGDA_W_ROOT_MS,
          "Uaithne's sorrow strain roots an enemy in radius");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms == ARENA_DAGDA_W_SILENCE_MS,
          "Uaithne's sleep strain silences an enemy in radius, in the same cast");
}

static void test_dagda_r_floor_and_heal(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DAGDA;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 1; /* one hit from death */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* melee range */

    arena_cast_r(0); /* the porridge: floor + heal */
    CHECK(arena_state.heroes[0].hp == 1 + ARENA_DAGDA_R_HEAL,
          "the porridge heals Dagda for real, not just holding him at a floor");

    /* Repeated melee auto-attacks, well within the 3000ms floor window,
       dealing far more cumulative damage than the healed HP total -- would
       be lethal without the floor. */
    for (int i = 0; i < 180; i++) arena_update_teams(16); /* ~2880ms */

    CHECK(arena_state.heroes[0].alive && arena_state.heroes[0].hp == 1,
          "the damage floor holds Dagda at 1 HP against repeated attacks that would otherwise be lethal");
}

/* S170-48: The Courier (Ratatoskr, TYLER #32). */

static void test_courier_q_dashes_and_damages(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK; /* no armor */
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_COURIER_Q_DASH_DIST; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_q(0);

    CHECK(arena_state.heroes[0].x > 0.0f, "The Insult, Lightly Edited dashes The Courier toward the enemy");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_COURIER_Q_DAMAGE,
          "the dash damages the enemy on arrival");
}

static void test_courier_q_cleanses_self_debuffs(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].rooted_ms = 500;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    /* silenced_ms > 0 would block the cast entirely (per arena_cast_q's own
       gate), so only rooted_ms is exercised here -- the cleanse still runs
       on a landed cast regardless. */
    arena_cast_q(0);

    CHECK(arena_state.heroes[0].rooted_ms == 0,
          "Lightly Edited cleanses The Courier's own active root on a landed cast");
}

static void test_courier_w_teleports_to_farther_node(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    /* Stand exactly on node 0 -- whichever OTHER node is farthest from here
       is computed below rather than hardcoded, so this test stays valid
       across any ARENA_NODE_COUNT/layout (S170-119: was a 2-node map with
       a hardcoded "node 1 is farther" expectation; now 5 nodes). */
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;

    int expected = 0;
    float best_dist = -1.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        float dx = arena_state.nodes[n].x - arena_state.heroes[0].x;
        float dz = arena_state.nodes[n].z - arena_state.heroes[0].z;
        float dist = dx * dx + dz * dz;
        if (dist > best_dist) { best_dist = dist; expected = n; }
    }

    arena_toggle_w(0);

    CHECK(arena_state.heroes[0].x == arena_state.nodes[expected].x && arena_state.heroes[0].z == arena_state.nodes[expected].z,
          "Between Eagle and Serpent teleports to whichever node is farther away");
}

static void test_courier_r_drains_life_from_nearest_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_COURIER_R_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100 - ARENA_COURIER_R_DRAIN,
          "The Debt Collector's Due drains HP from the nearest enemy");
    CHECK(arena_state.heroes[0].hp == 50 + ARENA_COURIER_R_DRAIN,
          "...and delivers it to The Courier");
}

static void test_courier_r_out_of_range_whiffs(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_COURIER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_COURIER_R_RANGE * 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_r(0);

    CHECK(arena_state.heroes[0].r_cooldown_ms == 0,
          "The Debt Collector's Due whiffs out of range -- cooldown is not consumed");
}

/* S170-51: territorial dynamic jungle creeps. */

static void test_creep_spawns_on_first_tick_with_flavor_from_node_owner(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 1; /* team 0's */
    arena_state.nodes[1].owner = 0; /* neutral/contested */

    arena_tick_creeps(16);

    CHECK(arena_state.creeps[0].alive, "a creep spawns on the very first tick of a match");
    CHECK(arena_state.creeps[0].flavor == ARENA_CREEP_TEAM0 && arena_state.creeps[0].hp == ARENA_CREEP_TEAM_HP,
          "a creep on a team-owned node spawns as that team's flavor, at the weaker team HP");
    CHECK(arena_state.creeps[1].flavor == ARENA_CREEP_NEUTRAL && arena_state.creeps[1].hp == ARENA_CREEP_NEUTRAL_HP,
          "a creep on a contested node spawns as the tougher neutral flavor");
}

/* S170-161: "add jungle creeps use the redgarden dynamic creep ecosystem
 * something simple to start" -- graveyard spawn + march/fan-out toward
 * unowned nodes for team-flavored creeps specifically. */

static void test_team_creep_spawns_at_graveyard_not_node_position(void) {
    arena_init_teams();
    arena_state.nodes[0].owner = 1; /* team 0's node -- creep flavor becomes TEAM0 */

    arena_tick_creeps(16); /* spawn */

    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    CHECK(arena_state.creeps[0].x == gx && arena_state.creeps[0].z == gz,
          "a team-flavored creep spawns at its team's graveyard, not its node's own position");
    CHECK(arena_state.creeps[0].x != arena_state.nodes[0].x || arena_state.creeps[0].z != arena_state.nodes[0].z,
          "the graveyard and the node are genuinely different points");
}

static void test_neutral_creep_still_spawns_at_node_position(void) {
    arena_init_teams();
    arena_state.nodes[0].owner = 0; /* neutral/contested -- no team, no graveyard to spawn from */

    arena_tick_creeps(16);

    CHECK(arena_state.creeps[0].x == arena_state.nodes[0].x && arena_state.creeps[0].z == arena_state.nodes[0].z,
          "a NEUTRAL creep is unaffected by the graveyard-spawn change -- still spawns at its own node's position");
}

static void test_team_creep_marches_toward_nearest_unowned_node(void) {
    arena_init_teams();
    arena_state.nodes[0].owner = 1; /* team 0 owns only node 0 -- nodes 1..4 stay neutral/unowned */

    arena_tick_creeps(16); /* spawn at the graveyard */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    float start_dist_sq = (arena_state.creeps[0].x - gx) * (arena_state.creeps[0].x - gx)
                         + (arena_state.creeps[0].z - gz) * (arena_state.creeps[0].z - gz);
    CHECK(start_dist_sq == 0.0f, "sanity: the creep starts exactly at the graveyard");

    arena_tick_creeps(2000); /* two real seconds of marching */

    float moved_dist_sq = (arena_state.creeps[0].x - gx) * (arena_state.creeps[0].x - gx)
                         + (arena_state.creeps[0].z - gz) * (arena_state.creeps[0].z - gz);
    CHECK(moved_dist_sq > 0.01f,
          "a team-flavored creep marches away from its graveyard spawn toward an unowned node -- 'fan out' from owned nodes");
}

static void test_team_creep_idles_once_its_team_owns_every_node(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything */

    arena_tick_creeps(16); /* spawn */
    float x_after_spawn = arena_state.creeps[0].x, z_after_spawn = arena_state.creeps[0].z;

    arena_tick_creeps(5000); /* five real seconds -- plenty of time to march if it were going to */

    CHECK(arena_state.creeps[0].x == x_after_spawn && arena_state.creeps[0].z == z_after_spawn,
          "a team-flavored creep idles in place once its own team already owns every node -- nothing left to fan out into");
}

static void test_team_creep_march_redirects_when_target_node_gets_captured(void) {
    /* The march target is recomputed live every tick, not locked in once at
       spawn -- if the node a creep was heading toward becomes owned by its
       own team mid-march, it should stop closing on that now-owned node
       (there's nothing further to gain by continuing toward a point that's
       already been resolved), matching S170-161's "recomputed live, reacts
       to ownership changing mid-march" design. */
    arena_init_teams();
    arena_state.nodes[0].owner = 1; /* team 0's home node */
    for (int n = 1; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0; /* all others neutral/unowned */

    arena_tick_creeps(16); /* spawn at the graveyard */
    arena_tick_creeps(2000); /* march partway toward whichever unowned node is nearest */
    float x_partway = arena_state.creeps[0].x, z_partway = arena_state.creeps[0].z;

    /* Team 0 suddenly captures every remaining node -- nothing left unowned. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1;
    arena_tick_creeps(2000); /* the creep should now hold its ground, not keep closing on a node that's already theirs */

    CHECK(arena_state.creeps[0].x == x_partway && arena_state.creeps[0].z == z_partway,
          "a marching creep stops advancing the instant its own team ends up owning every node, mid-march");
}

static void test_creep_attacks_nearby_hero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn */
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS); /* long enough for one attack */

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CREEP_NEUTRAL_DAMAGE,
          "a jungle creep auto-attacks a hero standing within its aggro radius");
}

static void test_hero_does_not_attack_creep_while_an_enemy_hero_is_in_range(void) {
    /* Creeps are a secondary objective -- a hero already trading blows with
       an enemy hero shouldn't split attention onto a nearby creep too. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x + 1.0f; /* within ARENA_ATTACK_RANGE */
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;

    arena_tick_creeps(16); /* spawn */
    int hp_before = arena_state.creeps[0].hp;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.creeps[0].hp == hp_before,
          "a hero with an enemy hero already in range does not also attack a nearby creep this tick");
}

static void test_hero_kills_creep_and_queues_correct_respawn_timer(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* S170-161: team-flavored creeps spawn at their team's graveyard, not
       the node's own position -- team 0 owns everything so it has nowhere
       to march, staying exactly at the graveyard. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team-flavored, so ARENA_CREEP_TEAM_RESPAWN_MS applies */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;

    arena_tick_creeps(16); /* spawn */
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE; /* one hit from death */
    arena_hero_attack_creeps(16);

    CHECK(!arena_state.creeps[0].alive, "the creep dies once its HP is reduced to 0");
    CHECK(arena_state.creeps[0].respawn_ms_remaining == ARENA_CREEP_TEAM_RESPAWN_MS,
          "a team-flavored creep queues the fast team respawn timer, not the slow neutral one");
    CHECK(arena_state.creeps[0].last_attacked_by_owner == 0, "the killing hero is credited as the last attacker");
}

static void test_neutral_creep_kill_grants_capture_bonus_only_while_channeling(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.nodes[0].owner = 0; /* neutral -- ARENA_CREEP_NEUTRAL flavor */

    arena_tick_creeps(16); /* spawn */
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_state.nodes[0].capturing_team = -1; /* not channeling right now */
    arena_state.nodes[0].capture_progress_ms = 0;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.nodes[0].capture_progress_ms == 0,
          "killing the neutral creep grants no capture bonus if the killer's team isn't actually channeling that node");

    arena_tick_creeps(16); /* respawn is queued, not immediate -- re-force it alive for the second half of this test */
    arena_state.creeps[0].alive = 1;
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_state.creeps[0].flavor = ARENA_CREEP_NEUTRAL;
    arena_state.heroes[0].attack_cooldown_ms = 0; /* the first kill above set this; nothing ticks it down outside the full update loop */
    arena_state.nodes[0].capturing_team = 0;
    arena_state.nodes[0].capture_progress_ms = 0;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.nodes[0].capture_progress_ms == ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS,
          "killing the neutral creep while your team is channeling that node grants the big capture bonus");
}

static void test_team_creep_kill_by_owning_team_heals(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything -- creep has nowhere to march */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;
    arena_state.heroes[0].hp = 50; arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16);
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_CREEP_TEAM_KILL_HEAL,
          "killing your own team's jungle creep on your own territory heals you (home-turf resupply)");
}

static void test_team_creep_kill_by_enemy_team_helps_flip_the_node(void) {
    /* Team 1 farms team 0's own jungle creep while team 1 is mid-channel
       trying to flip that node -- the counter-play tool against a
       turtling opponent. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything -- its creep has nowhere to march */
    /* S170-161: node[0] still needs to be the one actually captured below
       (capturing_team/capture_progress_ms live per-node), but the creep
       itself now spawns at team 0's graveyard, not node[0]'s position. */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[ARENA_TEAM_SIZE].x = gx;
    arena_state.heroes[ARENA_TEAM_SIZE].z = gz;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_tick_creeps(16);
    arena_state.creeps[0].hp = ARENA_ATTACK_DAMAGE;
    arena_state.nodes[0].capturing_team = 1; /* team 1 is trying to flip it */
    arena_state.nodes[0].capture_progress_ms = 0;
    arena_hero_attack_creeps(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100, "the enemy killer gets no heal -- that reward is owning-team-only");
    CHECK(arena_state.nodes[0].capture_progress_ms == ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS,
          "farming the enemy's own jungle creep while channeling their node grants the deny capture bonus");
}

/* S170-139: lane creep waves. */

static void test_lane_creep_wave_spawns_for_both_teams_after_initial_delay(void) {
    /* arena_init_teams() arms both teams' wave timer at
       ARENA_LANE_WAVE_INITIAL_DELAY_MS (a short real-MOBA-style grace
       period, not an instant 0:00 spawn) -- confirmed both that nothing
       spawns before it elapses and that a full wave spawns for both teams
       the instant it does. */
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;

    arena_tick_lane_creeps(16);
    int any_active_before_delay = 0;
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        if (arena_state.lane_creeps[i].active) any_active_before_delay = 1;
    }
    CHECK(!any_active_before_delay, "no lane creep wave spawns before the initial delay elapses");

    arena_tick_lane_creeps(ARENA_LANE_WAVE_INITIAL_DELAY_MS);

    int team0_count = 0, team1_count = 0;
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        ArenaLaneCreep *c = &arena_state.lane_creeps[i];
        if (!c->active) continue;
        if (c->team == 0) team0_count++;
        else team1_count++;
    }
    CHECK(team0_count == ARENA_LANE_CREEPS_PER_WAVE, "team 0's first wave spawns a full wave once the initial delay elapses");
    CHECK(team1_count == ARENA_LANE_CREEPS_PER_WAVE, "team 1's first wave spawns a full wave once the initial delay elapses, same timer start as team 0");
}

static void test_lane_creep_marches_toward_center_when_no_target(void) {
    /* Isolated single creep, real wave timers suppressed -- avoids the real
       opposing wave's own march/clash behavior clouding this specific
       "no target in range, just advance along the path" assertion. */
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1;
    creep->alive = 1;
    creep->team = 0;
    creep->waypoint_index = 0;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = -8.0f; /* team 0's spawn line, S170-139 */
    creep->z = 0.0f;

    for (int t = 0; t < 50; t++) arena_tick_lane_creeps(16); /* 0.8s of marching, well short of reaching the center waypoint */

    CHECK(creep->x > -8.0f, "a lane creep with no target in range marches toward the enemy spawn line (team 0 marches +x)");
}

static void test_lane_creep_attacks_nearby_enemy_hero_and_does_not_advance(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1;
    creep->alive = 1;
    creep->team = 1; /* enemy of hero 0 (team 0) */
    creep->waypoint_index = 0;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.0f;
    creep->z = 0.0f;

    arena_state.heroes[0].x = 1.0f; /* within ARENA_LANE_CREEP_AGGRO_RADIUS */
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_lane_creeps(16);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_LANE_CREEP_DAMAGE,
          "a lane creep auto-attacks a hittable enemy hero within its aggro radius");
    CHECK(creep->x == 0.0f, "a lane creep that stops to fight does not advance along its waypoint path this tick");
}

static void test_lane_creeps_fight_each_other_when_opposing_teams_meet(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *a = &arena_state.lane_creeps[0];
    a->active = 1; a->alive = 1; a->team = 0; a->waypoint_index = 1;
    a->hp = a->max_hp = ARENA_LANE_CREEP_HP; a->x = -1.0f; a->z = 0.0f;

    ArenaLaneCreep *b = &arena_state.lane_creeps[1];
    b->active = 1; b->alive = 1; b->team = 1; b->waypoint_index = 1;
    b->hp = b->max_hp = ARENA_LANE_CREEP_HP; b->x = 1.0f; b->z = 0.0f;

    arena_tick_lane_creeps(16);

    CHECK(a->hp == ARENA_LANE_CREEP_HP - ARENA_LANE_CREEP_DAMAGE, "an opposing-team lane creep in aggro range takes damage from the other wave");
    CHECK(b->hp == ARENA_LANE_CREEP_HP - ARENA_LANE_CREEP_DAMAGE, "both sides of a wave clash damage each other the same tick -- the actual push mechanic");
}

static void test_hero_kills_lane_creep_in_range(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].attack_cooldown_ms = 0;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 1; /* enemy of hero 0 */
    creep->hp = ARENA_ATTACK_DAMAGE; creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.0f; creep->z = 0.0f;

    arena_hero_attack_lane_creeps(16);

    CHECK(!creep->alive, "a hero kills a lane creep within attack range");
    CHECK(!creep->active, "a dead lane creep frees its pool slot immediately, unlike jungle creeps' delayed respawn");
}

static void test_hero_does_not_attack_own_team_lane_creep(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].attack_cooldown_ms = 0;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 0; /* SAME team as hero 0 */
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.0f; creep->z = 0.0f;

    arena_hero_attack_lane_creeps(16);

    CHECK(creep->hp == ARENA_LANE_CREEP_HP, "a hero never attacks its own team's lane creep");
}

static void test_hero_does_not_attack_lane_creep_while_enemy_hero_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;

    arena_state.heroes[0].x = 0.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].attack_cooldown_ms = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; /* within ARENA_ATTACK_RANGE */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 1;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 0.3f; creep->z = 0.0f; /* also within ARENA_ATTACK_RANGE */

    arena_hero_attack_lane_creeps(16);

    CHECK(creep->hp == ARENA_LANE_CREEP_HP,
          "a hero with an enemy hero already in range does not also attack a nearby lane creep this tick");
}

static void test_lane_creep_despawns_at_final_waypoint_with_no_reward(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.lane_wave_timer_ms[0] = 999999;
    arena_state.lane_wave_timer_ms[1] = 999999;

    ArenaLaneCreep *creep = &arena_state.lane_creeps[0];
    creep->active = 1; creep->alive = 1; creep->team = 0; creep->waypoint_index = ARENA_LANE_WAYPOINT_COUNT - 1;
    creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
    creep->x = 8.0f; creep->z = 0.0f; /* team 0's final waypoint -- the enemy's spawn line, S170-139 */

    arena_tick_lane_creeps(16);

    CHECK(!creep->active, "a lane creep that reaches the final waypoint despawns -- no structure exists yet to push against");
}

static void test_lane_creep_wave_respawns_after_the_interval(void) {
    arena_init_teams();
    for (int i = 0; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Skip past the initial delay -- this test is about the steady-state
       respawn interval, not the match-opening grace period (already covered
       by test_lane_creep_wave_spawns_for_both_teams_after_initial_delay). */
    arena_state.lane_wave_timer_ms[0] = 0;
    arena_state.lane_wave_timer_ms[1] = 0;

    arena_tick_lane_creeps(16); /* first wave, both teams */
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) arena_state.lane_creeps[i].active = 0; /* as if the wave was wiped */

    arena_tick_lane_creeps(ARENA_LANE_WAVE_INTERVAL_MS); /* advance the timer past a full interval in one tick */

    int active_count = 0;
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) if (arena_state.lane_creeps[i].active) active_count++;
    CHECK(active_count == ARENA_LANE_CREEPS_PER_WAVE * 2, "a fresh wave spawns for both teams once the wave timer elapses again");
}

static void test_stealthed_hero_captures_undetected_through_a_crowd_of_visible_enemies(void) {
    /* The archetypal WoW Arathi Basin moment, brought forward on purpose:
       a stealthed capper (Frog's R, which the doc itself describes as
       "vanishes... can't be targeted or seen") solo-caps a node while a
       crowd of visible enemies stands right on top of it, none the wiser. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    for (int i = ARENA_TEAM_SIZE + 1; i < ARENA_TEAM_SIZE + 6; i++) {
        arena_state.heroes[i].active = 1;
        arena_state.heroes[i].alive = 1;
        arena_state.heroes[i].x = arena_state.nodes[0].x;
        arena_state.heroes[i].z = arena_state.nodes[0].z;
    }
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;
    arena_state.heroes[1].active = 0;

    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].intangible_ms = 5000; /* stealthed, e.g. mid-Frog's-R */

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capturing_team == 0,
          "a lone stealthed hero channels a node even with six visible enemies standing right on it");
    CHECK(arena_state.nodes[0].capture_progress_ms > 0, "the undetected channel makes real progress, not just registering as attempted");
}

static void test_two_visible_teams_still_interrupt_normally_even_near_a_stealthed_ally(void) {
    /* Guards against the stealth exception swallowing the ordinary
       mixed-presence interrupt rule: if BOTH sides have a normal, visible
       presence, it's a contest as usual regardless of a stealthed hero
       loitering nearby. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;

    arena_state.heroes[0].x = arena_state.nodes[0].x; /* visible team-0 presence */
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].x = arena_state.nodes[0].x; /* a stealthed team-0 ally, also present */
    arena_state.heroes[1].z = arena_state.nodes[0].z;
    arena_state.heroes[1].intangible_ms = 5000;

    arena_tick_nodes(1000);

    CHECK(arena_state.nodes[0].capturing_team == -1,
          "a visible enemy still interrupts normally even when a stealthed ally is also present at the node");
}

static void test_starting_a_channel_breaks_the_capturer_stealth(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = arena_state.nodes[0].x;
    arena_state.heroes[ARENA_TEAM_SIZE].z = arena_state.nodes[0].z;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].intangible_ms = 5000; /* stealthed, sneaking in past the crowd */

    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == 0, "the sneak-capture starts undetected as before");
    CHECK(arena_state.heroes[0].intangible_ms == 0,
          "interacting with the flag breaks the capturer's own stealth the instant the channel starts, real Arathi Basin's own rule");
}

static void test_damage_to_channeling_team_interrupts_the_capture(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_nodes(ARENA_NODE_CAPTURE_CHANNEL_MS / 2);
    CHECK(arena_state.nodes[0].capturing_team == 0 && arena_state.nodes[0].capture_progress_ms > 0,
          "the channel is progressing normally, undamaged");

    /* apply_damage is static to arena_game.c and not linkable from here --
       set the flag it sets directly, same as this file already sets other
       status-effect fields (silenced_ms, rooted_ms, etc.) straight on the
       struct for test setup rather than going through a cast function. */
    arena_state.heroes[0].damaged_this_tick = 1;
    arena_tick_nodes(16);

    CHECK(arena_state.nodes[0].capturing_team == -1 && arena_state.nodes[0].capture_progress_ms == 0,
          "taking damage interrupts the capture channel, same as real Arathi Basin's flag-channel pushback");
}

static void test_dead_hero_respawns_at_graveyard_when_team_owns_no_node(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    ArenaHero *h = &arena_state.heroes[0];
    h->alive = 0;
    h->hero_id = ARENA_HERO_GHOST;

    /* Before the wave arrives, still dead even though the team owns nothing. */
    arena_update_teams(ARENA_RESPAWN_WAVE_MS - 100);
    CHECK(!h->alive, "wave hasn't arrived yet, still dead");

    arena_update_teams(200); /* crosses the wave boundary */
    float gx, gz;
    arena_graveyard_position(h->team, &gx, &gz);
    CHECK(h->alive, "wave arrived -- hero respawns even though the team owns no node");
    CHECK(h->x == gx && h->z == gz, "falls back to the team's permanent graveyard");
    CHECK(h->hero_id == ARENA_HERO_GHOST, "respawning preserves which hero this slot is playing");
}

static void test_dead_hero_respawns_at_owned_node_on_wave(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.nodes[0].owner = 1; /* team 0 owns node 0 */
    arena_state.creeps[0].alive = 0; /* isolate respawn correctness from the node's own creep aggro */
    arena_state.creeps[0].respawn_ms_remaining = ARENA_RESPAWN_WAVE_MS * 10;

    ArenaHero *h = &arena_state.heroes[0];
    h->alive = 0;
    h->hero_id = ARENA_HERO_GHOST;

    arena_update_teams(ARENA_RESPAWN_WAVE_MS - 100);
    CHECK(!h->alive, "wave hasn't arrived yet, still dead");

    arena_update_teams(200);
    CHECK(h->alive, "wave arrived and the team owns a node -- hero respawns");
    CHECK(h->hp == h->max_hp, "respawns at full HP");
    CHECK(h->x == arena_state.nodes[0].x && h->z == arena_state.nodes[0].z,
          "respawns at the owned node's position");
    CHECK(h->hero_id == ARENA_HERO_GHOST, "respawning preserves which hero this slot is playing");
}

static void test_respawn_wave_brings_back_all_dead_heroes_together(void) {
    /* S170-154, founder: "respawns happen in 30 second waves" -- heroes that
       died at very different times still come back on the exact same tick,
       not staggered by their own individual death timers. */
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    ArenaHero *early = &arena_state.heroes[0];
    ArenaHero *late = &arena_state.heroes[1];
    early->alive = 1;
    late->alive = 1;

    arena_update_teams(ARENA_RESPAWN_WAVE_MS / 4);
    early->alive = 0; /* dies early in the wave cycle */

    arena_update_teams(ARENA_RESPAWN_WAVE_MS / 2);
    late->alive = 0; /* dies much later, same cycle */
    CHECK(!early->alive && !late->alive, "both still dead mid-cycle");

    /* Advance to just past the wave boundary (timer started at 0, so the
       wave lands at ARENA_RESPAWN_WAVE_MS total elapsed). */
    arena_update_teams(ARENA_RESPAWN_WAVE_MS / 4 + 100);

    CHECK(early->alive && late->alive, "both heroes respawn together on the same wave tick");
}

static void test_resource_win_condition_replaces_team_wipe(void) {
    /* S170-153: a fully wiped team no longer instantly loses -- the match
       is decided by the resource race, not by hero deaths. */
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    for (int i = 0; i < ARENA_TEAM_SIZE; i++) arena_state.heroes[i].alive = 0;

    arena_update_teams(16);
    CHECK(arena_state.winner == 0, "a full team wipe alone no longer ends the match");
}

static void test_resource_accumulates_faster_with_more_owned_nodes(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.nodes[0].owner = 1; /* team 0 owns exactly one node */

    arena_state.resources[0] = 0;
    arena_state.resources[1] = 0;

    arena_update_teams(ARENA_RESOURCE_TICK_MS);
    CHECK(arena_state.resources[0] > 0, "team 0 gains resources from its owned node");
    CHECK(arena_state.resources[1] == 0, "team 1 owns nothing -- gains nothing");

    int gain_with_one_node = arena_state.resources[0];
    arena_state.nodes[1].owner = 1; /* team 0 now owns two nodes */
    arena_state.resources[0] = 0;

    arena_update_teams(ARENA_RESOURCE_TICK_MS);
    CHECK(arena_state.resources[0] > gain_with_one_node,
          "owning more nodes yields a bigger per-tick resource gain");
}

static void test_resource_cap_wins_the_match(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;

    arena_state.resources[0] = ARENA_RESOURCE_CAP;
    arena_state.resources[1] = 0;
    arena_update_teams(16);
    CHECK(arena_state.winner == 1, "team 0 hitting the resource cap wins for team 0");

    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 0;
    arena_state.resources[1] = ARENA_RESOURCE_CAP;
    arena_update_teams(16);
    CHECK(arena_state.winner == 2, "team 1 hitting the resource cap wins for team 1");
}

static void test_sudden_death_does_not_fire_before_max_duration(void) {
    /* S170-157, founder: "i think there may be zombie games with infinite
       win cons." All nodes neutral -- zero resource gain regardless of how
       many resource ticks land inside this one big dt_ms, isolating the
       sudden-death clock from the primary resource-cap check. */
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 500;
    arena_state.resources[1] = 500;

    arena_update_teams(ARENA_MATCH_MAX_DURATION_MS - 100);
    CHECK(arena_state.winner == 0,
          "no forced winner before the sudden-death clock runs out, even with tied resources and no nodes owned");
}

static void test_sudden_death_picks_team_ahead_on_resources(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 800;
    arena_state.resources[1] = 300;
    arena_state.match_elapsed_ms = ARENA_MATCH_MAX_DURATION_MS - 8;

    arena_update_teams(16); /* small dt -- crosses the threshold without also feeding the resource-tick accumulator */
    CHECK(arena_state.winner == 1, "sudden death: team 0 is ahead on resources once the clock runs out -- team 0 wins outright");
}

static void test_sudden_death_tiebreaks_by_nodes_owned(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.nodes[0].owner = 2; /* team 1 owns exactly one node, team 0 owns none */
    arena_state.resources[0] = 500;
    arena_state.resources[1] = 500;
    arena_state.match_elapsed_ms = ARENA_MATCH_MAX_DURATION_MS - 8;

    arena_update_teams(16);
    CHECK(arena_state.winner == 2,
          "resources are exactly tied at the sudden-death clock -- falls back to nodes currently owned, team 1 has one");
}

static void test_sudden_death_full_tie_resolves_to_team_zero(void) {
    arena_init_teams();
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 0;
    arena_state.resources[0] = 0;
    arena_state.resources[1] = 0;
    arena_state.match_elapsed_ms = ARENA_MATCH_MAX_DURATION_MS - 8;

    arena_update_teams(16);
    CHECK(arena_state.winner == 1,
          "resources AND nodes owned both exactly tied -- last-resort deterministic fallback picks team 0");
}

/* S170-162/163: NORTHSTAR §17's click-to-attack system -- attack-target
 * lock, chase, and Gary's homing ranged auto-attack. Team mode only. */

static void test_attack_target_clears_on_fresh_move_command(void) {
    arena_init_teams();
    arena_set_attack_target(0, 10);
    CHECK(arena_state.heroes[0].attack_target == 10, "sanity: the lock is set");

    arena_set_move_target(0, 5.0f, 5.0f);

    CHECK(arena_state.heroes[0].attack_target == -1,
          "a fresh move command immediately clears the attack-target lock (NORTHSTAR SS17.1)");
}

static void test_attack_target_chases_out_of_range_enemy(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1; /* keep the actual target alive -- the blanket deactivation above would otherwise also deactivate it */
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].target_x = 0.0f; arena_state.heroes[0].target_z = 0.0f;
    arena_state.heroes[0].moving = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 20.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* well outside ARENA_ATTACK_RANGE */

    arena_set_attack_target(0, ARENA_TEAM_SIZE);
    arena_update_teams(16);

    CHECK(arena_state.heroes[0].moving,
          "an out-of-range attack-target lock starts the hero moving toward it");
    CHECK(arena_state.heroes[0].target_x == arena_state.heroes[ARENA_TEAM_SIZE].x
          && arena_state.heroes[0].target_z == arena_state.heroes[ARENA_TEAM_SIZE].z,
          "chase targets the enemy's actual live position, pure pursuit");
}

static void test_attack_target_re_chases_a_fleeing_target(void) {
    /* The literal "if auto attacking and a character runs away do you
       follow it" ask -- yes, automatically, every tick, no re-click. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 20.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    arena_set_attack_target(0, ARENA_TEAM_SIZE);
    arena_update_teams(16);
    float first_target_x = arena_state.heroes[0].target_x;

    /* The enemy flees further away without either side re-issuing any command. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 25.0f;
    arena_update_teams(16);

    CHECK(arena_state.heroes[0].target_x != first_target_x
          && arena_state.heroes[0].target_x == arena_state.heroes[ARENA_TEAM_SIZE].x,
          "the chase re-targets the enemy's new position every tick, following a fleeing target with no re-click");
}

static void test_attack_target_clears_when_target_dies(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 20.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    arena_set_attack_target(0, ARENA_TEAM_SIZE);

    arena_state.heroes[ARENA_TEAM_SIZE].alive = 0;
    arena_update_teams(16);

    CHECK(arena_state.heroes[0].attack_target == -1,
          "the lock clears on its own once the target dies -- no dangling reference to a dead hero");
}

static void test_attack_target_rejects_own_team(void) {
    arena_init_teams();
    arena_state.heroes[0].team = 0;
    arena_state.heroes[1].team = 0;
    arena_set_attack_target(0, 1); /* both team 0 -- never a valid attack target */

    arena_update_teams(16);

    CHECK(arena_state.heroes[0].attack_target == -1,
          "a lock onto a hero on the same team is rejected/cleared, never chased or attacked");
}

static void test_gary_fires_homing_shot_at_locked_target_in_range(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_ATTACK_RANGE - 1.0f; /* within Gary's ranged auto-attack range */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    arena_set_attack_target(0, ARENA_TEAM_SIZE);
    arena_update_teams(16);

    int found = 0;
    for (int p = 0; p < ARENA_MAX_PROJECTILES; p++) {
        if (arena_state.projectiles[p].active && arena_state.projectiles[p].homing_target == ARENA_TEAM_SIZE
            && arena_state.projectiles[p].owner == 0) {
            found = 1;
        }
    }
    CHECK(found, "Gary fires a real homing projectile at his locked target once it's in range");
}

static void test_gary_does_not_deal_flat_melee_damage(void) {
    /* Founder: "gary auto attacks are projetiles ... you cant juke them" --
       Gary's plain auto-attack is exclusively the homing shot above, never
       the flat proximity melee tick every other hero still uses. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0.0f; arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f; /* within melee range too */
    int hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;
    /* Deliberately no attack_target set -- Gary's own homing shot never
       fires without an explicit lock (see arena_tick_attack_targets), and
       he's excluded from the generic melee loop, so nothing should land
       from mere proximity alone. */

    arena_update_teams(ARENA_ATTACK_COOLDOWN_MS);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == hp_before,
          "Gary standing next to an enemy with no attack command issued deals no damage -- unlike every other hero's ambient melee tick");
}

static void test_homing_shot_hits_target_that_moves_off_the_original_line(void) {
    /* The exact opposite of how a skill-shot ArenaProjectile already
       behaves (dodgeable by stepping off the line) -- a homing shot keeps
       tracking and still connects. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) { if (i != ARENA_TEAM_SIZE) arena_state.heroes[i].active = 0; } /* isolate: no other hero can accidentally be in the shot's path */
    ArenaProjectile *shot = arena_spawn_projectile(0, 0, ARENA_HERO_GARY,
        0.0f, 0.0f, 10.0f, 0.0f, ARENA_GARY_ATTACK_SPEED, 0.6f, ARENA_GARY_ATTACK_DAMAGE, 100.0f);
    shot->homing_target = ARENA_TEAM_SIZE;
    arena_state.heroes[ARENA_TEAM_SIZE].team = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 10.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    int hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    /* The target immediately steps well off the shot's original firing line. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 10.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 8.0f;

    for (int i = 0; i < 60 && arena_state.projectiles[0].active; i++) {
        arena_tick_projectiles(16);
    }

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < hp_before,
          "a homing shot still connects even after the target juked off the original firing line -- can't be dodged by positioning");
}

static void test_homing_shot_fizzles_if_target_dies_before_arrival(void) {
    arena_init_teams();
    ArenaProjectile *shot = arena_spawn_projectile(0, 0, ARENA_HERO_GARY,
        0.0f, 0.0f, 10.0f, 0.0f, ARENA_GARY_ATTACK_SPEED, 0.6f, ARENA_GARY_ATTACK_DAMAGE, 100.0f);
    shot->homing_target = ARENA_TEAM_SIZE;
    arena_state.heroes[ARENA_TEAM_SIZE].team = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 10.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 0; /* dead before the shot ever gets there */

    arena_tick_projectiles(16);

    CHECK(!arena_state.projectiles[0].active,
          "a homing shot fizzles (no floating hit) the instant its target is no longer a valid hit");
}

static void test_paimon_q_damages_and_roots_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + 4.0f; /* within ARENA_PAIMON_Q_RANGE */
    foe->z = paimon->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->rooted_ms == ARENA_PAIMON_Q_ROOT_MS, "Q roots the foe on a landed hit");
    CHECK(paimon->q_cooldown_ms == ARENA_PAIMON_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_paimon_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + ARENA_PAIMON_Q_RANGE + 5.0f;
    foe->z = paimon->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(foe->rooted_ms == 0, "Q out of range does not root the foe");
}

static void test_paimon_w_damages_and_silences_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + 4.0f; /* within ARENA_PAIMON_W_RANGE */
    foe->z = paimon->z;
    int foe_hp_before = foe->hp;

    arena_toggle_w(1);

    CHECK(foe->hp < foe_hp_before, "Speaks With Total Authority damages the nearest enemy in range");
    CHECK(foe->silenced_ms == ARENA_PAIMON_W_SILENCE_MS, "Speaks With Total Authority silences the nearest enemy");
    CHECK(paimon->w_cooldown_ms == ARENA_PAIMON_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_paimon_passive_silences_nearest_enemy_periodically(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_PAIMON);
    ArenaHero *paimon = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = paimon->x + 2.0f; /* within ARENA_PAIMON_PASSIVE_AURA_RADIUS */
    foe->z = paimon->z;
    foe->target_x = foe->x; /* don't wander out of aura range before the tick lands */
    foe->target_z = foe->z;

    arena_update(ARENA_PAIMON_PASSIVE_INTERVAL_MS);

    CHECK(foe->silenced_ms > 0, "Keeping the Peace silences the nearest enemy in range without being cast");
}

static void test_paimon_r_zone_damages_enemy_and_heals_ally(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_PAIMON;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, inside the zone */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_PAIMON_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_PAIMON_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_PAIMON_R_HEAL_PER_TICK,
          "Two Hundred Legions heals an ally standing in the zone");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp <= 100 - ARENA_PAIMON_R_DPS,
          "Two Hundred Legions damages an enemy standing in the zone");
}

static void test_cast_flash_slot_set_on_q(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = ghost->x + 4.0f;
    foe->z = ghost->z;

    arena_cast_q(1);

    CHECK(ghost->cast_flash_slot == 1, "a successful Q cast sets cast_flash_slot to 1");
}

static void test_cast_flash_slot_set_on_w_toggle_hero(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
    ArenaHero *unicorn = &arena_state.heroes[0];

    arena_toggle_w(0);

    CHECK(unicorn->cast_flash_slot == 2, "toggling a pure-toggle W (Unicorn) sets cast_flash_slot to 2");
}

static void test_cast_flash_slot_set_on_r(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];

    arena_cast_r(1);

    CHECK(ghost->cast_flash_slot == 3, "a successful R cast sets cast_flash_slot to 3");
}

static void test_cast_flash_slot_not_set_when_q_blocked_by_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ghost->q_cooldown_ms = 1000;

    arena_cast_q(1);

    CHECK(ghost->cast_flash_slot == 0, "a Q blocked by its own cooldown does not set cast_flash_slot");
}

static void test_cast_flash_slot_not_set_when_w_blocked_by_its_own_cooldown(void) {
    /* Ghost's W is instant-with-cooldown (not a pure toggle like Unicorn's) --
       exactly the case S170-124's arena_toggle_w gating exists to handle correctly. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GHOST);
    ArenaHero *ghost = &arena_state.heroes[1];
    ghost->w_cooldown_ms = 1000;

    arena_toggle_w(1);

    CHECK(ghost->cast_flash_slot == 0, "a cooldown-gated W blocked by its own cooldown does not set cast_flash_slot");
}

static void test_noor1_q_damages_and_roots_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    ArenaHero *noor1 = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = noor1->x + 4.0f; /* within ARENA_NOOR1_Q_RANGE */
    foe->z = noor1->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->rooted_ms == ARENA_NOOR1_Q_ROOT_MS, "Q roots the foe on a landed hit");
    CHECK(noor1->q_cooldown_ms == ARENA_NOOR1_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_noor1_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    ArenaHero *noor1 = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = noor1->x + ARENA_NOOR1_Q_RANGE + 5.0f;
    foe->z = noor1->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(foe->rooted_ms == 0, "Q out of range does not root the foe");
}

static void test_noor1_w_grants_intangibility_and_cooldown(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    ArenaHero *noor1 = &arena_state.heroes[1];

    arena_toggle_w(1);

    CHECK(noor1->intangible_ms == ARENA_NOOR1_W_INTANGIBLE_MS, "Sent In Clean grants self-intangibility");
    CHECK(noor1->w_cooldown_ms == ARENA_NOOR1_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_noor1_passive_silences_nearest_enemy_periodically(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_NOOR1);
    ArenaHero *noor1 = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = noor1->x + 2.0f; /* within ARENA_NOOR1_PASSIVE_AURA_RADIUS */
    foe->z = noor1->z;
    foe->target_x = foe->x; /* don't wander out of aura range before the tick lands */
    foe->target_z = foe->z;

    arena_update(ARENA_NOOR1_PASSIVE_INTERVAL_MS);

    CHECK(foe->silenced_ms > 0, "About Four Days Behind silences the nearest enemy in range without being cast");
}

static void test_noor1_r_zone_damages_enemy_no_ally_heal(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_NOOR1;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[ARENA_TEAM_SIZE].x = -1; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* enemy, inside the zone */
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_NOOR1_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_NOOR1_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp <= 100 - ARENA_NOOR1_R_DPS,
          "Do Not Approach damages an enemy standing in the zone");
    CHECK(arena_state.heroes[1].hp == 50,
          "Do Not Approach has no ally-heal side -- an ally standing in the zone is unaffected");
}

static void test_cain_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    CHECK(arena_hero_armor(cain) == (float)ARENA_CAIN_PASSIVE_ARMOR,
          "Cain's founded city grants a flat, always-on armor bonus");
}

static void test_cain_q_executes_harder_at_low_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    ArenaHero *full_hp_foe = &arena_state.heroes[0];
    full_hp_foe->x = cain->x + 4.0f;
    full_hp_foe->z = cain->z;
    int hp_before_full = full_hp_foe->hp;
    arena_cast_q(1);
    int dmg_at_full_hp = hp_before_full - full_hp_foe->hp;

    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    cain = &arena_state.heroes[1];
    ArenaHero *low_hp_foe = &arena_state.heroes[0];
    low_hp_foe->x = cain->x + 4.0f;
    low_hp_foe->z = cain->z;
    low_hp_foe->hp = low_hp_foe->max_hp; /* keep it alive after the hit so the damage delta is measurable --
                                             the point is the execute *scaling*, not landing a kill */
    /* Simulate "near death" via max_hp rather than a tiny hp value: execute_scale_damage reads
       hp/max_hp, so a huge max_hp with the same low hp ratio gets the same scaling without risking
       the foe actually dying (which would make the post-hit hp delta unmeasurable). */
    low_hp_foe->max_hp = 1000;
    low_hp_foe->hp = 10; /* 1% HP -- near the low_hp_dmg end of the scale */
    int hp_before_low = low_hp_foe->hp;
    arena_cast_q(1);
    int dmg_at_low_hp = hp_before_low - low_hp_foe->hp;

    CHECK(dmg_at_low_hp >= dmg_at_full_hp, "The First Murder deals at least as much damage against a near-dead foe as a full-HP one");
    CHECK(cain->q_cooldown_ms == ARENA_CAIN_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_cain_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = cain->x + ARENA_CAIN_Q_RANGE + 5.0f;
    foe->z = cain->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(cain->q_cooldown_ms == 0, "Q out of range does not start its cooldown");
}

static void test_cain_w_dashes_away_from_foe_and_cleanses(void) {
    /* silenced_ms is deliberately NOT pre-set here: silence gates the entire
       cast at arena_toggle_w's own top-level check (same as every hero), so
       pre-silencing Cain to test the cleanse would just block the cast that's
       supposed to do the cleansing -- rooted_ms is the meaningful cleanse to
       verify, since roots don't block casting, only movement. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = cain->x + 2.0f;
    foe->z = cain->z;
    cain->rooted_ms = 500;
    float dist_before = fabsf(cain->x - foe->x);

    arena_toggle_w(1);

    float dist_after = fabsf(cain->x - foe->x);
    CHECK(dist_after > dist_before, "Cursed to Wander dashes Cain away from the nearest enemy, increasing distance");
    CHECK(cain->rooted_ms == 0, "Cursed to Wander cleanses Cain's own root");
    CHECK(cain->w_cooldown_ms == ARENA_CAIN_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_cain_r_arms_the_survive_floor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_CAIN);
    ArenaHero *cain = &arena_state.heroes[1];

    arena_cast_r(1);
    CHECK(cain->survive_floor_ms == ARENA_CAIN_R_FLOOR_MS, "R sets the survive floor for its duration");
    CHECK(cain->r_cooldown_ms == ARENA_CAIN_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_gunnr_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    CHECK(arena_hero_armor(gunnr) == (float)ARENA_GUNNR_PASSIVE_ARMOR,
          "Gunnr's shieldmaiden stance grants a flat, always-on armor bonus");
}

static void test_gunnr_q_damages_in_melee_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + 1.5f; /* within ARENA_GUNNR_Q_RANGE */
    foe->z = gunnr->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in melee range");
    CHECK(gunnr->q_cooldown_ms == ARENA_GUNNR_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_gunnr_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + ARENA_GUNNR_Q_RANGE + 5.0f;
    foe->z = gunnr->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of melee range does not damage the foe");
    CHECK(gunnr->q_cooldown_ms == 0, "Q out of range does not start its cooldown");
}

static void test_gunnr_w_is_a_free_toggle_regen(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    gunnr->hp = 50;

    arena_toggle_w(1);
    CHECK(gunnr->w_active, "W toggles on");
    CHECK(gunnr->w_cooldown_ms == 0, "W is a free toggle, no cooldown");

    arena_update(1000); /* one full second of regen */
    CHECK(gunnr->hp > 50, "Three More Things regenerates HP while toggled on");
}

static void test_gunnr_r_executes_harder_at_low_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *full_hp_foe = &arena_state.heroes[0];
    full_hp_foe->x = gunnr->x + 4.0f;
    full_hp_foe->z = gunnr->z;
    int hp_before_full = full_hp_foe->hp;
    arena_cast_r(1);
    int dmg_at_full_hp = hp_before_full - full_hp_foe->hp;

    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    gunnr = &arena_state.heroes[1];
    ArenaHero *low_hp_foe = &arena_state.heroes[0];
    low_hp_foe->x = gunnr->x + 4.0f;
    low_hp_foe->z = gunnr->z;
    low_hp_foe->max_hp = 1000;
    low_hp_foe->hp = 10; /* 1% HP -- near the low_hp_dmg end of the scale */
    int hp_before_low = low_hp_foe->hp;
    arena_cast_r(1);
    int dmg_at_low_hp = hp_before_low - low_hp_foe->hp;

    CHECK(dmg_at_low_hp >= dmg_at_full_hp, "Valhalla Has Yet To Admit It deals at least as much damage against a near-dead foe as a full-HP one");
    CHECK(gunnr->r_cooldown_ms == ARENA_GUNNR_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_gunnr_r_out_of_range_whiffs_but_still_starts_cooldown(void) {
    /* Gunnr's R dispatch sets the cooldown unconditionally, not gated on a helper's
       return value like Q is -- a deliberate, documented choice for this kit (the R is
       inlined directly in arena_cast_r's switch, not routed through a landed/whiff helper). */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_GUNNR);
    ArenaHero *gunnr = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = gunnr->x + ARENA_GUNNR_R_RANGE + 5.0f;
    foe->z = gunnr->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);

    CHECK(foe->hp == foe_hp_before, "R out of range does not damage the foe");
    CHECK(gunnr->r_cooldown_ms == ARENA_GUNNR_R_COOLDOWN_MS, "R still starts its cooldown even on a whiff");
}

static void test_vassago_passive_regenerates_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];
    vassago->hp = 50;

    arena_update(1000); /* one full second of passive regen */

    CHECK(vassago->hp > 50, "The passive regenerates HP every tick with no cast at all");
}

static void test_vassago_q_damages_and_silences_in_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = vassago->x + 4.0f; /* within ARENA_VASSAGO_Q_RANGE */
    foe->z = vassago->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->silenced_ms == ARENA_VASSAGO_Q_SILENCE_MS, "Reveal the Gentle Maybe silences the foe on a landed hit");
    CHECK(vassago->q_cooldown_ms == ARENA_VASSAGO_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_vassago_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = vassago->x + ARENA_VASSAGO_Q_RANGE + 5.0f;
    foe->z = vassago->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(foe->silenced_ms == 0, "Q out of range does not silence the foe");
}

static void test_vassago_w_grants_ally_next_cast_refund(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_VASSAGO;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally */

    arena_toggle_w(0);

    CHECK(arena_state.heroes[1].next_cast_refund == 1, "The Soft Foresight grants the nearest ally next_cast_refund");
    CHECK(arena_state.heroes[0].w_cooldown_ms == ARENA_VASSAGO_W_COOLDOWN_MS, "W starts on its own cooldown after cast");
}

static void test_vassago_w_no_ally_in_1v1_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_VASSAGO);
    ArenaHero *vassago = &arena_state.heroes[1];

    arena_toggle_w(1);

    CHECK(vassago->w_cooldown_ms == 0, "no ally in 1v1 means the cast whiffs -- cooldown is not consumed");
}

static void test_vassago_r_zone_silences_but_deals_no_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_VASSAGO;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    /* 3.0 units away: inside ARENA_VASSAGO_R_RADIUS (4.5) but outside melee auto-attack
       range (~1.6) -- close enough for x=1 would let the two heroes auto-attack each other
       for ordinary melee damage in the same tick, which isn't what this test measures. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_VASSAGO_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_VASSAGO_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000); /* one full 1000ms zone tick */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].silenced_ms > 0, "The Gentle Maybe silences an enemy standing in the zone");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100, "The Gentle Maybe deals no damage at all -- pure control, not a hit");
}

static void test_he_xiangu_passive_regenerates_hp(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    he_xiangu->hp = 50;

    arena_update(1000);

    CHECK(he_xiangu->hp > 50, "The passive regenerates HP every tick with no cast at all");
}

static void test_he_xiangu_q_damages_foe_and_heals_self(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = he_xiangu->x + 4.0f; /* within ARENA_HE_XIANGU_Q_RANGE */
    foe->z = he_xiangu->z;
    he_xiangu->hp = 50;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(he_xiangu->hp > 50, "Subsisting on Mother-of-Pearl and Moonlight heals her for a fraction of the damage dealt");
    CHECK(he_xiangu->q_cooldown_ms == ARENA_HE_XIANGU_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_he_xiangu_q_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = he_xiangu->x + ARENA_HE_XIANGU_Q_RANGE + 5.0f;
    foe->z = he_xiangu->z;
    he_xiangu->hp = 50;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp == foe_hp_before, "Q out of range does not damage the foe");
    CHECK(he_xiangu->hp == 50, "Q out of range does not heal her either -- it whiffed, not cast");
}

static void test_he_xiangu_w_is_a_free_toggle_regen(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_HE_XIANGU);
    ArenaHero *he_xiangu = &arena_state.heroes[1];
    he_xiangu->hp = 50;

    arena_toggle_w(1);
    CHECK(he_xiangu->w_active, "W toggles on");
    CHECK(he_xiangu->w_cooldown_ms == 0, "W is a free toggle, no cooldown");

    arena_update(1000);
    CHECK(he_xiangu->hp > 52, "Self-Denial regenerates HP on top of the base passive while toggled on");
}

static void test_he_xiangu_r_zone_heals_ally_no_enemy_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_HE_XIANGU;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* ally, inside the zone */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    /* 3.0 units away: inside ARENA_HE_XIANGU_R_RADIUS but outside melee auto-attack range,
       same reasoning as Vassago's equivalent test -- isolates the zone's own heal-only
       property from ordinary melee combat between the two heroes. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;

    arena_cast_r(0);
    CHECK(arena_state.heroes[0].r_active_ms == ARENA_HE_XIANGU_R_DURATION_MS, "R starts its zone duration on cast");
    CHECK(arena_state.heroes[0].r_cooldown_ms == ARENA_HE_XIANGU_R_COOLDOWN_MS, "R starts on its own cooldown after cast");

    arena_update_teams(1000);

    CHECK(arena_state.heroes[1].hp == 50 + ARENA_HE_XIANGU_R_HEAL_PER_TICK,
          "Never Once Framed It As Sacrifice heals an ally standing in the zone");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == 100,
          "the zone deals no damage at all -- pure support, not a hit");
}

static void test_beleth_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    CHECK(arena_hero_armor(beleth) == (float)ARENA_BELETH_PASSIVE_ARMOR,
          "Beleth's own survival grants a flat, always-on armor bonus");
}

static void test_beleth_q_damages_and_burns_foe(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + 4.0f; /* within ARENA_BELETH_Q_RANGE */
    foe->z = beleth->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in range");
    CHECK(foe->burning_ms == ARENA_BELETH_Q_BURN_MS, "Q applies the burn DoT");
    CHECK(foe->burn_dps == ARENA_BELETH_Q_BURN_DPS, "the burn ticks at Beleth's own Q burn rate");
    CHECK(beleth->q_cooldown_ms == ARENA_BELETH_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_beleth_w_silences_no_damage(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + 4.0f; /* within ARENA_BELETH_W_RANGE */
    foe->z = beleth->z;
    int foe_hp_before = foe->hp;

    arena_toggle_w(1);

    CHECK(foe->silenced_ms == ARENA_BELETH_W_SILENCE_MS, "W silences the nearest enemy");
    CHECK(foe->hp == foe_hp_before, "W is escalation-denial only -- it deals no damage at all");
    CHECK(beleth->w_cooldown_ms == ARENA_BELETH_W_COOLDOWN_MS, "W starts on its own cooldown after a landed decree");
}

static void test_beleth_r_marks_zone_no_immediate_damage(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + 3.0f; /* within ARENA_BELETH_R_RANGE, and later inside R_RADIUS too */
    foe->z = beleth->z;
    int foe_hp_before = foe->hp;

    arena_cast_r(1);

    CHECK(beleth->r_active_ms == ARENA_BELETH_R_FUSE_MS, "R starts the fuse on cast");
    CHECK(beleth->r_cooldown_ms == ARENA_BELETH_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
    CHECK(foe->hp == foe_hp_before, "the detonation hasn't happened yet -- no damage at cast time, only a mark");
}

static void test_beleth_r_detonates_after_fuse(void) {
    /* Team mode, not arena_init_with_heroes/arena_update: the 1v1 local-demo path's
       arena_update runs an autonomous chase-bot on owner 1 (arena_bot_enabled defaults on)
       that would close the distance to melee range well within the fuse's 1.8s window at
       ARENA_HERO_SPEED, contaminating the burst-damage check with an extra melee trade --
       found via this exact test failing. Team mode's arena_update_teams has no such chase
       AI (update_hero_motion only moves a hero toward an explicitly-set target), same
       reasoning as Vassago's/He Xiangu's own R tests using team mode for their zone checks. */
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) {
        if (i == ARENA_TEAM_SIZE) continue;
        arena_state.heroes[i].active = 0;
    }
    arena_state.heroes[0].hero_id = ARENA_HERO_BELETH;
    /* z=15: off every node's aggro/capture footprint (the Blacksmith node sits at (0,0), same
       real bug this session already hit once for a different hero's test -- a jungle creep
       spawning on the node dealt real damage the strict-equality check misattributed to the
       ability itself). heroes[ARENA_TEAM_SIZE]: the enemy team, same convention as Vassago's/
       He Xiangu's own team-mode R tests (heroes[0]/[1] are the SAME team by default). 3.0
       units: inside ARENA_BELETH_R_RADIUS but outside melee auto-attack range, isolating the
       zone's own effect from ordinary melee. */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 15.0f;
    /* arena_init_teams() leaves every hero_id at its own ARENA_HERO_UNICORN placeholder
       ("until the real client's draft pick overrides it", per its own comment) -- Unicorn
       carries a flat +4 armor passive, which would silently eat 4 of this test's exact
       damage figure. Duck has no passive at all, so the burst lands un-mitigated. */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 3.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 15.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 100;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_r(0);
    arena_update_teams(ARENA_BELETH_R_FUSE_MS); /* one big tick, past the whole fuse */

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before - ARENA_BELETH_R_DAMAGE,
          "the fuse hitting zero deals ONE full, un-mitigated burst to whoever's still in the zone");
    CHECK(arena_state.heroes[0].r_active_ms == 0, "the fuse doesn't go negative or wrap, it pins at zero");
}

static void test_beleth_r_out_of_range_whiffs(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_BELETH);
    ArenaHero *beleth = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = beleth->x + ARENA_BELETH_R_RANGE + 5.0f;
    foe->z = beleth->z;

    arena_cast_r(1);

    CHECK(beleth->r_active_ms == 0, "R out of range doesn't start the fuse -- it whiffed, not cast");
    CHECK(beleth->r_cooldown_ms == 0, "a whiffed R doesn't consume the cooldown either");
}

static void test_mnm_passive_grants_flat_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];
    CHECK(arena_hero_armor(mnm) == (float)ARENA_MNM_PASSIVE_ARMOR,
          "MnM's shell grants a flat, always-on armor bonus even with W off");
}

static void test_mnm_w_toggle_stacks_on_top_of_passive_armor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];

    arena_toggle_w(1);

    CHECK(mnm->w_active == 1, "W toggles on");
    CHECK(arena_hero_armor(mnm) == (float)(ARENA_MNM_PASSIVE_ARMOR + ARENA_MNM_W_ARMOR_BONUS),
          "the toggle bonus stacks on top of the passive, not replaces it");

    arena_toggle_w(1);
    CHECK(mnm->w_active == 0, "W toggles back off");
    CHECK(arena_hero_armor(mnm) == (float)ARENA_MNM_PASSIVE_ARMOR,
          "toggling off drops back to just the passive base");
}

static void test_mnm_q_damages_and_roots_in_melee_range(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    foe->x = mnm->x + 1.5f; /* within ARENA_MNM_Q_RANGE */
    foe->z = mnm->z;
    int foe_hp_before = foe->hp;

    arena_cast_q(1);

    CHECK(foe->hp < foe_hp_before, "Q damages the foe when in melee range");
    CHECK(foe->rooted_ms == ARENA_MNM_Q_ROOT_MS, "Q roots the foe on a landed hit");
    CHECK(mnm->q_cooldown_ms == ARENA_MNM_Q_COOLDOWN_MS, "Q starts on cooldown after a landed hit");
}

static void test_mnm_r_roots_self_and_grants_survive_floor(void) {
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_MNM);
    ArenaHero *mnm = &arena_state.heroes[1];

    arena_cast_r(1);

    CHECK(mnm->rooted_ms == ARENA_MNM_R_ROOT_MS, "R roots MnM in place");
    CHECK(mnm->survive_floor_ms == ARENA_MNM_R_SURVIVE_FLOOR_MS, "R grants the guaranteed-survival window");
    CHECK(mnm->r_cooldown_ms == ARENA_MNM_R_COOLDOWN_MS, "R starts on its own cooldown after cast");
}

static void test_mnm_r_survive_floor_actually_blocks_lethal_damage(void) {
    /* Same real-ability-not-fake-damage pattern as test_pizza_r_prevents_death_for_duration:
       a real Duck Q (Telekinetic Yank), not a synthetic damage injection. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[1].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_MNM;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[0].hp = 1; arena_state.heroes[0].max_hp = 100; /* one hit from death */
    arena_state.heroes[ARENA_TEAM_SIZE].hero_id = ARENA_HERO_DUCK;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_DUCK_Q_RANGE - 1.0f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_r(0); /* Absorbing Hits Meant For Somebody Else */
    arena_cast_q(ARENA_TEAM_SIZE); /* Duck's Telekinetic Yank, would normally kill a 1-HP target */

    CHECK(arena_state.heroes[0].hp == 1, "the shell absorbs it -- HP floors at 1 against lethal damage");
    CHECK(arena_state.heroes[0].alive, "MnM survives a hit that would kill anyone else on the roster outright");
}

/* S170-136: Gary's Q is now a real travelling projectile, not an instant
 * hit -- these tests exercise the whole shape: cast spawns a projectile
 * (no immediate damage), the projectile travels and lands after enough
 * ticks, a target that steps off the line before it arrives genuinely
 * dodges it, and an unhit shot despawns cleanly once it exceeds its range
 * rather than lingering forever. */

static void test_gary_q_cast_spawns_projectile_no_instant_damage(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before,
          "casting Q does not deal instant damage -- it fires a projectile instead");
    ArenaProjectile *p = find_active_projectile();
    CHECK(p != NULL, "a projectile is actually spawned on cast");
    CHECK(arena_state.heroes[0].q_cooldown_ms == ARENA_GARY_Q_COOLDOWN_MS,
          "cooldown is spent on cast, same as every other ability, regardless of the shot's eventual outcome");
}

static void test_gary_q_out_of_range_whiffs_no_projectile(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE + 5.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);

    CHECK(find_active_projectile() == NULL, "no projectile spawns when no foe is in range at cast time");
    CHECK(arena_state.heroes[0].q_cooldown_ms == 0, "an out-of-range whiff doesn't consume the cooldown, same convention as every other Q");
}

static void test_gary_q_projectile_lands_after_travel_time(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "no damage the instant the shot is fired");

    /* Foe stays put -- the shot should reach it well within one full second
       of flight given ARENA_GARY_Q_PROJECTILE_SPEED. */
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before, "a stationary target is hit once the projectile travels far enough to reach it");
    CHECK(find_active_projectile() == NULL, "the projectile deactivates on hit, doesn't linger");
}

static void test_gary_q_projectile_misses_if_target_steps_off_line(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    ArenaProjectile *p = find_active_projectile();
    CHECK(p != NULL, "projectile spawned");

    /* Real dodge: step far off the original firing line before the shot
       arrives, well clear of the projectile's hit radius. */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f;
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before,
          "a target that steps off the firing line before the shot arrives takes no damage -- a real dodge, not homing");
}

static void test_gary_q_projectile_despawns_after_max_range_unhit(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_GARY;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_GARY_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;

    arena_cast_q(0);
    /* Move the foe out of the way immediately, then run well past the time
       the shot would need to exceed its own max_range. */
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f;
    for (int i = 0; i < 200; i++) arena_tick_projectiles(16);

    CHECK(find_active_projectile() == NULL, "an unhit projectile despawns once it exceeds its max range, doesn't travel forever");
}

/* S170-140: Tyler's Q (Earthbind) converted from an instant hit to a real
 * projectile, carrying both root and burn as on-hit effects. */
static void test_tyler_q_cast_spawns_projectile_no_instant_effect(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TYLER_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "casting Q does not deal instant damage -- it fires a projectile instead");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == 0, "no instant root either");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == 0, "no instant burn either");
    CHECK(find_active_projectile() != NULL, "a projectile is actually spawned on cast");
    CHECK(arena_state.heroes[0].q_cooldown_ms == ARENA_TYLER_Q_COOLDOWN_MS, "cooldown is spent on cast");
}

static void test_tyler_q_projectile_roots_and_burns_on_hit(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TYLER_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before, "a stationary target is hit once the net reaches it");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == ARENA_TYLER_Q_ROOT_MS, "Earthbind roots the foe on a landed hit");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == ARENA_TYLER_Q_BURN_MS, "...and applies the burn DoT too, both carried by the same projectile");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burn_dps == ARENA_TYLER_Q_BURN_DPS, "burn DoT rate matches Tyler's own Q burn rate");
}

static void test_tyler_q_projectile_misses_if_target_steps_off_line(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = ARENA_TYLER_Q_RANGE - 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    arena_cast_q(0);
    arena_state.heroes[ARENA_TEAM_SIZE].z = 10.0f;
    for (int i = 0; i < 100; i++) arena_tick_projectiles(16);

    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp == foe_hp_before, "a target that steps off the net's line takes no damage");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].rooted_ms == 0, "...and isn't rooted");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].burning_ms == 0, "...or burned -- a real dodge, not homing");
}

/* S170-141: Tyler's puppet clones ("true Meepo parity"). See
 * docs/HEROES_VS0.md's Tyler section for the full design/scope note. */
static void test_tyler_r_spawns_clones_linked_to_caster(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 5.0f; arena_state.heroes[0].z = 2.0f;

    arena_cast_r(0);

    int clone_count = 0;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *c = &arena_state.heroes[i];
        if (!c->active) continue;
        clone_count++;
        CHECK(c->is_clone == 1, "each spawned puppet is marked is_clone");
        CHECK(c->clone_owner == 0, "each spawned puppet links back to Tyler's own owner slot");
        CHECK(c->team == arena_state.heroes[0].team, "a clone shares Tyler's team");
        CHECK(c->hp == (int)(arena_state.heroes[0].max_hp * ARENA_TYLER_CLONE_HP_PCT),
              "a clone spawns with the documented fraction of Tyler's max HP");
    }
    CHECK(clone_count == ARENA_TYLER_R_CLONE_COUNT, "R spawns the documented number of clones");
}

static void test_tyler_clones_mirror_move_target_and_fight(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;

    arena_cast_r(0);
    arena_set_move_target(0, 20.0f, 0.0f);

    /* Place a lone enemy exactly where the clone army is marching through,
       far from Tyler himself, so only a clone (not the real Tyler) can be
       the one that actually lands the hit -- proves clones fight through
       the same generic combat loop, not a special-cased one. */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.5f;
    int foe_hp_before = arena_state.heroes[ARENA_TEAM_SIZE].hp;

    for (int i = 0; i < 30; i++) arena_update_teams(16);

    int any_clone_moved = 0;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *c = &arena_state.heroes[i];
        if (c->active && c->x > 0.5f) any_clone_moved = 1;
    }
    CHECK(any_clone_moved, "clones mirror Tyler's own move-click and actually advance");
    CHECK(arena_state.heroes[ARENA_TEAM_SIZE].hp < foe_hp_before,
          "an enemy near the marching clone army takes real damage -- clones fight through the generic melee loop");
}

/* apply_damage/arena_tick_respawns are static to arena_game.c -- the three
 * tests below drive real kills entirely through the public arena_update_teams
 * loop (a lone, heavily-buffed enemy hero parked in melee range), the same
 * "run real ticks until it happens" style already used elsewhere in this
 * suite (e.g. test_dead_hero_respawns_at_owned_node_once_timer_expires). */

static void test_tyler_shared_fate_clone_death_kills_tyler_and_siblings(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 500; /* outlasts the clone's own counter-attacks */
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;

    arena_cast_r(0);
    ArenaHero *clone = &arena_state.heroes[ARENA_MAX_HEROES];
    CHECK(clone->active && clone->alive, "sanity: the first clone slot is alive before combat");
    ArenaHero *sibling = &arena_state.heroes[ARENA_MAX_HEROES + 1];
    /* Separate the clone from Tyler and park the sibling clone far out of
       reach, so the lone enemy below can only ever fight the ONE clone --
       isolates this to "a clone's own death cascades," not a mixed brawl. */
    clone->x = 5.0f; clone->z = 0.0f;
    sibling->x = -50.0f; sibling->z = -50.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 5.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    for (int i = 0; i < 500 && arena_state.heroes[0].alive; i++) arena_update_teams(16);

    CHECK(!clone->alive, "the clone that was actually hit dies");
    CHECK(!arena_state.heroes[0].alive, "the real Tyler dies too -- literal OG shared fate, no exceptions");
    CHECK(!sibling->alive, "every other linked clone dies in the same cascade");
    CHECK(!sibling->active, "a dead clone's slot frees immediately, no respawn queue");
}

static void test_tyler_death_kills_his_clones_too(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].hp = arena_state.heroes[ARENA_TEAM_SIZE].max_hp = 500;
    arena_state.heroes[0].hero_id = ARENA_HERO_TYLER;
    arena_state.heroes[0].max_hp = arena_state.heroes[0].hp = 40; /* converges quickly under real combat */
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;

    arena_cast_r(0);
    /* Park both clones far away -- isolates this to Tyler's OWN death
       triggering the cascade, not a clone dying alongside him in the same
       fight. */
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        arena_state.heroes[i].x = -50.0f;
        arena_state.heroes[i].z = -50.0f;
    }
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.0f;
    arena_state.heroes[ARENA_TEAM_SIZE].z = 0.0f;

    for (int i = 0; i < 500 && arena_state.heroes[0].alive; i++) arena_update_teams(16);

    CHECK(!arena_state.heroes[0].alive, "Tyler himself dies from real combat");
    ArenaHero *clone = &arena_state.heroes[ARENA_MAX_HEROES];
    CHECK(!clone->alive && !clone->active, "Tyler dying kills his clones too, same shared-fate link in the other direction");
}

/* S170-143: hover casting (WoW-macro-style mouseover targeting), starting with Doc Wheel. */

static void test_hover_ally_or_nearest_falls_back_when_nothing_hovered(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally */
    arena_state.heroes[2].x = 10; arena_state.heroes[2].z = 0; /* farther ally */

    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[1], "with no hover target set (-1 default after init), falls back to the nearest ally exactly as before");
}

static void test_hover_ally_or_nearest_prefers_hover_target_over_nearest(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally -- should be skipped */
    arena_state.heroes[2].x = 10; arena_state.heroes[2].z = 0; /* the hovered, farther ally */

    arena_set_hover_target(0, 2);
    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[2], "a real WoW-macro mouseover target wins over nearest-ally targeting, even when farther away");
}

static void test_hover_ally_or_nearest_falls_back_for_enemy_target(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally */
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0; /* hovered, but an ENEMY */

    arena_set_hover_target(0, ARENA_TEAM_SIZE);
    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[1], "hovering an enemy hero never redirects an ally-heal onto them -- falls back to nearest ally");
}

static void test_hover_ally_or_nearest_falls_back_for_dead_target(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest, living ally */
    arena_state.heroes[2].x = 2; arena_state.heroes[2].z = 0;
    arena_state.heroes[2].alive = 0; /* hovered, but dead */

    arena_set_hover_target(0, 2);
    ArenaHero *result = arena_hover_ally_or_nearest(0);

    CHECK(result == &arena_state.heroes[1], "hovering a dead ally falls back to nearest ally rather than returning the corpse");
}

static void test_set_hover_target_out_of_range_owner_is_a_safe_noop(void) {
    arena_init_teams();
    arena_set_hover_target(-1, 0); /* must not crash or write out of bounds */
    arena_set_hover_target(ARENA_MAX_HEROES, 0);
    CHECK(1, "arena_set_hover_target with an out-of-range owner does not crash");
}

static void test_doc_wheel_q_heals_hover_target_over_nearest_ally(void) {
    arena_init_teams();
    for (int i = 3; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].hero_id = ARENA_HERO_DOC_WHEEL;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[1].x = 1; arena_state.heroes[1].z = 0; /* nearest ally -- should NOT be healed */
    arena_state.heroes[1].max_hp = 100; arena_state.heroes[1].hp = 50;
    arena_state.heroes[2].x = 10; arena_state.heroes[2].z = 0; /* the mouseover-hovered ally */
    arena_state.heroes[2].max_hp = 100; arena_state.heroes[2].hp = 50;

    arena_set_hover_target(0, 2);
    arena_cast_q(0);

    CHECK(arena_state.heroes[2].hp > 50, "Bedside Manner heals the hovered ally, a real WoW-style mouseover heal");
    CHECK(arena_state.heroes[1].hp == 50, "...not the nearer, un-hovered ally -- the whole point of hover casting");
}

/* S170-147: healing fountains, 2 corners, neutral (heals any team). */

static void test_fountain_heals_hero_in_radius(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;

    arena_tick_fountains(1000); /* one full heal tick */

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_FOUNTAIN_HEAL_PER_SEC,
          "a hero standing at a fountain's position heals for one tick's worth");
}

static void test_fountain_does_not_heal_hero_outside_radius(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx + ARENA_FOUNTAIN_RADIUS + 5.0f; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 50, "a hero well outside the fountain's radius is not healed");
}

static void test_fountain_heals_either_team_neutral(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].team = 1; /* team 1 hero at team 0's own default index -- fountains don't care */
    float fx, fz;
    arena_fountain_position(1, &fx, &fz); /* the OTHER fountain, still neutral */
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 50;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 50 + ARENA_FOUNTAIN_HEAL_PER_SEC,
          "fountains heal any team, a genuinely neutral contestable resource");
}

static void test_fountain_caps_healing_at_max_hp(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 100 - 1;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 100, "fountain healing caps at max_hp, doesn't overheal");
}

static void test_fountain_does_not_heal_dead_hero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_hp = 100; arena_state.heroes[0].hp = 0;
    arena_state.heroes[0].alive = 0;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].hp == 0, "a dead hero standing at a fountain's position is not healed");
}

static void test_fountain_restores_mana(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX; arena_state.heroes[0].mp = 10;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].mp == 10 + ARENA_FOUNTAIN_MANA_PER_SEC,
          "founder: 'fountains should also restore mana' -- one tick's worth restored");
}

static void test_fountain_mana_restore_caps_at_max(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    float fx, fz;
    arena_fountain_position(0, &fx, &fz);
    arena_state.heroes[0].x = fx; arena_state.heroes[0].z = fz;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX; arena_state.heroes[0].mp = ARENA_MP_MAX - 1;

    arena_tick_fountains(1000);

    CHECK(arena_state.heroes[0].mp == ARENA_MP_MAX, "fountain mana restore caps at max_mp, doesn't overfill");
}

/* S170-148: mana visibility + combat-gated regen. */

static void test_mana_regenerates_out_of_combat(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = 0; /* out of combat */

    arena_update_teams(1000);

    CHECK(arena_state.heroes[0].mp > 0, "mana regenerates normally once combat_timer_ms has expired");
}

static void test_mana_regenerates_slowly_in_combat(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = ARENA_COMBAT_TIMEOUT_MS; /* just took damage */

    arena_update_teams(1000); /* combat_timer_ms ticks down but stays > 0 the whole second */

    CHECK(arena_state.heroes[0].mp == ARENA_MP_REGEN_IN_COMBAT_PER_SEC,
          "founder: 'have mana tic up slowly 1 per second always' -- a slow trickle even mid-fight, not a dead stop");
}

static void test_mana_regenerates_faster_out_of_combat_than_in_combat(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = ARENA_COMBAT_TIMEOUT_MS;
    arena_state.heroes[1].max_mp = ARENA_MP_MAX;
    arena_state.heroes[1].mp = 0;
    arena_state.heroes[1].combat_timer_ms = 0;

    arena_update_teams(1000);

    CHECK(arena_state.heroes[0].mp == ARENA_MP_REGEN_IN_COMBAT_PER_SEC, "in-combat hero regens at the slow trickle rate");
    CHECK(arena_state.heroes[1].mp == ARENA_MP_REGEN_PER_SEC, "out-of-combat hero regens at the full rate");
    CHECK(arena_state.heroes[1].mp > arena_state.heroes[0].mp, "out-of-combat regen is genuinely faster than the in-combat trickle");
}

static void test_mana_regen_accumulates_correctly_across_many_small_ticks(void) {
    /* S170-150 bugfix: this is the actual production tick shape
       (arena_server always calls with dt_ms=16) -- (int)(6 * 16 / 1000.0)
       truncates to 0 on every single call without a persistent fractional
       accumulator, so a naive per-tick cast would silently never regen
       mana at all in real gameplay. 63 ticks of 16ms = 1008ms, just over a
       full second. */
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* Keep one living hero on team 1 (deactivating ALL of it would instantly
       trigger a real team-wipe win condition on the first tick -- team 1
       alive-count 0 and owning no node -- which then freezes every
       subsequent arena_update_teams() call in the loop below for the rest
       of the test, a real gotcha this session already hit once before in
       bot-mode testing with an undersized lobby). */
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 1000.0f; /* far away -- not a combat participant, just present */
    arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].mp = 0;
    arena_state.heroes[0].combat_timer_ms = 0;

    for (int i = 0; i < 63; i++) arena_update_teams(16);

    CHECK(arena_state.heroes[0].mp >= ARENA_MP_REGEN_PER_SEC,
          "mana actually regenerates over many real-sized (16ms) ticks, not just in single large-dt_ms test steps");
}

static void test_taking_damage_rearms_the_combat_timer(void) {
    arena_init_teams();
    for (int i = 2; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].active = 1;
    arena_state.heroes[ARENA_TEAM_SIZE].alive = 1;
    arena_state.heroes[0].combat_timer_ms = 0;
    arena_state.heroes[0].x = 0; arena_state.heroes[0].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].x = 0.5f; arena_state.heroes[ARENA_TEAM_SIZE].z = 0;
    arena_state.heroes[ARENA_TEAM_SIZE].attack_cooldown_ms = 0;

    arena_update_teams(16); /* one tick -- enough for the enemy's melee auto-attack to land */

    CHECK(arena_state.heroes[0].combat_timer_ms > 0, "taking damage re-arms the combat timer, gating mana regen again");
}

static void test_combat_timer_counts_down_to_zero(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.heroes[0].combat_timer_ms = 500;

    arena_update_teams(1000); /* more than enough to fully expire it */

    CHECK(arena_state.heroes[0].combat_timer_ms == 0, "the combat timer counts down and pins at 0, doesn't go negative");
}

/* S170-152: "capturing node should not make the user take damage" -- a team-flavored jungle
 * creep no longer attacks its own owning team, only the opposing one. */

static void test_team_creep_does_not_attack_own_owning_team(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    /* S170-161: team 0 owns everything -- its creep has nowhere to march,
       staying at its graveyard spawn for the whole test. */
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* creep flavor becomes TEAM0 */
    arena_state.heroes[0].team = 0;
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn */
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS); /* long enough for one attack, if it were going to land */

    CHECK(arena_state.heroes[0].hp == 100,
          "a team-flavored creep does not attack a hero of its own owning team standing at its graveyard spawn");
}

static void test_team_creep_still_attacks_opposing_team(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) arena_state.nodes[n].owner = 1; /* team 0 owns everything -- creep has nowhere to march */
    arena_state.heroes[0].team = 1; /* the enemy, trying to flip it */
    float gx, gz;
    arena_graveyard_position(0, &gx, &gz);
    arena_state.heroes[0].x = gx;
    arena_state.heroes[0].z = gz;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn -- all 5 nodes are team 0's, so all 5 creeps spawn at the same graveyard point */
    /* Unlike arena_hero_attack_creeps (one hero-initiated hit per tick,
       first in-range creep wins via its own `break`), arena_tick_creeps'
       creep-initiated attack loop has no such per-tick cap -- every alive
       creep independently checks and can attack. With all 5 creeps
       stacked on the same graveyard tile this tick, the hero would take
       5x the intended single-creep hit. Isolate the one creep this test
       actually cares about by killing the other 4 off before the attack
       tick -- same "reduce the moving parts to what's actually being
       tested" convention this file already uses elsewhere. */
    for (int i = 1; i < ARENA_MAX_CREEPS; i++) { arena_state.creeps[i].alive = 0; arena_state.creeps[i].respawn_ms_remaining = ARENA_CREEP_TEAM_RESPAWN_MS * 10; }
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CREEP_TEAM_DAMAGE,
          "a team-flavored creep still attacks the OPPOSING team -- the real counter-play, unchanged");
}

static void test_neutral_creep_still_attacks_anyone(void) {
    arena_init_teams();
    for (int i = 1; i < ARENA_MAX_HEROES; i++) arena_state.heroes[i].active = 0;
    arena_state.nodes[0].owner = 0; /* neutral/contested -- creep flavor stays NEUTRAL */
    arena_state.heroes[0].team = 0;
    arena_state.heroes[0].x = arena_state.nodes[0].x;
    arena_state.heroes[0].z = arena_state.nodes[0].z;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;

    arena_tick_creeps(16); /* spawn */
    arena_tick_creeps(ARENA_CREEP_ATTACK_COOLDOWN_MS);

    CHECK(arena_state.heroes[0].hp == 100 - ARENA_CREEP_NEUTRAL_DAMAGE,
          "a NEUTRAL/contested creep still attacks anyone regardless of team -- the real 'fight through the prize' challenge, unchanged");
}

int main(void) {
    printf("RED GARDEN arena_game headless smoke test\n\n");
    test_movement_reaches_target();
    test_bounds_clamped();
    test_combat_and_win_condition();
    test_bot_steers_toward_player();
    test_click_near_enemy_becomes_attack_move();
    test_unicorn_q_dashes_and_damages();
    test_unicorn_q_respects_cooldown();
    test_unicorn_w_regen_toggle();
    test_mp_starts_full();
    test_mp_regenerates_over_time();
    test_mp_deducted_on_landed_q_cast();
    test_mp_blocks_cast_when_insufficient();
    test_mp_toggle_w_charges_on_activate_free_on_deactivate();
    test_mp_toggle_w_blocked_when_insufficient();
    test_unicorn_r_doubles_armor_temporarily();
    test_unicorn_armor_reduces_incoming_damage();
    test_duck_q_pulls_foe_and_damages();
    test_duck_q_out_of_range_whiffs();
    test_duck_q_never_pulls_past_the_duck();
    test_duck_r_bigger_pull_and_damage_than_q();
    test_duck_has_no_w();
    test_hero_dispatch_is_by_hero_not_owner_slot();
    test_ghost_q_cast_spawns_projectile_no_instant_effect();
    test_ghost_q_out_of_range_whiffs_no_projectile();
    test_ghost_q_projectile_damages_and_silences_on_hit();
    test_ghost_q_projectile_misses_and_no_silence_if_target_steps_off_line();
    test_silenced_hero_cannot_cast();
    test_ghost_w_grants_intangibility_and_expires();
    test_intangible_hero_cannot_be_hit();
    test_ghost_r_zone_damages_enemy_jungle_creep();
    test_ghost_r_zone_does_not_damage_own_team_jungle_creep();
    test_ghost_r_zone_damages_enemy_lane_creep();
    test_pizza_aura_damages_enemy_jungle_creep();
    test_ghost_r_zone_damages_foe_over_time();
    test_ghost_r_zone_stays_fixed_when_foe_moves_away();
    test_frog_q_rewinds_position_and_hp();
    test_frog_q_uses_oldest_available_history_before_3s_elapsed();
    test_frog_r_vanishes();
    test_frog_w_noop_in_1v1_no_ally();
    test_arena_bot_enabled_gates_kit_casts_too();
    test_arena_init_teams_sets_up_both_sides();
    test_nearest_enemy_finds_closest_on_other_team();
    test_nearest_enemy_ignores_teammates_and_dead_heroes();
    test_team_melee_converges_multiple_attackers_on_one_target();
    test_team_wipe_alone_does_not_win_the_match();
    test_nearest_ally_finds_closest_teammate();
    test_nearest_ally_ignores_enemies_and_dead_teammates();
    test_nearest_ally_never_returns_self();
    test_nearest_ally_null_in_1v1();
    test_ghost_r_zone_heals_ally_in_team_mode();
    test_ghost_r_zone_does_not_heal_ally_outside_radius();
    test_frog_w_refunds_ally_next_cast_cooldown();
    test_frog_w_whiffs_with_no_ally_cooldown_not_consumed();
    test_doc_wheel_q_heals_more_at_lower_hp();
    test_doc_wheel_q_cleanses_silence();
    test_doc_wheel_q_whiffs_with_no_ally_cooldown_not_consumed();
    test_doc_wheel_w_teleports_to_ally();
    test_doc_wheel_r_heals_allies_in_radius_only();
    test_doc_wheel_r_consumes_cooldown_even_with_zero_allies();
    test_node_channel_starts_and_flips_node_neutral_immediately();
    test_node_channel_completes_to_capturing_team();
    test_node_channel_interrupted_by_mixed_presence_loses_all_progress();
    test_node_channel_interrupted_when_capturing_team_leaves();
    test_node_already_owned_by_present_team_has_no_channel();
    test_tree_doubles_channel_speed();
    test_flamel_mark_speeds_up_channel_on_marked_ground();
    test_pizza_corrupts_any_channel_regardless_of_side();
    test_tree_q_roots_and_damages_in_range();
    test_tree_q_out_of_range_whiffs();
    test_tree_r_self_roots_grants_armor_and_heals();
    test_tree_r_makes_immune_to_duck_pull();
    test_pizza_q_damages_and_applies_burn();
    test_pizza_burn_ticks_damage_over_time();
    test_pizza_passive_aura_damages_nearby_foe();
    test_pizza_r_prevents_death_for_duration();
    test_flamel_q_roots_without_damage();
    test_flamel_w_heals_allies_in_radius();
    test_flamel_w_heals_more_on_marked_ground();
    test_flamel_r_roots_enemies_and_heals_allies_in_zone();
    test_flamel_r_mass_marks_nodes_in_radius();
    test_rooted_hero_cannot_move();
    test_morrigan_passive_grants_armor_on_contested_node();
    test_morrigan_q_executes_harder_at_low_hp();
    test_morrigan_w_teleports_and_roots_nearest_enemy();
    test_morrigan_r_zone_executes_harder_at_low_hp();
    test_dagda_passive_regenerates_hp();
    test_dagda_q_kills_when_enemy_in_range();
    test_dagda_q_revives_when_only_hurt_ally_in_range();
    test_dagda_w_heals_allies_and_cc_enemies_at_once();
    test_dagda_r_floor_and_heal();
    test_courier_q_dashes_and_damages();
    test_courier_q_cleanses_self_debuffs();
    test_courier_w_teleports_to_farther_node();
    test_courier_r_drains_life_from_nearest_enemy();
    test_courier_r_out_of_range_whiffs();
    test_creep_spawns_on_first_tick_with_flavor_from_node_owner();
    test_team_creep_spawns_at_graveyard_not_node_position();
    test_neutral_creep_still_spawns_at_node_position();
    test_team_creep_marches_toward_nearest_unowned_node();
    test_team_creep_idles_once_its_team_owns_every_node();
    test_team_creep_march_redirects_when_target_node_gets_captured();
    test_creep_attacks_nearby_hero();
    test_hero_does_not_attack_creep_while_an_enemy_hero_is_in_range();
    test_hero_kills_creep_and_queues_correct_respawn_timer();
    test_neutral_creep_kill_grants_capture_bonus_only_while_channeling();
    test_team_creep_kill_by_owning_team_heals();
    test_team_creep_kill_by_enemy_team_helps_flip_the_node();
    test_lane_creep_wave_spawns_for_both_teams_after_initial_delay();
    test_lane_creep_marches_toward_center_when_no_target();
    test_lane_creep_attacks_nearby_enemy_hero_and_does_not_advance();
    test_lane_creeps_fight_each_other_when_opposing_teams_meet();
    test_hero_kills_lane_creep_in_range();
    test_hero_does_not_attack_own_team_lane_creep();
    test_hero_does_not_attack_lane_creep_while_enemy_hero_in_range();
    test_lane_creep_despawns_at_final_waypoint_with_no_reward();
    test_lane_creep_wave_respawns_after_the_interval();
    test_stealthed_hero_captures_undetected_through_a_crowd_of_visible_enemies();
    test_two_visible_teams_still_interrupt_normally_even_near_a_stealthed_ally();
    test_starting_a_channel_breaks_the_capturer_stealth();
    test_damage_to_channeling_team_interrupts_the_capture();
    test_attack_target_clears_on_fresh_move_command();
    test_attack_target_chases_out_of_range_enemy();
    test_attack_target_re_chases_a_fleeing_target();
    test_attack_target_clears_when_target_dies();
    test_attack_target_rejects_own_team();
    test_gary_fires_homing_shot_at_locked_target_in_range();
    test_gary_does_not_deal_flat_melee_damage();
    test_homing_shot_hits_target_that_moves_off_the_original_line();
    test_homing_shot_fizzles_if_target_dies_before_arrival();
    test_dead_hero_respawns_at_graveyard_when_team_owns_no_node();
    test_dead_hero_respawns_at_owned_node_on_wave();
    test_respawn_wave_brings_back_all_dead_heroes_together();
    test_resource_win_condition_replaces_team_wipe();
    test_resource_accumulates_faster_with_more_owned_nodes();
    test_resource_cap_wins_the_match();
    test_sudden_death_does_not_fire_before_max_duration();
    test_sudden_death_picks_team_ahead_on_resources();
    test_sudden_death_tiebreaks_by_nodes_owned();
    test_sudden_death_full_tie_resolves_to_team_zero();
    test_paimon_q_damages_and_roots_in_range();
    test_paimon_q_out_of_range_whiffs();
    test_paimon_w_damages_and_silences_in_range();
    test_paimon_passive_silences_nearest_enemy_periodically();
    test_paimon_r_zone_damages_enemy_and_heals_ally();
    test_cast_flash_slot_set_on_q();
    test_cast_flash_slot_set_on_w_toggle_hero();
    test_cast_flash_slot_set_on_r();
    test_cast_flash_slot_not_set_when_q_blocked_by_cooldown();
    test_cast_flash_slot_not_set_when_w_blocked_by_its_own_cooldown();
    test_noor1_q_damages_and_roots_in_range();
    test_noor1_q_out_of_range_whiffs();
    test_noor1_w_grants_intangibility_and_cooldown();
    test_noor1_passive_silences_nearest_enemy_periodically();
    test_noor1_r_zone_damages_enemy_no_ally_heal();
    test_cain_passive_grants_flat_armor();
    test_cain_q_executes_harder_at_low_hp();
    test_cain_q_out_of_range_whiffs();
    test_cain_w_dashes_away_from_foe_and_cleanses();
    test_cain_r_arms_the_survive_floor();
    test_gunnr_passive_grants_flat_armor();
    test_gunnr_q_damages_in_melee_range();
    test_gunnr_q_out_of_range_whiffs();
    test_gunnr_w_is_a_free_toggle_regen();
    test_gunnr_r_executes_harder_at_low_hp();
    test_gunnr_r_out_of_range_whiffs_but_still_starts_cooldown();
    test_vassago_passive_regenerates_hp();
    test_vassago_q_damages_and_silences_in_range();
    test_vassago_q_out_of_range_whiffs();
    test_vassago_w_grants_ally_next_cast_refund();
    test_vassago_w_no_ally_in_1v1_whiffs();
    test_vassago_r_zone_silences_but_deals_no_damage();
    test_he_xiangu_passive_regenerates_hp();
    test_he_xiangu_q_damages_foe_and_heals_self();
    test_he_xiangu_q_out_of_range_whiffs();
    test_he_xiangu_w_is_a_free_toggle_regen();
    test_he_xiangu_r_zone_heals_ally_no_enemy_damage();
    test_beleth_passive_grants_flat_armor();
    test_beleth_q_damages_and_burns_foe();
    test_beleth_w_silences_no_damage();
    test_beleth_r_marks_zone_no_immediate_damage();
    test_beleth_r_detonates_after_fuse();
    test_beleth_r_out_of_range_whiffs();
    test_mnm_passive_grants_flat_armor();
    test_mnm_w_toggle_stacks_on_top_of_passive_armor();
    test_mnm_q_damages_and_roots_in_melee_range();
    test_mnm_r_roots_self_and_grants_survive_floor();
    test_mnm_r_survive_floor_actually_blocks_lethal_damage();
    test_gary_q_cast_spawns_projectile_no_instant_damage();
    test_gary_q_out_of_range_whiffs_no_projectile();
    test_gary_q_projectile_lands_after_travel_time();
    test_gary_q_projectile_misses_if_target_steps_off_line();
    test_gary_q_projectile_despawns_after_max_range_unhit();
    test_tyler_q_cast_spawns_projectile_no_instant_effect();
    test_tyler_q_projectile_roots_and_burns_on_hit();
    test_tyler_q_projectile_misses_if_target_steps_off_line();
    test_tyler_r_spawns_clones_linked_to_caster();
    test_tyler_clones_mirror_move_target_and_fight();
    test_tyler_shared_fate_clone_death_kills_tyler_and_siblings();
    test_tyler_death_kills_his_clones_too();
    test_hover_ally_or_nearest_falls_back_when_nothing_hovered();
    test_hover_ally_or_nearest_prefers_hover_target_over_nearest();
    test_hover_ally_or_nearest_falls_back_for_enemy_target();
    test_hover_ally_or_nearest_falls_back_for_dead_target();
    test_set_hover_target_out_of_range_owner_is_a_safe_noop();
    test_doc_wheel_q_heals_hover_target_over_nearest_ally();
    test_fountain_heals_hero_in_radius();
    test_fountain_does_not_heal_hero_outside_radius();
    test_fountain_heals_either_team_neutral();
    test_fountain_caps_healing_at_max_hp();
    test_fountain_does_not_heal_dead_hero();
    test_fountain_restores_mana();
    test_fountain_mana_restore_caps_at_max();
    test_mana_regenerates_out_of_combat();
    test_mana_regenerates_slowly_in_combat();
    test_mana_regenerates_faster_out_of_combat_than_in_combat();
    test_mana_regen_accumulates_correctly_across_many_small_ticks();
    test_taking_damage_rearms_the_combat_timer();
    test_combat_timer_counts_down_to_zero();
    test_team_creep_does_not_attack_own_owning_team();
    test_team_creep_still_attacks_opposing_team();
    test_neutral_creep_still_attacks_anyone();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
