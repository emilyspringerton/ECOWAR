/* apps/arena_training/headless.c (S170-224, NORTHSTAR §21): a ctypes-callable C environment
 * API for Python-side reinforcement learning, same three-function shape as the sibling SHANKPIT
 * repo's own apps/training/headless.c (sim_init/sim_step/sim_get_state -- see NORTHSTAR §21.1's
 * own doc comment for why that shape was reused rather than invented here) -- plus sim_reset
 * (cheap episode restart) and sim_get_obs/sim_get_done/sim_get_winner in place of SHANKPIT's own
 * raw ServerState* return.
 *
 * Deliberately does NOT expose a raw ArenaState* the way SHANKPIT's sim_get_state() exposes
 * ServerState* -- ArenaState is a large, actively-growing struct (heroes[]/nodes[]/creeps[]/
 * projectiles[]/lane_creeps[]/powerups[]/resources[], and this whole session added new fields to
 * it repeatedly); mirroring its exact layout as a Python ctypes.Structure would be fragile ABI
 * surface that silently desyncs the moment a future C-side field gets added, with no compiler to
 * catch it on the Python side. Instead, sim_get_obs() writes a small, fixed, DOCUMENTED flat
 * float array -- the same "count + fixed array, one stable format" philosophy
 * packages/common/protocol.h's own wire messages already use, just serving ctypes instead of
 * UDP. Reward computation itself stays entirely on the Python side (NORTHSTAR §21.2's own
 * reasoning: iterable without a C recompile every time reward shaping gets tuned) -- this file
 * only needs to expose enough raw state (hp/flow/xp/alive/position) for Python to compute those
 * deltas itself between consecutive sim_get_obs() calls.
 *
 * Controls hero 0 ("the Agent," matching SHANKPIT's own Player-0-is-the-Agent framing) via
 * sim_step's own action parameters. Hero 1 (the training opponent) is driven directly by
 * arena_bot_tick_heuristic()/bot_cast_kit_if_ready() -- NOT arena_update's own automatic
 * arena_bot_enabled-gated bot-tick, which as of S170-228 routes arena_bot_tick() itself through
 * whatever policy is currently compiled into packages/common/rl_policy_weights.h. Training needs
 * a STABLE, never-changing opponent: if hero 1 were driven by the same policy currently being
 * trained (or a stale earlier export of it), re-training would mean training against a moving
 * target of itself -- circular, and completely broken on the very first run, before any
 * rl_policy_weights.h has ever been exported. sim_init() disables arena_bot_enabled for exactly
 * this reason; sim_step() calls the stable heuristic functions directly, in hero 1's own place,
 * before arena_update() runs. */

#include <stddef.h>

#include "../../../packages/simulation/arena_game.h"

/* ARENA_TRAINING_OBS_SIZE: the exact, documented layout sim_get_obs() writes into out_obs.
 * Index      Field
 *   0        self hp
 *   1        self max_hp
 *   2        self mp
 *   3        self x
 *   4        self z
 *   5        self q_cooldown_ms
 *   6        self w_cooldown_ms
 *   7        self r_cooldown_ms
 *   8        self flow
 *   9        self xp
 *  10        self alive (0.0 or 1.0)
 *  11        foe hp
 *  12        foe max_hp
 *  13        foe x
 *  14        foe z
 *  15        foe alive (0.0 or 1.0)
 *  16        dx (foe x - self x)
 *  17        dz (foe z - self z)
 *  18..45    self hero_id, one-hot (ARENA_HERO_COUNT-wide)
 *  46..73    foe hero_id, one-hot (ARENA_HERO_COUNT-wide)
 *
 * The two one-hot blocks (2026-07-29, founder: "not just 2 heroes" -- see the module doc
 * comment's own history for the full "train across the roster + self-play" thread) are new:
 * every earlier pass's network had literally no way to know which hero it was controlling or
 * fighting -- indices 0-17 alone are hero-agnostic (hp/position/cooldowns mean the same thing
 * regardless of kit), so a policy trained only on Unicorn-vs-Duck couldn't meaningfully condition
 * on hero identity even if scripts/rl_env.py started randomizing hero0_id/hero1_id per episode.
 * One-hot rather than a raw scalar hero_id specifically because hero IDs are an unordered
 * categorical set (Unicorn=0 is not "closer" to Duck=1 than to Zagan=27 in any real sense) -- a
 * bare scalar would bake in a false ordinal relationship a small MLP has no way to undo. */
