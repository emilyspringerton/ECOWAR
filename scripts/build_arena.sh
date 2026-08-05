#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

# No -lGLU: the arena client is a shader-based (modern GL) renderer, loading
# GL 3.x entry points itself via SDL_GL_GetProcAddress, so it doesn't need
# GLU at all (unlike apps/lobby, which is blocked here on a missing
# libglu1-mesa-dev).
# -D_DEFAULT_SOURCE: needed by packages/common/http_client.h's getaddrinfo/
# struct addrinfo/usleep under -std=c99 (same fix already applied to
# scripts/build.sh for the same reason).
# packages/common/mlp_infer.c (S170-228): arena_game.c's own arena_bot_tick now calls
# rl_policy_forward() (packages/common/rl_policy_weights.h), which calls mlp_forward(),
# defined in mlp_infer.c -- missing here broke CI's Linux build (undefined reference at
# link time; scripts/build.sh and scripts/build_training.sh already had this fix).
gcc -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -o "${BUILD_DIR}/red_garden_arena" \
  "${ROOT_DIR}/apps/arena/src/main.c" \
  "${ROOT_DIR}/packages/simulation/arena_game.c" \
  "${ROOT_DIR}/packages/simulation/arena_replay.c" \
  "${ROOT_DIR}/packages/simulation/arena_ai_bridge.c" \
  "${ROOT_DIR}/packages/common/mlp_infer.c" \
  "${ROOT_DIR}/packages/goldenband/gband.c" \
  "${ROOT_DIR}/packages/goldenband/gband_rig.c" \
  "${ROOT_DIR}/packages/goldenband/gskel.c" \
  "${ROOT_DIR}/packages/goldenband/gmesh.c" \
  "${ROOT_DIR}/packages/goldenband/gband_mesh_rig.c" \
  -lSDL2 -lGL -lm
