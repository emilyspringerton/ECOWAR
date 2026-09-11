#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
LIVINGMAP_DIR="${ROOT_DIR}/packages/livingmap"

mkdir -p "${BUILD_DIR}"

# Living Map (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377): the new hex-grid/town/creep
# strategic layer, deliberately its own standalone package (packages/livingmap), not folded into
# scripts/test_arena.sh's own arena_game mod list -- this layer has no dependency on arena_game.c
# at all. Headless on purpose, same reasoning test_arena.sh's own header comment gives.
LIVINGMAP_SRCS=(
  "${LIVINGMAP_DIR}/creep.c"
  "${LIVINGMAP_DIR}/hex_grid.c"
  "${LIVINGMAP_DIR}/living_map_events.c"
  "${LIVINGMAP_DIR}/town.c"
  "${LIVINGMAP_DIR}/frontier_village_mod.c"
  "${LIVINGMAP_DIR}/walled_hamlet_mod.c"
)
LIVINGMAP_INCLUDES=(
  -include "${LIVINGMAP_DIR}/frontier_village_mod_host.h"
  -include "${LIVINGMAP_DIR}/walled_hamlet_mod_host.h"
)

for test_name in test_hex_grid test_town_frontier_village test_walled_hamlet test_creep; do
  gcc -std=c99 -O2 -Wall -Wextra -I"${ROOT_DIR}/packages" \
    "${LIVINGMAP_INCLUDES[@]}" \
    -o "${BUILD_DIR}/${test_name}" \
    "${ROOT_DIR}/tests/${test_name}.c" \
    "${LIVINGMAP_SRCS[@]}" \
    -lm
done

"${BUILD_DIR}/test_hex_grid"
"${BUILD_DIR}/test_town_frontier_village"
"${BUILD_DIR}/test_walled_hamlet"
"${BUILD_DIR}/test_creep"