#define ARENA_TRAINING_OBS_SIZE (18 + 2 * ARENA_HERO_COUNT)

void sim_init(int hero0_id, int hero1_id) {
    arena_init_with_heroes((ArenaHeroID)hero0_id, (ArenaHeroID)hero1_id);
    /* S170-228: see this file's own module doc comment -- training drives hero 1 (the
       opponent) directly via arena_bot_tick_heuristic()/bot_cast_kit_if_ready() in sim_step
       below, not arena_update's own automatic bot-tick, so disabling this here prevents
       arena_update from ALSO independently ticking hero 1 a second time through whatever
       arena_bot_tick() itself currently does. */
    arena_bot_enabled = 0;
}

/* sim_reset: identical to sim_init -- a distinct name so the Python env's own reset() call reads
 * as "start a fresh episode," not "coincidentally the same call as first-time setup," even
 * though today they're the same operation. */
void sim_reset(int hero0_id, int hero1_id) {
    arena_init_with_heroes((ArenaHeroID)hero0_id, (ArenaHeroID)hero1_id);
    arena_bot_enabled = 0; /* see sim_init's own comment */
}

/* sim_set_hero_position (2026-07-29, NORTHSTAR §21 follow-up): training-only, called from
 * Python right after sim_reset to relocate a hero -- arena_init_with_heroes itself always
 * spawns hero 0/1 at the fixed (-6,0)/(6,0) points (unchanged, still what the LIVE 1v1
 * local-practice mode uses for real players), which is exactly why the first trained policy
 * generalized poorly once reused for real networked matches: it had only ever seen combat
 * near the map's own origin, but a real 20-player match happens anywhere across the full
 * ~103-unit-wide map (packages/simulation/arena_game.h's own ARENA_HALF_EXTENT ~51.78, see
 * scripts/rl_env.py's own hand-synced copy of that constant). Lets Python's own reset()
 * randomize BOTH heroes' spawn positions across the real map extent each episode instead,
 * teaching the policy "engage whoever's near you, wherever that is" rather than "engage
 * whoever's near map-center." Sets target_x/target_z to match too, same as
 * arena_init_with_heroes's own spawn-time convention, so a hero doesn't immediately start
 * "moving" back toward whatever stale target a previous episode left behind. */
void sim_set_hero_position(int owner, float x, float z) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    arena_state.heroes[owner].x = x;
    arena_state.heroes[owner].z = z;
    arena_state.heroes[owner].target_x = x;
    arena_state.heroes[owner].target_z = z;
}

/* sim_step: applies the Agent's (hero 0's) action, drives hero 1 (the training opponent) with
 * the STABLE heuristic AI directly (see this file's own module doc comment for why it can't go
 * through arena_update's own automatic, now-RL-driven bot-tick path), then ticks the sim forward
 * dt_ms. move_x/move_z set a new move target every call (arena_set_move_target itself is
 * idempotent/safe to call every tick, same as a human repeatedly re-clicking) -- cast_q/cast_w/
 * cast_r are edge-triggered by the CALLER (Python), not debounced here: calling arena_cast_q
 * every tick while cast_q stays 1 would just repeatedly hit its own cooldown gate and no-op,
 * which is safe but wasteful; the Python env is expected to only pass 1 the single tick it wants
 * to actually attempt that cast, matching how a real discrete action space works. */
void sim_step(float move_x, float move_z, int cast_q, int cast_w, int cast_r, unsigned int dt_ms) {
    arena_set_move_target(0, move_x, move_z);
    if (cast_q) arena_cast_q(0);
    if (cast_w) arena_toggle_w(0);
    if (cast_r) arena_cast_r(0);

    arena_bot_tick_heuristic(dt_ms);
    bot_cast_kit_if_ready(&arena_state.heroes[1], &arena_state.heroes[0]);

    arena_update(dt_ms);
}

