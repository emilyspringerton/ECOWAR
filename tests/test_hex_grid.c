/* tests/test_hex_grid.c -- headless smoke test for ECOWAR's Living Map hex grid
 * (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377). Same "no SDL/GL dependency" reasoning
 * as test_arena_game.c's own header comment -- this package has no rendering at all yet. */
#include <stdio.h>
#include <math.h>

#include "../packages/livingmap/hex_grid.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void test_init_fills_every_real_cell(void) {
    HexGrid grid;
    hex_grid_init(&grid);
    CHECK(grid.cell_count == HEX_MAP_CELL_COUNT, "init fills exactly HEX_MAP_CELL_COUNT real cells");

    HexCell *origin = hex_grid_cell_at(&grid, (HexCoord){0, 0});
    CHECK(origin != NULL, "the origin cell exists");
    CHECK(origin && origin->faction_owner == 0, "every cell starts neutral");
    CHECK(origin && origin->town_id == -1, "every cell starts with no town");
    CHECK(origin && origin->corruption == 0, "every cell starts with zero corruption");
}

static void test_out_of_range_coord_returns_null(void) {
    HexGrid grid;
    hex_grid_init(&grid);
    HexCoord far = {HEX_MAP_RADIUS * 3, HEX_MAP_RADIUS * 3};
    CHECK(hex_grid_cell_at(&grid, far) == NULL, "a coord far outside the map radius returns NULL, not a wild index");
    CHECK(hex_grid_index_for(&grid, far) == -1, "hex_grid_index_for reports -1 for the same out-of-range coord");
}

static void test_edge_of_radius_is_still_in_range(void) {
    HexGrid grid;
    hex_grid_init(&grid);
    HexCoord edge = {HEX_MAP_RADIUS, 0};
    CHECK(hex_grid_cell_at(&grid, edge) != NULL, "a coord exactly at the map's own hex radius is real and in range");
    HexCoord just_past = {HEX_MAP_RADIUS + 1, 0};
    CHECK(hex_grid_cell_at(&grid, just_past) == NULL, "one hex past the radius is out of range");
}

static void test_distance_is_symmetric_and_zero_at_self(void) {
    HexCoord a = {0, 0};
    HexCoord b = {3, -1};
    CHECK(hex_distance(a, a) == 0, "distance from a cell to itself is 0");
    CHECK(hex_distance(a, b) == hex_distance(b, a), "hex_distance is symmetric");
    CHECK(hex_distance(a, b) == 3, "a real, hand-checked axial distance (0,0)->(3,-1) is 3");
}

static void test_neighbors_are_each_distance_one_and_direction_wraps(void) {
    HexCoord center = {2, -2};
    for (int dir = 0; dir < 6; dir++) {
        HexCoord n = hex_neighbor(center, dir);
        if (hex_distance(center, n) != 1) {
            printf("FAIL: neighbor in direction %d is not distance 1 from center\n", dir);
            failures++;
        }
    }
    printf("PASS: all 6 neighbor directions are real, distance-1 cells\n");

    HexCoord n_neg = hex_neighbor(center, -1);
    HexCoord n_five = hex_neighbor(center, 5);
    CHECK(n_neg.q == n_five.q && n_neg.r == n_five.r, "a negative direction wraps mod 6 to the same neighbor as its positive equivalent");

    HexCoord n_six = hex_neighbor(center, 6);
    HexCoord n_zero = hex_neighbor(center, 0);
    CHECK(n_six.q == n_zero.q && n_six.r == n_zero.r, "direction 6 wraps back to direction 0");
}

static void test_world_roundtrip_recovers_the_same_hex(void) {
    float size = 4.0f;
    HexCoord original = {5, -3};
    float x, z;
    hex_axial_to_world(original, size, &x, &z);
    HexCoord recovered = hex_world_to_axial(x, z, size);
    CHECK(recovered.q == original.q && recovered.r == original.r,
          "converting a hex to world space and back recovers the exact same hex");
}

static void test_origin_is_at_world_zero(void) {
    float x, z;
    hex_axial_to_world((HexCoord){0, 0}, 4.0f, &x, &z);
    CHECK(fabsf(x) < 0.0001f && fabsf(z) < 0.0001f, "the (0,0) hex sits at world-space origin");
}

int main(void) {
    test_init_fills_every_real_cell();
    test_out_of_range_coord_returns_null();
    test_edge_of_radius_is_still_in_range();
    test_distance_is_symmetric_and_zero_at_self();
    test_neighbors_are_each_distance_one_and_direction_wraps();
    test_world_roundtrip_recovers_the_same_hex();
    test_origin_is_at_world_zero();
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
