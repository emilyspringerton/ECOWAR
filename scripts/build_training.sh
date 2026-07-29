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

gcc -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -fPIC -shared \
  -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/libarena_training.so" \
  "${ROOT_DIR}/apps/arena_training/src/headless.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  -lm

echo "Built ${BUILD_DIR}/libarena_training.so"