/* sim_step_both (2026-07-29, founder: "not just 2 heroes" / self-play): applies REAL actions to
 * BOTH heroes instead of driving hero 1 through the stable heuristic -- what actual self-play
 * needs. Python is expected to call sim_get_obs(1, ...) to get hero 1's own mirrored-perspective
 * observation (sim_get_obs's own doc comment already flagged it as symmetric "in case a later
 * pass wants... self-play"), run it through whatever policy is standing in for the opponent this
 * training run (a frozen past checkpoint -- see scripts/rl_env.py's own doc comment for why a
 * frozen snapshot, not the live-updating model, avoids the "training against a moving target of
 * itself" circularity sim_step's own module doc comment already named), and pass its action back
 * in here. arena_bot_enabled is already 0 (sim_init/sim_reset), so arena_update() below won't
 * ALSO independently drive hero 1 a second time on top of these explicit actions -- same gating
 * sim_step's own heuristic path already relies on. */
void sim_step_both(float move_x0, float move_z0, int cast_q0, int cast_w0, int cast_r0,
                    float move_x1, float move_z1, int cast_q1, int cast_w1, int cast_r1,
                    unsigned int dt_ms) {
    arena_set_move_target(0, move_x0, move_z0);
    if (cast_q0) arena_cast_q(0);
    if (cast_w0) arena_toggle_w(0);
    if (cast_r0) arena_cast_r(0);

    arena_set_move_target(1, move_x1, move_z1);
    if (cast_q1) arena_cast_q(1);
    if (cast_w1) arena_toggle_w(1);
    if (cast_r1) arena_cast_r(1);

    arena_update(dt_ms);
}

/* sim_get_obs: writes ARENA_TRAINING_OBS_SIZE floats for `owner`'s own point of view (0 = the
 * Agent's own perspective, 1 = the opponent's -- exposed symmetrically in case a later pass
 * wants to observe both sides, e.g. for self-play; NORTHSTAR §21.3 explicitly defers self-play
 * itself, but there's no reason to make this function single-sided when symmetry costs nothing).
 * out_obs must be preallocated to ARENA_TRAINING_OBS_SIZE floats by the caller. Returns
 * ARENA_TRAINING_OBS_SIZE so Python can assert its own buffer size matches without hardcoding
 * the number twice. */
int sim_get_obs(int owner, float *out_obs) {
    int foe_owner = owner == 0 ? 1 : 0;
    ArenaHero *self = &arena_state.heroes[owner];
    ArenaHero *foe = &arena_state.heroes[foe_owner];

    out_obs[0]  = (float)self->hp;
    out_obs[1]  = (float)self->max_hp;
    out_obs[2]  = (float)self->mp;
    out_obs[3]  = self->x;
    out_obs[4]  = self->z;
    out_obs[5]  = (float)(self->q_cooldown_ms > 0 ? self->q_cooldown_ms : 0);
    out_obs[6]  = (float)(self->w_cooldown_ms > 0 ? self->w_cooldown_ms : 0);
    out_obs[7]  = (float)(self->r_cooldown_ms > 0 ? self->r_cooldown_ms : 0);
    out_obs[8]  = (float)(self->flow > 0 ? self->flow : 0);
    out_obs[9]  = (float)self->xp;
    out_obs[10] = self->alive ? 1.0f : 0.0f;

    out_obs[11] = (float)(foe->hp > 0 ? foe->hp : 0);
    out_obs[12] = (float)foe->max_hp;
    out_obs[13] = foe->x;
    out_obs[14] = foe->z;
    out_obs[15] = foe->alive ? 1.0f : 0.0f;

    out_obs[16] = foe->x - self->x;
    out_obs[17] = foe->z - self->z;

    /* One-hot hero identity blocks -- see ARENA_TRAINING_OBS_SIZE's own doc comment above for
       why one-hot, not a raw scalar. Zeroed first since self->hero_id/foe->hero_id are each only
       ever one index into their own ARENA_HERO_COUNT-wide block. */
    for (int i = 0; i < ARENA_HERO_COUNT; i++) {
        out_obs[18 + i] = 0.0f;
        out_obs[18 + ARENA_HERO_COUNT + i] = 0.0f;
    }
    if (self->hero_id >= 0 && self->hero_id < ARENA_HERO_COUNT) {
        out_obs[18 + self->hero_id] = 1.0f;
    }
    if (foe->hero_id >= 0 && foe->hero_id < ARENA_HERO_COUNT) {
        out_obs[18 + ARENA_HERO_COUNT + foe->hero_id] = 1.0f;
    }

    return ARENA_TRAINING_OBS_SIZE;
}

