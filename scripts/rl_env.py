#!/usr/bin/env python3
"""
scripts/rl_env.py (S170-225, NORTHSTAR §21) -- a gymnasium.Env wrapping
apps/arena_training/src/headless.c's ctypes API (build/libarena_training.so, see
scripts/build_training.sh) for reward-driven reinforcement learning against REDGARDEN's arena
sim, in place of S170-194/195/220's own corpus-based next-token-prediction pretraining (founder:
"running training on a corpus of games is cool but thats not what i actually want right now i
want unsupervised learning with rewards like in the unity ml-agents plugin").

Architecture (see NORTHSTAR §21.2 for the full design): hero 0 is "the Agent" (matches sibling
SHANKPIT's own apps/training/headless.c convention, Player 0 = the trainee); hero 1 is the
existing hand-authored heuristic bot AI (arena_bot_enabled, already on by default in the C sim --
no separate opponent code needed, same "practice against what already exists" reasoning
SHANKPIT's own headless.c already established).

Observation: the 18-float vector apps/arena_training/src/headless.c's own sim_get_obs() writes
(self hp/max_hp/mp/x/z/q_cd/w_cd/r_cd/flow/xp/alive, foe hp/max_hp/x/z/alive, dx, dz) -- see that
file's own doc comment for the exact index layout, mirrored here via ARENA_TRAINING_OBS_SIZE so
the two stay in sync by construction, not by two independently-hand-maintained numbers.

Action: a 5-float Box -- [move_x, move_z, cast_q, cast_w, cast_r]. move_x/move_z are passed
straight through to sim_step (arena_set_move_target's own range, roughly the map's own
ARENA_HALF_EXTENT -- see arena_game.h -- clamped here to a conservative fixed range rather than
importing that C constant, since a slightly-too-generous move target is harmless, arena_game.c's
own movement code already clamps position to the map bounds every tick regardless of what target
it's chasing). cast_q/cast_w/cast_r are continuous in the action space (Box, not MultiBinary) for
PPO's own convenience (a single Box action space is simpler to configure than a mixed
Box+MultiBinary Dict space) but interpreted as a THRESHOLD in step() below -- any value > 0
attempts that cast for this tick.

NOTE ON VERIFICATION (S170-225): `gymnasium`/`stable_baselines3` are not installable in the
environment this file was written in (no venv module available, system Python is
externally-managed, no sudo) -- this file's ctypes loading, observation shape, and full reward
function were verified directly against the real compiled .so with plain ctypes (bypassing
gymnasium's own Env base class entirely -- see this pass's own commit message for the concrete
numbers from that run). The gymnasium.Env subclassing itself (this class's own reset()/step()
method signatures and Box space construction) is written to gymnasium's documented API but has
NOT been run against a real gymnasium install -- flagged here rather than claimed, same "flagged
not faked" discipline every other gap in this session got. Whoever next has a normal (non-
externally-managed) Python environment should `pip install gymnasium stable-baselines3` and run
`python3 scripts/rl_env.py --smoke-test` (works with or without gymnasium installed -- see
`__main__` below) to close that gap.
"""

import argparse
import ctypes
import math
import os
import random

ARENA_TRAINING_OBS_SIZE = 18

# Index layout -- must match apps/arena_training/src/headless.c's own sim_get_obs() doc comment
# exactly. Named here so reward computation below reads by name, not by magic index number.
OBS_SELF_HP = 0
OBS_SELF_MAX_HP = 1
OBS_SELF_MP = 2
OBS_SELF_X = 3
OBS_SELF_Z = 4
OBS_SELF_Q_CD = 5
OBS_SELF_W_CD = 6
OBS_SELF_R_CD = 7
OBS_SELF_FLOW = 8
OBS_SELF_XP = 9
OBS_SELF_ALIVE = 10
OBS_FOE_HP = 11
OBS_FOE_MAX_HP = 12
OBS_FOE_X = 13
OBS_FOE_Z = 14
OBS_FOE_ALIVE = 15
OBS_DX = 16
OBS_DZ = 17

# S170-223's own reward design (NORTHSTAR §21.2) -- dense per-tick shaping (small, so it can't
# outweigh actually engaging) plus a dominant sparse terminal win/loss term (the real objective).
REWARD_DAMAGE_DEALT_PER_HP = 0.01
REWARD_DAMAGE_TAKEN_PER_HP = -0.01
REWARD_KILL = 5.0
REWARD_DEATH = -5.0
REWARD_FLOW_PER_POINT = 0.001
REWARD_XP_PER_POINT = 0.0005
REWARD_ALIVE_PER_TICK = 0.001
REWARD_WIN = 10.0
REWARD_LOSS = -10.0

