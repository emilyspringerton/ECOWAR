/* packages/livingmap/hex_grid.h -- axial-coordinate hex grid for ECOWAR's Living Map
 * (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377). Founder: "the entire map is divided
 * into cells (hex grid) - so we can start to get the living map stuff more formalized."
 *
 * Standard flat-top hex math (redblobgames' well-known formulas, not reinvented): axial (q, r)
 * coordinates, cube-coordinate distance/rounding. This is a NEW, standalone strategic layer over
 * the whole map -- it does not touch or replace arena_game.c's own ArenaNode system (see the
 * NORTHSTAR doc's "this is a new layer, not an extension of the arena map" section for why).
 *
 * The grid is a fixed hex-radius-HEX_MAP_RADIUS map (a big hex made of hexes). index_lookup is a
 * plain (2R+1)x(2R+1) table (a few hundred ints of overhead) rather than an analytic ring-index
 * formula -- simple, obviously correct, and cheap enough at this radius; a real, deliberate
 * "narrowest correct thing" choice, not a placeholder for something smarter later.
 */
#ifndef LIVINGMAP_HEX_GRID_H
#define LIVINGMAP_HEX_GRID_H

#define HEX_MAP_RADIUS 12
#define HEX_MAP_SIDE (2 * HEX_MAP_RADIUS + 1)
#define HEX_MAP_CELL_COUNT (3 * HEX_MAP_RADIUS * (HEX_MAP_RADIUS + 1) + 1)

typedef struct {
    int q, r;
} HexCoord;

/* HexCell.faction_owner: 0 = neutral/unclaimed, 1 = Dominion, 2 = Symbiosis, 3 = Corruption
 * (docs/NORTHSTAR_LIVING_MAP.md's own faction numbering -- kept here as the one real source of
 * truth other systems reference by these same integers, no separate enum yet since no faction-AI
 * code exists to need one). town_id: -1 = no town on this cell, else an index into a TownRegistry
 * (town.h) -- HexCell itself doesn't own Town data, just the back-reference.
 * corruption: reserved for the not-yet-decided corruption mechanic (NORTHSTAR doc's "Corruption,
 * honestly" section) -- always 0 today, no system writes it yet. */
typedef struct {
    HexCoord coord;
    int faction_owner;
    int town_id;
    int corruption;
} HexCell;

typedef struct {
    HexCell cells[HEX_MAP_CELL_COUNT];
    int index_lookup[HEX_MAP_SIDE][HEX_MAP_SIDE]; /* [q+RADIUS][r+RADIUS] -> cell index, -1 if out of range */
    int cell_count; /* always HEX_MAP_CELL_COUNT once init'd -- kept as a field (not just the macro) so tests can assert init actually ran */
} HexGrid;

/* Resets every cell to neutral/no-town/no-corruption and rebuilds index_lookup. Must be called
 * before any other hex_grid_ or town_ function touches a fresh HexGrid. */
void hex_grid_init(HexGrid *grid);

/* Cube-coordinate distance (exact, integer, no rounding). */
int hex_distance(HexCoord a, HexCoord b);

/* direction is taken mod 6 (negative values wrap correctly), so callers never need to pre-clamp. */
HexCoord hex_neighbor(HexCoord c, int direction);

/* -1 if c falls outside the map's hex-radius-HEX_MAP_RADIUS boundary. */
int hex_grid_index_for(const HexGrid *grid, HexCoord c);

/* NULL if c falls outside the map. */
HexCell *hex_grid_cell_at(HexGrid *grid, HexCoord c);
const HexCell *hex_grid_cell_at_const(const HexGrid *grid, HexCoord c);

/* World-space <-> hex conversion, flat-top orientation. hex_size is the distance from a hex's
 * center to any of its 6 corners (the arena's existing float x/z coordinate space is the target;
 * hex_size is left caller-tunable rather than hardcoded against ARENA_HALF_EXTENT, since no real
 * system ties the two together yet -- see the NORTHSTAR doc). */
void hex_axial_to_world(HexCoord c, float hex_size, float *out_x, float *out_z);
HexCoord hex_world_to_axial(float x, float z, float hex_size);

#endif /* LIVINGMAP_HEX_GRID_H */
