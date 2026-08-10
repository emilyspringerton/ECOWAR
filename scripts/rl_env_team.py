#!/usr/bin/env python3
"""
scripts/rl_env_team.py (NORTHSTAR §25.2.1) -- multi-agent extension of scripts/rl_env.py, wrapping
apps/arena_training/src/headless.c's team-mode ctypes API (sim_init_team/sim_step_team/
sim_get_obs_team/sim_get_done_team/sim_get_winner_team) for shared-parameter multi-agent PPO
training against a full team, not just 1v1.

Architecture: team_size agents on team A (heroes[0..team_size-1], being trained) vs. team_size
heuristic-driven agents on team B (heroes[ARENA_TEAM_SIZE..ARENA_TEAM_SIZE+team_size-1], stable,
non-circular opponent -- same reasoning rl_env.py's own module doc comment already gives for why
hero 1 in the 1v1 case can't be the live-training policy). ALL team-A agents share ONE arena_state
and advance together, one sim_step_team() call per tick -- not team_size independent physics
worlds.

Exposed as a Stable-Baselines3 `VecEnv` (not team_size separate gymnasium.Env instances wrapped in
SB3's own SubprocVecEnv) specifically because that's the wrong shape here: SB3's normal VecEnv
assumption is N independent environments with independent episode lifecycles, which is exactly
what N separate physics worlds would give you -- but a real team match's N agents share one match,
one win/loss outcome, one clock. Implementing team_size "slots" of ONE shared match AS an SB3
VecEnv (step_async/step_wait taking/returning N-stacked arrays, one real sim_step_team() call per
step_wait(), all N slots auto-reset together when the shared match ends) gets genuine
shared-parameter multi-agent PPO training out of SB3's existing, already-used PPO implementation
with zero new dependencies (no PettingZoo, no custom trainer) -- SB3's PPO already knows how to
learn one shared policy from a VecEnv's N parallel rollout streams; the only real change is that
those N streams come from one team match instead of N independent ones.

Reward: reuses scripts/rl_env.py's own compute_reward() per-agent, per-tick -- each team-A agent's
own 18-scalar self/foe-in-front block (the first ARENA_TRAINING_OBS_SIZE floats of its own
sim_get_obs_team() slice) has the identical layout compute_reward() already expects, and
sim_get_winner_team()'s 1=team-A/2=team-B convention already matches compute_reward's own
agent_owner=0 assumption exactly (every team-A agent "wins" when winner==1), so no reward-function
duplication is needed here.

NOTE ON VERIFICATION: same posture as rl_env.py's own module doc comment -- gymnasium/
stable-baselines3 are not installable in the environment this file was written in. The ctypes
team-mode API itself was verified directly (see NORTHSTAR §25.2.1's own commit message for the
real ctypes smoke-test run). This file's VecEnv subclassing is written to SB3's documented
VecEnv API but has NOT been run against a real install -- flagged, not claimed. Run
`python3 scripts/rl_env_team.py --smoke-test` (works without gymnasium/SB3 installed) to verify
the ctypes layer; `python3 scripts/rl_train_team.py` needs a real Python env (Colab, same pattern
NORTHSTAR §21's own status update already used once) to close the SB3 gap.
"""

import argparse
import ctypes

from rl_env import (
    ARENA_HALF_EXTENT,
    ARENA_TRAINING_OBS_SIZE,
    DEFAULT_LIB_PATH,
    compute_reward,
)