# ArenaHeroID values REDGARDEN's own packages/simulation/arena_game.h defines -- kept in sync by
# hand here, same "pure ctypes client, no direct C header access" reasoning apps/arena_bot/src/
# main.c's own ARENA_HERO_COUNT duplicate already documents for itself.
ARENA_HERO_UNICORN = 0
ARENA_HERO_DUCK = 1

# ARENA_HALF_EXTENT (2026-07-29): hand-synced copy of packages/simulation/arena_game.h's own
# real constant (this file has no C header access, same "duplicated by hand" reasoning
# apps/arena_bot/src/main.c's own ARENA_HERO_COUNT copy already documents for itself) -- must be
# bumped here too if that constant ever changes again (S170-191's own golden-ratio history is a
# real precedent that it has). Needed for real this time (not just as a comment): spawn
# randomization below picks positions relative to it.
ARENA_HALF_EXTENT = 51.78

# SPAWN_MARGIN keeps randomized spawns off the literal map edge (obstacles/geometry live out
# there in the real game; the training sim doesn't model them, but there's no reason to spend
# training time on positions no real fight would ever start at either).
SPAWN_MARGIN = 0.85 * ARENA_HALF_EXTENT

# Move targets get clamped to this range before being passed to sim_step. Originally a
# conservative 20.0 against the training arena's old always-near-origin fixed spawns (-6/+6) --
# found live (2026-07-29, wiring the trained policy into apps/arena_bot's real networked match
# bots, REDGARDEN Apple #11301) to be a real bug once spawns are no longer fixed: the policy's
# own action output is an ABSOLUTE world-space target, so clipping it to +-20 made it physically
# unable to aim anywhere near a hero actually spawned out past that range -- exactly the
# coordinate-frame mismatch that Apple's own doc comment flagged and worked around with a
# nudge-not-teleport reinterpretation on the C side. Matching this to the real map's own reach
# directly, rather than continuing to patch around it downstream, is the actual fix -- see
# ArenaTrainingEnv.reset()'s own doc comment below for the matching spawn-randomization half of
# this fix.
MOVE_TARGET_RANGE = ARENA_HALF_EXTENT

DEFAULT_LIB_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "libarena_training.so"
)


def load_lib(lib_path=None):
    """Loads the compiled shared library and sets up ctypes argtypes/restype for every
    function apps/arena_training/src/headless.c exports. Kept as a standalone function (not
    inlined into the Env's own __init__) so the ctypes-loading logic itself can be exercised
    and verified independent of gymnasium being installed at all -- see this file's own module
    doc comment."""
    lib = ctypes.CDLL(lib_path or DEFAULT_LIB_PATH)
    lib.sim_init.argtypes = [ctypes.c_int, ctypes.c_int]
    lib.sim_init.restype = None
    lib.sim_reset.argtypes = [ctypes.c_int, ctypes.c_int]
    lib.sim_reset.restype = None
    lib.sim_set_hero_position.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float]
    lib.sim_set_hero_position.restype = None
    lib.sim_step.argtypes = [
        ctypes.c_float, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint,
    ]
    lib.sim_step.restype = None
    lib.sim_get_obs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_float)]
    lib.sim_get_obs.restype = ctypes.c_int
    lib.sim_get_done.argtypes = []
    lib.sim_get_done.restype = ctypes.c_int
    lib.sim_get_winner.argtypes = []
    lib.sim_get_winner.restype = ctypes.c_int
    return lib