/* sim_get_done: 1 once the episode has a winner (either hero died -- see arena_update's own
 * 1v1 win-condition check), 0 while still live. */
int sim_get_done(void) {
    return arena_state.winner != 0;
}

/* sim_get_winner: 0 = no winner yet, 1 = hero0 (the Agent) won, 2 = hero1 (the opponent) won --
 * same convention ArenaState.winner already uses. */
int sim_get_winner(void) {
    return arena_state.winner;
}

/* ---------------------------------------------------------------------------------------------
 * Team-mode API (NORTHSTAR §25.2.1, VS0): additive to everything above -- the 1v1 functions are
 * untouched and still work exactly as before. Team A (heroes[0..team_size-1]) is the side being
 * trained (multi-agent policy, one action per agent per sim_step_team call); team B
 * (heroes[ARENA_TEAM_SIZE..ARENA_TEAM_SIZE+team_size-1]) is the stable, non-circular heuristic
 * opponent, same `arena_bot_tick_heuristic`/`bot_cast_kit_if_ready` functions the 1v1 API
 * already uses for exactly the same "training against a moving target of itself is circular"
 * reason sim_step's own module doc comment already gives -- just applied once per opponent agent
 * instead of once total.
 *
 * team_size scales from 2 up to ARENA_TEAM_SIZE (10, the real match size) without a second code
 * path -- NORTHSTAR §25.6 deliberately leaves which size to actually train first undecided. */

#define ARENA_TRAINING_MAX_TEAM_SIZE ARENA_TEAM_SIZE

/* sim_init_team: arena_init_teams() populates all ARENA_MAX_HEROES (20) slots at fixed
 * graveyard-corner positions; slots beyond team_size on either side are deactivated
 * (active=0, alive=0) rather than left alive as an unused team_size..9 rump -- see
 * arena_team_owns_any_node/respawn-adjacent logic elsewhere in arena_game.c, which already
 * expects to iterate "active" heroes, not assume exactly ARENA_TEAM_SIZE per side. hero_ids_a/
 * hero_ids_b are each team_size-long arrays overriding arena_init_teams()'s own
 * ARENA_HERO_UNICORN-for-everyone placeholder, same reasoning §21.2's own hero-identity one-hot
 * observation slots exist for: a policy that only ever sees Unicorn can't condition on hero kit
 * at all. */
void sim_init_team(int team_size, const int *hero_ids_a, const int *hero_ids_b) {
    if (team_size < 1) team_size = 1;
    if (team_size > ARENA_TRAINING_MAX_TEAM_SIZE) team_size = ARENA_TRAINING_MAX_TEAM_SIZE;

    arena_init_teams();
    arena_bot_enabled = 0; /* same reasoning as sim_init -- team B is driven directly below, not
                               through arena_update's own automatic bot-tick */

    for (int i = 0; i < ARENA_TEAM_SIZE; i++) {
        int team_a_idx = i;
        int team_b_idx = ARENA_TEAM_SIZE + i;
        if (i < team_size) {
            if (hero_ids_a) arena_state.heroes[team_a_idx].hero_id = (ArenaHeroID)hero_ids_a[i];
            if (hero_ids_b) arena_state.heroes[team_b_idx].hero_id = (ArenaHeroID)hero_ids_b[i];
        } else {
            arena_state.heroes[team_a_idx].active = 0;
            arena_state.heroes[team_a_idx].alive = 0;
            arena_state.heroes[team_b_idx].active = 0;
            arena_state.heroes[team_b_idx].alive = 0;
        }
    }
}

/* sim_step_team: applies one action per team-A agent (actions is a flat team_size*5 array --
 * [move_x, move_z, cast_q, cast_w, cast_r] per agent, same per-agent action shape sim_step
 * already uses for the 1v1 case, just repeated), drives every active team-B agent with the
 * stable heuristic against its own nearest team-A foe (a simple nearest-enemy targeting rule --
 * real team-fight target selection is a much larger, separate problem this training opponent
 * deliberately doesn't need to solve well, only to be a stable, non-trivial thing to train
 * against), then ticks the whole sim forward once. */