# Noisy-gestalt alternating phased training (2026-08-10, founder: "ensure we are doing some of
# the new exotic training" -- NORTHSTAR §25.2.2, sourced from the CarePyre transcript's
# "Compositional Co-Adaptation" section). The transcript's own two-phase design, applied here for
# real (this was previously spec-only -- see rl_train_team.py's own "NOT yet trained here" note,
# now stale for this one piece):
#   Johnny phase: crank up a Synergy Reward so bots are heavily rewarded just for staying near a
#     living teammate -- "they will invent crazy, highly choreographed team setups. Win rate
#     drops, but team coordination skyrockets."
#   Spike phase: turn the Synergy Reward off entirely -- "the bots take the wild, highly
#     choreographed combo they just invented and strip out the fluff, refining it into a lethal,
#     meta-viable strategy." Reduces to the plain baseline reward from rl_env.py's compute_reward,
#     unchanged from before this feature existed.
# The transcript's own version computes synergy from a learned mutual-information objective
# requiring model internals SB3's stock PPO doesn't expose per-tick; this is a real, grounded,
# but simpler proxy computed straight from data already on the wire -- sim_get_obs_team's own
# teammate block (hp_frac, dx, dz, alive) already carries relative teammate position for exactly
# this reason -- a positional-proximity bonus for staying near a LIVING teammate, rather than the
# transcript's own attention-based mutual-info term. Simpler, but grounded in the same "reward
# genuinely interacting with each other" intent, and immediately runnable without a bigger
# architecture change.
REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY = 0.05  # small per-tick nudge, same order of magnitude
                                                    # as REWARD_ALIVE_PER_TICK in rl_env.py -- a
                                                    # constant presence incentive, not a
                                                    # damage/kill-scale reward that could dominate
                                                    # the real win-condition signal
SYNERGY_PROXIMITY_RADIUS = 8.0  # matches this file's sibling arena_game.h's own
                                  # ARENA_LANE_CREEP_XP_SHARE_RADIUS (8.0) -- "close enough to be
                                  # meaningfully fighting together," not just anywhere on the map
DEFAULT_GESTALT_PHASE_TICKS = 50_000  # ticks per phase (not SB3 timesteps exactly -- see
                                        # ArenaTeamVecEnv._global_tick's own doc comment) -- long
                                        # enough for a real Johnny-phase exploration window before
                                        # switching to Spike-phase refinement, short enough that a
                                        # single training run gets several alternations rather
                                        # than one Johnny phase that never actually resolves

# ARENA_TEAM_SIZE (2026-08-10): hand-synced copy of packages/simulation/arena_game.h's own real
# constant -- same "no direct C header access from Python" reasoning every other hand-synced
# constant in this file's sibling rl_env.py already documents for itself.
ARENA_TEAM_SIZE = 10

DEFAULT_TEAM_SIZE = 3  # NORTHSTAR §25.6 deliberately leaves the real first-run team size
                        # undecided -- 3v3 is a reasonable, small first target, easily overridden.

# Per-agent observation size: the same 18+2*ARENA_HERO_COUNT self/foe/hero-id-onehot block
# sim_get_obs's own layout already uses, PLUS (team_size-1)*4 teammate floats, PLUS a final
# team_size-long agent-identity one-hot (NORTHSTAR §25.2.2 role discovery prerequisite, added
# 2026-08-10 -- see headless.c's own sim_get_obs_team doc comment for the full reasoning: without
# this block the shared PPO policy has no signal for "which of my team_size copies am I," so it
# cannot differentiate behavior per slot even where that would score higher). Must match
# apps/arena_training/src/headless.c's own sim_get_obs_team() return value exactly. A function,
# not a constant, since it depends on team_size.
def team_obs_size(team_size):
    return ARENA_TRAINING_OBS_SIZE + (team_size - 1) * 4 + team_size


def load_team_lib(lib_path=None):
    """Loads the compiled shared library and sets up ctypes argtypes/restype for the team-mode
    functions specifically (see rl_env.py's own load_lib() for the 1v1 functions -- both can be
    called against the same loaded library, since headless.c exports both APIs additively)."""
    lib = ctypes.CDLL(lib_path or DEFAULT_LIB_PATH)
    lib.sim_init_team.argtypes = [
        ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
    ]
    lib.sim_init_team.restype = None
    lib.sim_step_team.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.c_uint]
    lib.sim_step_team.restype = None
    lib.sim_get_obs_team.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_float)]
    lib.sim_get_obs_team.restype = ctypes.c_int
    lib.sim_get_done_team.argtypes = [ctypes.c_int]
    lib.sim_get_done_team.restype = ctypes.c_int
    lib.sim_get_winner_team.argtypes = [ctypes.c_int]
    lib.sim_get_winner_team.restype = ctypes.c_int
    return lib