def compute_reward(prev_obs, cur_obs, done, winner, agent_owner=0):
    """S170-223's own full reward function (NORTHSTAR §21.2), computed as a delta between two
    consecutive sim_get_obs() snapshots -- deliberately NOT accumulated inside the C sim (unlike
    SHANKPIT's own accumulated_reward field) so this function stays tunable without a C
    recompile every time reward shaping needs adjusting, the real practical reason NORTHSTAR
    §21.2 gives for keeping this on the Python side. prev_obs may be None for the very first
    tick of an episode (nothing to diff against yet -- returns 0.0 in that case, same "no
    fabricated signal on the first tick" reasoning a real Gym env's own reset() gives)."""
    if prev_obs is None:
        return 0.0

    reward = 0.0

    self_hp_delta = cur_obs[OBS_SELF_HP] - prev_obs[OBS_SELF_HP]
    foe_hp_delta = cur_obs[OBS_FOE_HP] - prev_obs[OBS_FOE_HP]

    if foe_hp_delta < 0:
        reward += REWARD_DAMAGE_DEALT_PER_HP * (-foe_hp_delta)
    if self_hp_delta < 0:
        reward += REWARD_DAMAGE_TAKEN_PER_HP * (-self_hp_delta)  # already negative, adds a penalty

    # Kill/death: an alive->dead (1.0 -> 0.0) transition on the relevant side.
    if prev_obs[OBS_FOE_ALIVE] > 0.5 and cur_obs[OBS_FOE_ALIVE] <= 0.5:
        reward += REWARD_KILL
    if prev_obs[OBS_SELF_ALIVE] > 0.5 and cur_obs[OBS_SELF_ALIVE] <= 0.5:
        reward += REWARD_DEATH

    flow_delta = cur_obs[OBS_SELF_FLOW] - prev_obs[OBS_SELF_FLOW]
    if flow_delta > 0:
        reward += REWARD_FLOW_PER_POINT * flow_delta

    xp_delta = cur_obs[OBS_SELF_XP] - prev_obs[OBS_SELF_XP]
    if xp_delta > 0:
        reward += REWARD_XP_PER_POINT * xp_delta

    if cur_obs[OBS_SELF_ALIVE] > 0.5:
        reward += REWARD_ALIVE_PER_TICK

    if done:
        # winner: 0=none, 1=hero0(agent), 2=hero1(opponent) -- see sim_get_winner's own doc
        # comment in apps/arena_training/src/headless.c.
        agent_won = winner == (1 if agent_owner == 0 else 2)
        opponent_won = winner == (2 if agent_owner == 0 else 1)
        if agent_won:
            reward += REWARD_WIN
        elif opponent_won:
            reward += REWARD_LOSS

    return reward


try:
    import gymnasium as gym
    from gymnasium import spaces
    import numpy as np

    class ArenaTrainingEnv(gym.Env):
        """gymnasium.Env wrapping apps/arena_training/src/headless.c. See this module's own doc
        comment for the full architecture and the observation/action/reward layout."""

        metadata = {"render_modes": []}

        def __init__(self, lib_path=None, hero0_id=ARENA_HERO_UNICORN, hero1_id=ARENA_HERO_DUCK,
                     dt_ms=16, max_episode_ticks=3000):
            super().__init__()
            self.lib = load_lib(lib_path)
            self.hero0_id = hero0_id
            self.hero1_id = hero1_id
            self.dt_ms = dt_ms
            self.max_episode_ticks = max_episode_ticks
            self._tick = 0
            self._prev_obs = None

            self.observation_space = spaces.Box(
                low=-1e4, high=1e4, shape=(ARENA_TRAINING_OBS_SIZE,), dtype=np.float32
            )
            # [move_x, move_z, cast_q, cast_w, cast_r] -- cast_* interpreted as "> 0 = attempt
            # this tick" in step() below, a continuous Box rather than a mixed Box+MultiBinary
            # Dict space purely for PPO configuration simplicity (see this module's own doc
            # comment).
            self.action_space = spaces.Box(
                low=np.array([-MOVE_TARGET_RANGE, -MOVE_TARGET_RANGE, -1.0, -1.0, -1.0], dtype=np.float32),
                high=np.array([MOVE_TARGET_RANGE, MOVE_TARGET_RANGE, 1.0, 1.0, 1.0], dtype=np.float32),
                dtype=np.float32,
            )

        def _read_obs(self):
            buf = (ctypes.c_float * ARENA_TRAINING_OBS_SIZE)()
            self.lib.sim_get_obs(0, buf)
            return np.array(buf[:], dtype=np.float32)

        def reset(self, *, seed=None, options=None):
            super().reset(seed=seed)
            self.lib.sim_reset(self.hero0_id, self.hero1_id)
            # Spawn-position randomization (2026-07-29, see MOVE_TARGET_RANGE's own doc comment
            # above for the full "why" -- this is the other half of that same fix). Without
            # this, sim_reset alone always leaves both heroes at arena_init_with_heroes' own
            # fixed (-6,0)/(6,0) spawn, so nothing here would ever teach the policy what combat
            # looks like anywhere else on the map. Picks a random engagement CENTER anywhere
            # within SPAWN_MARGIN of map center, then places the two heroes a random distance
            # apart (8-16 units, the same order of magnitude as the original fixed 12-unit
            # separation) along a random facing -- varies both "where on the map" and "exactly
            # how far apart," rather than only one or the other. Clamped back inside
            # SPAWN_MARGIN afterward in case the offset would've pushed a hero past the edge.
            center_x = random.uniform(-SPAWN_MARGIN, SPAWN_MARGIN)
            center_z = random.uniform(-SPAWN_MARGIN, SPAWN_MARGIN)
            angle = random.uniform(0.0, 2.0 * math.pi)
            half_sep = random.uniform(8.0, 16.0) / 2.0
            offset_x = math.cos(angle) * half_sep
            offset_z = math.sin(angle) * half_sep
            h0x = max(-SPAWN_MARGIN, min(SPAWN_MARGIN, center_x - offset_x))
            h0z = max(-SPAWN_MARGIN, min(SPAWN_MARGIN, center_z - offset_z))
            h1x = max(-SPAWN_MARGIN, min(SPAWN_MARGIN, center_x + offset_x))
            h1z = max(-SPAWN_MARGIN, min(SPAWN_MARGIN, center_z + offset_z))
            self.lib.sim_set_hero_position(0, h0x, h0z)
            self.lib.sim_set_hero_position(1, h1x, h1z)
            self._tick = 0
            obs = self._read_obs()
            self._prev_obs = obs
            return obs, {}

        def step(self, action):
            move_x = float(action[0])
            move_z = float(action[1])
            cast_q = 1 if action[2] > 0 else 0
            cast_w = 1 if action[3] > 0 else 0
            cast_r = 1 if action[4] > 0 else 0

            self.lib.sim_step(move_x, move_z, cast_q, cast_w, cast_r, self.dt_ms)
            self._tick += 1

            obs = self._read_obs()
            done = bool(self.lib.sim_get_done())
            winner = self.lib.sim_get_winner()
            reward = compute_reward(self._prev_obs, obs, done, winner, agent_owner=0)
            self._prev_obs = obs

            terminated = done
            truncated = self._tick >= self.max_episode_ticks
            info = {"winner": winner, "tick": self._tick}
            return obs, reward, terminated, truncated, info

