#!/usr/bin/env bash
# scripts/build_training.sh (S170-224, NORTHSTAR §21) -- builds the ctypes-callable shared
# library apps/arena_training/src/headless.c exposes for the Python RL environment
# (scripts/rl_env.py). Kept as its own script, separate from scripts/build.sh's own game
# binaries -- this produces a .so for ctypes to dlopen, not a standalone executable, a genuinely
# different consumer than every other build.sh target.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

# packages/common/mlp_infer.c (S170-228): arena_game.c now #includes packages/common/
# rl_policy_weights.h and calls rl_policy_forward()/mlp_forward() from arena_bot_tick --
# unreachable during actual training (sim_init/sim_step drive owner 1 via
# arena_bot_tick_heuristic()/bot_cast_kit_if_ready() directly, arena_bot_enabled is off, so
# arena_update() never calls arena_bot_tick() here), but leaving mlp_forward undefined in this
# shared library is still real, fragile, worth-fixing risk -- a shared lib can build clean with
# an unresolved symbol (deferred to runtime), only failing loudly if that path ever DOES get
# exercised, which is a worse way to find out than a normal link error would have been.
gcc -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -fPIC -shared \
  -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/libarena_training.so" \
  "${ROOT_DIR}/apps/arena_training/src/headless.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  -lm

echo "Built ${BUILD_DIR}/libarena_training.so"
