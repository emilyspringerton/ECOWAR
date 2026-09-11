#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

mkdir -p "${BUILD_DIR}"

# Living Map (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377): the new hex-grid/town
# strategic layer, deliberately its own standalone package (packages/livingmap), not folded into
# scripts/test_arena.sh's own arena_game mod list -- this layer has no dependency on arena_game.c
# at all. Headless on purpose, same reasoning test_arena.sh's own header comment gives.
gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -include "${ROOT_DIR}/packages/livingmap/frontier_village_mod_host.h" \
  -o "${BUILD_DIR}/test_hex_grid" \
  "${ROOT_DIR}/tests/test_hex_grid.c" \
  "${ROOT_DIR}/packages/livingmap/hex_grid.c" \
  "${ROOT_DIR}/packages/livingmap/living_map_events.c" \
  "${ROOT_DIR}/packages/livingmap/town.c" \
  "${ROOT_DIR}/packages/livingmap/frontier_village_mod.c" \
  -lm

gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
  -include "${ROOT_DIR}/packages/livingmap/frontier_village_mod_host.h" \
  -o "${BUILD_DIR}/test_town_frontier_village" \
  "${ROOT_DIR}/tests/test_town_frontier_village.c" \
  "${ROOT_DIR}/packages/livingmap/hex_grid.c" \
  "${ROOT_DIR}/packages/livingmap/living_map_events.c" \
  "${ROOT_DIR}/packages/livingmap/town.c" \
  "${ROOT_DIR}/packages/livingmap/frontier_village_mod.c" \
  -lm

"${BUILD_DIR}/test_hex_grid"
"${BUILD_DIR}/test_town_frontier_village"