except ImportError:
    ArenaTrainingEnv = None  # gymnasium not installed -- see this module's own doc comment


def _smoke_test():
    """Runs without gymnasium installed -- exercises load_lib()/sim_step()/compute_reward()
    directly via plain ctypes, the same verification this pass's own commit message describes
    running for real against the compiled .so."""
    lib = load_lib()
    lib.sim_init(ARENA_HERO_UNICORN, ARENA_HERO_DUCK)

    buf = (ctypes.c_float * ARENA_TRAINING_OBS_SIZE)()
    lib.sim_get_obs(0, buf)
    prev_obs = list(buf)
    print("initial obs:", prev_obs)

    total_reward = 0.0
    done = False
    winner = 0
    for i in range(400):
        lib.sim_step(4.0, 0.0, 1, 0, 0, 16)
        lib.sim_get_obs(0, buf)
        cur_obs = list(buf)
        done = bool(lib.sim_get_done())
        winner = lib.sim_get_winner()
        total_reward += compute_reward(prev_obs, cur_obs, done, winner, agent_owner=0)
        prev_obs = cur_obs
        if done:
            print(f"episode ended at tick {i}, winner={winner}")
            break

    print("final obs:", prev_obs)
    print("total accumulated reward:", total_reward)
    print("done:", done, "winner:", winner)
    if ArenaTrainingEnv is None:
        print("\ngymnasium not installed in this environment -- ArenaTrainingEnv itself was NOT "
              "exercised, only load_lib()/sim_step()/compute_reward() above. See this module's "
              "own doc comment.")
    else:
        print("\ngymnasium IS installed -- running a real ArenaTrainingEnv rollout too:")
        env = ArenaTrainingEnv()
        obs, info = env.reset()
        ep_reward = 0.0
        for i in range(400):
            action = env.action_space.sample()
            action[0], action[1] = 4.0, 0.0  # bias toward closing distance, same as above
            obs, reward, terminated, truncated, info = env.step(action)
            ep_reward += reward
            if terminated or truncated:
                print(f"gymnasium rollout ended at tick {i}, info={info}")
                break
        print("gymnasium rollout total reward:", ep_reward)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--smoke-test", action="store_true",
                   help="run a scripted rollout against the compiled .so and print reward "
                        "totals -- works with or without gymnasium installed")
    args = p.parse_args()
    if args.smoke_test:
        _smoke_test()
    else:
        p.print_help()
