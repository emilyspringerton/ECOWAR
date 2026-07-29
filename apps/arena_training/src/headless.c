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

#include "../../../packages/simulation/arena_game.h"

/* ARENA_TRAINING_OBS_SIZE: the exact, documented layout sim_get_obs() writes into out_obs.
 * Index  Field
 *   0    self hp
 *   1    self max_hp
 *   2    self mp
 *   3    self x
 *   4    self z
 *   5    self q_cooldown_ms
 *   6    self w_cooldown_ms
 *   7    self r_cooldown_ms
 *   8    self flow
 *   9    self xp
 *  10    self alive (0.0 or 1.0)
 *  11    foe hp
 *  12    foe max_hp
 *  13    foe x
 *  14    foe z
 *  15    foe alive (0.0 or 1.0)
 *  16    dx (foe x - self x)
 *  17    dz (foe z - self z)
 */
#define ARENA_TRAINING_OBS_SIZE 18

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