void sim_step_team(const float *actions, int team_size, unsigned int dt_ms) {
    if (team_size < 1) team_size = 1;
    if (team_size > ARENA_TRAINING_MAX_TEAM_SIZE) team_size = ARENA_TRAINING_MAX_TEAM_SIZE;

    for (int i = 0; i < team_size; i++) {
        if (!arena_state.heroes[i].alive) continue;
        const float *a = &actions[i * 5];
        arena_set_move_target(i, a[0], a[1]);
        if (a[2] != 0.0f) arena_cast_q(i);
        if (a[3] != 0.0f) arena_toggle_w(i);
        if (a[4] != 0.0f) arena_cast_r(i);
    }

    for (int i = 0; i < team_size; i++) {
        int b_idx = ARENA_TEAM_SIZE + i;
        ArenaHero *bot = &arena_state.heroes[b_idx];
        if (!bot->alive) continue;

        /* Nearest-alive-team-A-hero targeting -- deliberately simple; this opponent's job is to
           be stable and non-trivial, not to play optimal team-fight target selection. */
        ArenaHero *target = NULL;
        float best_dist2 = 0.0f;
        for (int j = 0; j < team_size; j++) {
            ArenaHero *cand = &arena_state.heroes[j];
            if (!cand->alive) continue;
            float dx = cand->x - bot->x, dz = cand->z - bot->z;
            float dist2 = dx * dx + dz * dz;
            if (!target || dist2 < best_dist2) {
                target = cand;
                best_dist2 = dist2;
            }
        }
        if (!target) continue;

        /* NOT arena_bot_tick_heuristic() -- that function is hardcoded to heroes[1] vs.
           heroes[0] (checked directly in arena_game.c: `ArenaHero *bot = &heroes[1]; ArenaHero
           *foe = &heroes[0];`, no index parameter at all), a genuinely 1v1-only function, not a
           per-agent-callable one -- calling it in this loop would silently only ever move
           heroes[1] toward heroes[0], team_size times redundantly, and never move any other
           team-B agent. Move-toward-target here instead: simpler than routing every team-B bot
           through bot_brain_forward's own per-agent-normalized inputs (which arena_bot_tick_
           heuristic wraps for exactly one fixed pair), and sufficient for "stable, non-trivial
           training opponent" per this file's own module doc comment -- optimal team-fight
           positioning is explicitly not this opponent's job. */
        arena_set_move_target(b_idx, target->x, target->z);
        bot_cast_kit_if_ready(bot, target);
    }

    arena_update(dt_ms);
}

/* sim_get_obs_team: writes ONE agent's own observation (team_a_owner's index within team A, 0..
 * team_size-1) into out_obs -- same 18+2*ARENA_HERO_COUNT self/nearest-foe/hero-id-one-hot
 * layout sim_get_obs already writes (nearest foe = nearest alive team-B hero, not literally
 * heroes[1]), PLUS a new teammate block: (team_size-1) slots of (hp_frac, dx, dz, alive) for
 * every OTHER team-A agent, ordered by team-A index (not by distance -- a fixed, stable
 * ordering, same "no false ordinal relationship" reasoning ARENA_TRAINING_OBS_SIZE's own doc
 * comment already gives for one-hot hero IDs applies here too: sorting by distance would make
 * "teammate slot 0" mean a different physical teammate every tick). Returns the actual number
 * of floats written so Python can assert its buffer size without hardcoding the formula twice. */
