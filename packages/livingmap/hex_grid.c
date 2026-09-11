/* packages/livingmap/hex_grid.c -- see hex_grid.h for the real design rationale. */
#include "hex_grid.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

static const int HEX_DIR_Q[6] = {1, 1, 0, -1, -1, 0};
static const int HEX_DIR_R[6] = {0, -1, -1, 0, 1, 1};

void hex_grid_init(HexGrid *grid) {
    memset(grid, 0, sizeof(*grid));
    for (int i = 0; i < HEX_MAP_SIDE; i++) {
        for (int j = 0; j < HEX_MAP_SIDE; j++) {
            grid->index_lookup[i][j] = -1;
        }
    }

    int idx = 0;
    for (int q = -HEX_MAP_RADIUS; q <= HEX_MAP_RADIUS; q++) {
        int r_min = -HEX_MAP_RADIUS;
        if (-q - HEX_MAP_RADIUS > r_min) r_min = -q - HEX_MAP_RADIUS;
        int r_max = HEX_MAP_RADIUS;
        if (-q + HEX_MAP_RADIUS < r_max) r_max = -q + HEX_MAP_RADIUS;
        for (int r = r_min; r <= r_max; r++) {
            HexCoord c = {q, r};
            grid->cells[idx].coord = c;
            grid->cells[idx].faction_owner = 0;
            grid->cells[idx].town_id = -1;
            grid->cells[idx].corruption = 0;
            grid->index_lookup[q + HEX_MAP_RADIUS][r + HEX_MAP_RADIUS] = idx;
            idx++;
        }
    }
    grid->cell_count = idx;
}

int hex_distance(HexCoord a, HexCoord b) {
    int dq = a.q - b.q;
    int dr = a.r - b.r;
    int adq = dq < 0 ? -dq : dq;
    int adr = dr < 0 ? -dr : dr;
    int adqr = (dq + dr) < 0 ? -(dq + dr) : (dq + dr);
    return (adq + adqr + adr) / 2;
}

int hex_coord_equal(HexCoord a, HexCoord b) {
    return a.q == b.q && a.r == b.r;
}

HexCoord hex_step_toward(HexCoord from, HexCoord to) {
    if (hex_coord_equal(from, to)) return from;

    HexCoord best = from;
    int best_dist = -1;
    for (int dir = 0; dir < 6; dir++) {
        HexCoord candidate = hex_neighbor(from, dir);
        int d = hex_distance(candidate, to);
        if (best_dist == -1 || d < best_dist) {
            best_dist = d;
            best = candidate;
        }
    }
    return best;
}

HexCoord hex_neighbor(HexCoord c, int direction) {
    int dir = direction % 6;
    if (dir < 0) dir += 6;
    HexCoord n;
    n.q = c.q + HEX_DIR_Q[dir];
    n.r = c.r + HEX_DIR_R[dir];
    return n;
}

int hex_grid_index_for(const HexGrid *grid, HexCoord c) {
    int qi = c.q + HEX_MAP_RADIUS;
    int ri = c.r + HEX_MAP_RADIUS;
    if (qi < 0 || qi >= HEX_MAP_SIDE || ri < 0 || ri >= HEX_MAP_SIDE) return -1;
    return grid->index_lookup[qi][ri];
}

HexCell *hex_grid_cell_at(HexGrid *grid, HexCoord c) {
    int idx = hex_grid_index_for(grid, c);
    if (idx < 0) return NULL;
    return &grid->cells[idx];
}

const HexCell *hex_grid_cell_at_const(const HexGrid *grid, HexCoord c) {
    int idx = hex_grid_index_for(grid, c);
    if (idx < 0) return NULL;
    return &grid->cells[idx];
}

void hex_axial_to_world(HexCoord c, float hex_size, float *out_x, float *out_z) {
    *out_x = hex_size * (1.5f * (float)c.q);
    *out_z = hex_size * (sqrtf(3.0f) * 0.5f * (float)c.q + sqrtf(3.0f) * (float)c.r);
}

static HexCoord hex_round(float qf, float rf) {
    float xf = qf, zf = rf, yf = -xf - zf;
    float rx = roundf(xf), ry = roundf(yf), rz = roundf(zf);

    float x_diff = fabsf(rx - xf);
    float y_diff = fabsf(ry - yf);
    float z_diff = fabsf(rz - zf);

    if (x_diff > y_diff && x_diff > z_diff) {
        rx = -ry - rz;
    } else if (y_diff > z_diff) {
        ry = -rx - rz;
    } else {
        rz = -rx - ry;
    }

    HexCoord result;
    result.q = (int)rx;
    result.r = (int)rz;
    return result;
}

HexCoord hex_world_to_axial(float x, float z, float hex_size) {
    float qf = (2.0f / 3.0f * x) / hex_size;
    float rf = (-1.0f / 3.0f * x + sqrtf(3.0f) / 3.0f * z) / hex_size;
    return hex_round(qf, rf);
}