try:
    from stable_baselines3.common.vec_env.base_vec_env import VecEnv
    import numpy as np
    from gymnasium import spaces

    class ArenaTeamVecEnv(VecEnv):
        """SB3 VecEnv wrapping ONE shared team match with `team_size` team-A agent "slots" --
        see this module's own doc comment for why VecEnv (not team_size independent
        gymnasium.Env instances) is the right shape here."""

        def __init__(self, team_size=DEFAULT_TEAM_SIZE, lib_path=None, dt_ms=16,
                     max_episode_ticks=4000, hero_ids_a=None, hero_ids_b=None,
                     noisy_gestalt=False, gestalt_phase_ticks=DEFAULT_GESTALT_PHASE_TICKS):
            if team_size < 2 or team_size > ARENA_TEAM_SIZE:
                raise ValueError(f"team_size must be in [2, {ARENA_TEAM_SIZE}], got {team_size}")
            self.team_size = team_size
            self.lib = load_team_lib(lib_path)
            self.dt_ms = dt_ms
            self.max_episode_ticks = max_episode_ticks
            # Noisy-gestalt alternating phased training -- see this module's own doc comment
            # above REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY for the full design. _global_tick
            # counts real step_wait() calls across the WHOLE env lifetime (survives episode
            # resets, unlike self._tick which is per-episode) -- an env-tick proxy for SB3's own
            # global timestep counter, not an exact match to it (SB3 counts across n_steps
            # rollout boundaries with its own bookkeeping this env has no visibility into), but
            # close enough for "which phase are we in" purposes -- phase drift of a few hundred
            # ticks against SB3's own counter doesn't change the qualitative Johnny/Spike
            # alternation this feature is actually for.
            self.noisy_gestalt = noisy_gestalt
            self.gestalt_phase_ticks = gestalt_phase_ticks
            self._global_tick = 0
            self._last_logged_phase = None
            # Hero-per-slot: fixed unless the caller randomizes externally (unlike rl_env.py's
            # own randomize_heroes flag, not duplicated here yet -- NORTHSTAR §25.6 leaves this
            # open; the C API already accepts arbitrary hero_ids arrays, so adding per-episode
            # randomization later is additive, not a redesign).
            self.hero_ids_a = hero_ids_a or [0] * team_size  # default: everyone Unicorn
            self.hero_ids_b = hero_ids_b or [0] * team_size
            self._c_hero_ids_a = (ctypes.c_int * team_size)(*self.hero_ids_a)
            self._c_hero_ids_b = (ctypes.c_int * team_size)(*self.hero_ids_b)

            self._obs_size = team_obs_size(team_size)
            self._tick = 0
            self._prev_obs = None  # list of team_size np.ndarray, one per agent slot
            self._actions = None

            observation_space = spaces.Box(
                low=-1e4, high=1e4, shape=(self._obs_size,), dtype=np.float32
            )
            # Same [move_x, move_z, cast_q, cast_w, cast_r] shape as rl_env.py's own
            # ArenaTrainingEnv -- one action per agent slot, cast_* thresholded at >0 in
            # _apply_actions below, matching the 1v1 env's own convention exactly.
            action_space = spaces.Box(
                low=np.array([-ARENA_HALF_EXTENT, -ARENA_HALF_EXTENT, -1.0, -1.0, -1.0], dtype=np.float32),
                high=np.array([ARENA_HALF_EXTENT, ARENA_HALF_EXTENT, 1.0, 1.0, 1.0], dtype=np.float32),
                dtype=np.float32,
            )
            super().__init__(team_size, observation_space, action_space)
            self._do_reset()

        def _synergy_bonus(self, agent_obs):
            """Johnny-phase-only synergy proxy: sums REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY
            for every OTHER living team-A agent within SYNERGY_PROXIMITY_RADIUS, read straight
            off this agent's own teammate block in sim_get_obs_team's layout -- (team_size-1)
            slots of (hp_frac, dx, dz, alive) starting right after the shared 18+2*ARENA_HERO_COUNT
            self/foe block (see headless.c's own sim_get_obs_team doc comment for the exact
            layout this indexes into)."""
            bonus = 0.0
            for t in range(self.team_size - 1):
                base = ARENA_TRAINING_OBS_SIZE + t * 4
                alive = agent_obs[base + 3]
                if alive <= 0.5:
                    continue
                dx = agent_obs[base + 1]
                dz = agent_obs[base + 2]
                if (dx * dx + dz * dz) ** 0.5 <= SYNERGY_PROXIMITY_RADIUS:
                    bonus += REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY
            return bonus

        def _read_all_obs(self):
            obs_list = []
            for i in range(self.team_size):
                buf = (ctypes.c_float * self._obs_size)()
                self.lib.sim_get_obs_team(i, self.team_size, buf)
                obs_list.append(np.array(buf[:], dtype=np.float32))
            return obs_list

        def _do_reset(self):
            self.lib.sim_init_team(self.team_size, self._c_hero_ids_a, self._c_hero_ids_b)
            self._tick = 0
            obs = self._read_all_obs()
            self._prev_obs = obs
            return obs

        # --- VecEnv abstract interface ---

        def reset(self):
            obs = self._do_reset()
            return np.stack(obs, axis=0)

        def step_async(self, actions):
            self._actions = np.asarray(actions, dtype=np.float32)

        def step_wait(self):
            # ONE sim_step_team() call for the whole team this tick -- see this module's own doc
            # comment for why this must be a single batched call, not team_size individual ones.
            flat = (ctypes.c_float * (self.team_size * 5))()
            for i in range(self.team_size):
                a = self._actions[i]
                flat[i * 5 + 0] = float(a[0])
                flat[i * 5 + 1] = float(a[1])
                flat[i * 5 + 2] = 1.0 if a[2] > 0 else 0.0
                flat[i * 5 + 3] = 1.0 if a[3] > 0 else 0.0
                flat[i * 5 + 4] = 1.0 if a[4] > 0 else 0.0
            self.lib.sim_step_team(flat, self.team_size, self.dt_ms)
            self._tick += 1
            self._global_tick += 1

            obs = self._read_all_obs()
            done = bool(self.lib.sim_get_done_team(self.team_size))
            winner = self.lib.sim_get_winner_team(self.team_size)
            truncated = self._tick >= self.max_episode_ticks
            episode_over = done or truncated

            # Noisy-gestalt: even phase index = Johnny (synergy reward ON), odd = Spike (OFF) --
            # see this module's own doc comment above REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY.
            in_johnny_phase = (
                self.noisy_gestalt
                and (self._global_tick // self.gestalt_phase_ticks) % 2 == 0
            )
            if self.noisy_gestalt:
                phase_name = "Johnny (synergy ON)" if in_johnny_phase else "Spike (synergy OFF)"
                if phase_name != self._last_logged_phase:
                    print(f"[noisy-gestalt] tick {self._global_tick}: entering {phase_name} phase")
                    self._last_logged_phase = phase_name

            rewards = np.zeros(self.team_size, dtype=np.float32)
            for i in range(self.team_size):
                rewards[i] = compute_reward(
                    self._prev_obs[i], obs[i], done, winner, agent_owner=0
                )
                if in_johnny_phase:
                    rewards[i] += self._synergy_bonus(obs[i])
            self._prev_obs = obs

            dones = np.full(self.team_size, episode_over, dtype=bool)
            infos = [{"winner": winner, "tick": self._tick, "TimeLimit.truncated": truncated}
                     for _ in range(self.team_size)]

            if episode_over:
                # All team_size slots terminate together (shared match outcome) -- reset the
                # whole match now and hand back FRESH initial observations for every slot, same
                # "auto-reset on done" contract SB3's own VecEnv expects from each sub-env,
                # just applied to all team_size slots at once since they share one episode.
                for info in infos:
                    info["terminal_observation"] = None  # filled in below, per-slot
                fresh_obs = self._do_reset()
                for i, info in enumerate(infos):
                    info["terminal_observation"] = obs[i]
                obs_out = np.stack(fresh_obs, axis=0)
            else:
                obs_out = np.stack(obs, axis=0)

            return obs_out, rewards, dones, infos

        def close(self):
            pass

        def get_attr(self, attr_name, indices=None):
            return [getattr(self, attr_name)] * self.team_size

        def set_attr(self, attr_name, value, indices=None):
            setattr(self, attr_name, value)

        def env_method(self, method_name, *args, indices=None, **kwargs):
            return [getattr(self, method_name)(*args, **kwargs)] * self.team_size

        def env_is_wrapped(self, wrapper_class, indices=None):
            return [False] * self.team_size

except ImportError:
    ArenaTeamVecEnv = None  # gymnasium/stable_baselines3 not installed -- see this module's own
                             # doc comment


def _smoke_test(team_size):
    """Runs without gymnasium/SB3 installed -- exercises load_team_lib()/sim_step_team()/
    compute_reward() directly via plain ctypes, mirroring rl_env.py's own _smoke_test()."""
    lib = load_team_lib()
    hero_ids_a = (ctypes.c_int * team_size)(*([0] * team_size))
    hero_ids_b = (ctypes.c_int * team_size)(*([0] * team_size))
    lib.sim_init_team(team_size, hero_ids_a, hero_ids_b)

    obs_size = team_obs_size(team_size)
    prev_obs = []
    for i in range(team_size):
        buf = (ctypes.c_float * obs_size)()
        lib.sim_get_obs_team(i, team_size, buf)
        prev_obs.append(list(buf))
    print(f"team_size={team_size}, obs_size={obs_size}")
    print("initial obs[0]:", prev_obs[0])

    total_reward = [0.0] * team_size
    done = False
    winner = 0
    flat = (ctypes.c_float * (team_size * 5))()
    for tick in range(3000):
        # Naive scripted policy: everyone advances toward map center and tries every cast --
        # enough to force real engagement (agents from both teams end up in range of each
        # other), not a real trained policy.
        for i in range(team_size):
            flat[i * 5 + 0] = 0.0
            flat[i * 5 + 1] = 0.0
            flat[i * 5 + 2] = 1.0
            flat[i * 5 + 3] = 1.0
            flat[i * 5 + 4] = 1.0
        lib.sim_step_team(flat, team_size, 16)

        cur_obs = []
        for i in range(team_size):
            buf = (ctypes.c_float * obs_size)()
            lib.sim_get_obs_team(i, team_size, buf)
            cur_obs.append(list(buf))

        done = bool(lib.sim_get_done_team(team_size))
        winner = lib.sim_get_winner_team(team_size)
        for i in range(team_size):
            total_reward[i] += compute_reward(prev_obs[i], cur_obs[i], done, winner, agent_owner=0)
        prev_obs = cur_obs
        if done:
            print(f"episode ended at tick {tick}, winner={winner}")
            break

    print("total reward per agent:", total_reward)
    print("done:", done, "winner:", winner)

    if ArenaTeamVecEnv is None:
        print("\ngymnasium/stable_baselines3 not installed -- ArenaTeamVecEnv itself was NOT "
              "exercised, only the raw ctypes team API above. See this module's own doc comment.")
    else:
        print("\ngymnasium/stable_baselines3 ARE installed -- running a real ArenaTeamVecEnv "
              "rollout too:")
        env = ArenaTeamVecEnv(team_size=team_size)
        obs = env.reset()
        ep_reward = np.zeros(team_size)
        for i in range(400):
            actions = np.array([env.action_space.sample() for _ in range(team_size)])
            actions[:, 0] = 0.0
            actions[:, 1] = 0.0
            obs, rewards, dones, infos = env.step(actions)
            ep_reward += rewards
            if dones[0]:
                print(f"VecEnv rollout ended at tick {i}, info={infos[0]}")
                break
        print("VecEnv rollout total reward per agent:", ep_reward)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--smoke-test", action="store_true",
                   help="run a scripted rollout against the compiled .so and print reward "
                        "totals -- works with or without gymnasium/SB3 installed")
    p.add_argument("--team-size", type=int, default=DEFAULT_TEAM_SIZE)
    args = p.parse_args()
    if args.smoke_test:
        _smoke_test(args.team_size)
    else:
        p.print_help()