int sim_get_obs_team(int team_a_owner, int team_size, float *out_obs) {
    if (team_size < 1) team_size = 1;
    if (team_size > ARENA_TRAINING_MAX_TEAM_SIZE) team_size = ARENA_TRAINING_MAX_TEAM_SIZE;

    ArenaHero *self = &arena_state.heroes[team_a_owner];

    ArenaHero *foe = NULL;
    float best_dist2 = 0.0f;
    for (int i = 0; i < team_size; i++) {
        ArenaHero *cand = &arena_state.heroes[ARENA_TEAM_SIZE + i];
        if (!cand->alive) continue;
        float dx = cand->x - self->x, dz = cand->z - self->z;
        float dist2 = dx * dx + dz * dz;
        if (!foe || dist2 < best_dist2) {
            foe = cand;
            best_dist2 = dist2;
        }
    }

    out_obs[0]  = (float)self->hp;
    out_obs[1]  = (float)self->max_hp;
    out_obs[2]  = (float)self->mp;
    out_obs[3]  = self->x;
    out_obs[4]  = self->z;
    out_obs[5]  = (float)(self->q_cooldown_ms > 0 ? self->q_cooldown_ms : 0);
    out_obs[6]  = (float)(self->w_cooldown_ms > 0 ? self->w_cooldown_ms : 0);
    out_obs[7]  = (float)(self->r_cooldown_ms > 0 ? self->r_cooldown_ms : 0);
    out_obs[8]  = (float)(self->flow > 0 ? self->flow : 0);
    out_obs[9]  = (float)self->xp;
    out_obs[10] = self->alive ? 1.0f : 0.0f;

    if (foe) {
        out_obs[11] = (float)(foe->hp > 0 ? foe->hp : 0);
        out_obs[12] = (float)foe->max_hp;
        out_obs[13] = foe->x;
        out_obs[14] = foe->z;
        out_obs[15] = 1.0f;
        out_obs[16] = foe->x - self->x;
        out_obs[17] = foe->z - self->z;
    } else {
        /* Whole enemy team dead -- episode should already be `done` by the time this happens
           (see sim_get_done_team), but zero rather than garbage if Python reads one more obs
           anyway before checking done. */
        for (int i = 11; i <= 17; i++) out_obs[i] = 0.0f;
    }

    for (int i = 0; i < ARENA_HERO_COUNT; i++) {
        out_obs[18 + i] = 0.0f;
        out_obs[18 + ARENA_HERO_COUNT + i] = 0.0f;
    }
    if (self->hero_id >= 0 && self->hero_id < ARENA_HERO_COUNT) {
        out_obs[18 + self->hero_id] = 1.0f;
    }
    if (foe && foe->hero_id >= 0 && foe->hero_id < ARENA_HERO_COUNT) {
        out_obs[18 + ARENA_HERO_COUNT + foe->hero_id] = 1.0f;
    }

    int base = 18 + 2 * ARENA_HERO_COUNT;
    int slot = 0;
    for (int i = 0; i < team_size; i++) {
        if (i == team_a_owner) continue;
        ArenaHero *mate = &arena_state.heroes[i];
        float *o = &out_obs[base + slot * 4];
        o[0] = mate->max_hp > 0 ? (float)mate->hp / (float)mate->max_hp : 0.0f;
        o[1] = mate->x - self->x;
        o[2] = mate->z - self->z;
        o[3] = mate->alive ? 1.0f : 0.0f;
        slot++;
    }

    return base + (team_size - 1) * 4;
}

/* sim_get_done_team: episode ends when either whole team has no alive heroes left -- a fast
 * proxy for a real match's actual win condition (node/resource control, far too slow to reach
 * within a training episode's budget -- see NORTHSTAR §25.1's own note on this), same
 * "simple death-based termination for training, real rules for the live game" split the
 * existing 1v1 API already makes (arena_game.c's line-5623-ish 1v1 win check vs. its own
 * separate real-match resource/node win conditions). */
int sim_get_done_team(int team_size) {
    if (team_size < 1) team_size = 1;
    if (team_size > ARENA_TRAINING_MAX_TEAM_SIZE) team_size = ARENA_TRAINING_MAX_TEAM_SIZE;

    int a_alive = 0, b_alive = 0;
    for (int i = 0; i < team_size; i++) {
        if (arena_state.heroes[i].alive) a_alive = 1;
        if (arena_state.heroes[ARENA_TEAM_SIZE + i].alive) b_alive = 1;
    }
    return (!a_alive || !b_alive) ? 1 : 0;
}

/* sim_get_winner_team: 0 = no winner yet, 1 = team A (being trained) won, 2 = team B (heuristic
 * opponent) won. Only meaningful once sim_get_done_team() is true. */
int sim_get_winner_team(int team_size) {
    if (team_size < 1) team_size = 1;
    if (team_size > ARENA_TRAINING_MAX_TEAM_SIZE) team_size = ARENA_TRAINING_MAX_TEAM_SIZE;

    int a_alive = 0, b_alive = 0;
    for (int i = 0; i < team_size; i++) {
        if (arena_state.heroes[i].alive) a_alive = 1;
        if (arena_state.heroes[ARENA_TEAM_SIZE + i].alive) b_alive = 1;
    }
    if (a_alive && !b_alive) return 1;
    if (b_alive && !a_alive) return 2;
    return 0;
}
