#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
LIVINGMAP_DIR="${ROOT_DIR}/packages/livingmap"
REFLUX_DIR="${ROOT_DIR}/packages/reflux"

mkdir -p "${BUILD_DIR}"

# Living Map (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377): the new hex-grid/town/creep
# strategic layer, deliberately its own standalone package (packages/livingmap), not folded into
# scripts/test_arena.sh's own arena_game mod list -- this layer has no dependency on arena_game.c
# at all. Headless on purpose, same reasoning test_arena.sh's own header comment gives.
#
# town_cap_mod.c/reflux_runtime.c added 2026-09-11 (real CI break found live, same session as the
# ci.yml Windows-cross-compile glob fix above it in git history): SECTION 381 (ALLCAP) made
# town.c's own town_attempt_convert call on_town_captured (town_cap_mod.c) unconditionally, which
# itself calls reflux_dispatch (packages/reflux/reflux_runtime.c) -- this script's own file list
# was never updated when that landed, so every test_* binary below failed to link (undefined
# reference to on_town_captured, then to reflux_dispatch) from that commit onward. Confirmed via a
# local repro against the clean committed tree, not assumed.
LIVINGMAP_SRCS=(
  "${LIVINGMAP_DIR}/creep.c"
  "${LIVINGMAP_DIR}/hex_grid.c"
  "${LIVINGMAP_DIR}/living_map_events.c"
  "${LIVINGMAP_DIR}/town.c"
  "${LIVINGMAP_DIR}/frontier_village_mod.c"
  "${LIVINGMAP_DIR}/walled_hamlet_mod.c"
  "${LIVINGMAP_DIR}/town_cap_mod.c"
  "${REFLUX_DIR}/reflux_runtime.c"
  "${REFLUX_DIR}/reflux_mod.c"
)
LIVINGMAP_INCLUDES=(
  -include "${LIVINGMAP_DIR}/frontier_village_mod_host.h"
  -include "${LIVINGMAP_DIR}/walled_hamlet_mod_host.h"
  -include "${LIVINGMAP_DIR}/town_cap_mod_host.h"
)

for test_name in test_hex_grid test_town_frontier_village test_walled_hamlet test_creep test_cow; do
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
"${BUILD_DIR}/test_cow"
