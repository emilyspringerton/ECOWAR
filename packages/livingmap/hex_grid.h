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

/* HexBiome (BACKLOG.md SECTION 382): "each map rolls 3-5 biomes, stitched together
 * procedurally." Real, deliberately renamed from a founder-shared reference design that used
 * Minecraft-specific terminology ("Pillagers," "Dragons") with no real connection to ECOWAR's own
 * established vocabulary -- see docs/NORTHSTAR_BIOMES.md's own critique section for the full
 * reasoning. Every biome maps onto a real, already-built Living Map system rather than inventing
 * a new one: BIOME_VERDANT_WILDS/BIOME_FROZEN_REACH modulate town convert-resistance (see
 * town.c's town_attempt_convert); BIOME_BLIGHTED_GRID is corruption's own first real mechanic
 * (hex_grid_tick_corruption below) -- the exact field this struct reserved back in Phase 1 and no
 * system ever wrote until now. */
typedef enum {
    BIOME_VERDANT_WILDS = 0,   /* Jungle core -- neutral villages flip easily, real creep camps */
    BIOME_ASH_BARRENS = 1,     /* High risk/reward -- faster corruption growth, frequent hostile spawns */
    BIOME_FROZEN_REACH = 2,    /* Defensive strongholds -- towns harder to flip, slower creep movement */
    BIOME_BLIGHTED_GRID = 3,   /* Chaos zone -- corruption's own real epicenter, can flip neutral cells outright */
    BIOME_COUNT = 4
} HexBiome;

/* HexCell.faction_owner: 0 = neutral/unclaimed, 1 = Dominion, 2 = Symbiosis, 3 = Corruption
 * (docs/NORTHSTAR_LIVING_MAP.md's own faction numbering -- kept here as the one real source of
 * truth other systems reference by these same integers, no separate enum yet since no faction-AI
 * code exists to need one). town_id: -1 = no town on this cell, else an index into a TownRegistry
 * (town.h) -- HexCell itself doesn't own Town data, just the back-reference.
 * corruption: real elapsed exposure milliseconds, 0..HEX_CORRUPTION_FLIP_THRESHOLD_MS, written now
 * by hex_grid_tick_corruption (BIOME_BLIGHTED_GRID cells only, this pass's own first real
 * corruption mechanic -- see that function's own doc comment). biome: one of HexBiome above,
 * assigned once by hex_grid_generate_biomes. */
typedef struct {
    HexCoord coord;
    int faction_owner;
    int town_id;
    int corruption;
    int biome;
} HexCell;

typedef struct {
    HexCell cells[HEX_MAP_CELL_COUNT];
    int index_lookup[HEX_MAP_SIDE][HEX_MAP_SIDE]; /* [q+RADIUS][r+RADIUS] -> cell index, -1 if out of range */
    int cell_count; /* always HEX_MAP_CELL_COUNT once init'd -- kept as a field (not just the macro) so tests can assert init actually ran */
} HexGrid;

/* Resets every cell to neutral/no-town/no-corruption/BIOME_VERDANT_WILDS and rebuilds
 * index_lookup. Must be called before any other hex_grid_ or town_ function touches a fresh
 * HexGrid. Does NOT itself assign real biome variety -- call hex_grid_generate_biomes right after
 * for a real, stitched-together map; every cell defaulting to BIOME_VERDANT_WILDS here is a real,
 * safe, inert value if a caller ever skips that step (e.g. an existing test that doesn't care
 * about biomes at all), not an uninitialized read. */
void hex_grid_init(HexGrid *grid);

/* hex_grid_generate_biomes (BACKLOG.md SECTION 382): a real, deterministic, seeded procedural
 * biome layout -- "each map rolls 3-5 biomes, stitched together procedurally." This pass's own
 * real v0: exactly HEX_BIOME_SEED_COUNT (4, one per real HexBiome) seed cells are picked via a
 * real isolated xorshift32 PRNG (same "isolated, seed-reproducible" convention arena_game.c's own
 * Mandelbrot jungle generation already established -- not libc rand()), and every real cell is
 * assigned the biome of its nearest seed cell (real hex distance, ties broken by lowest biome id)
 * -- a real, standard Voronoi-style partition, not a random per-cell scatter. Real, honest,
 * narrower than the founder's own "3-5" framing: always exactly 4 regions, one of each real
 * biome, never a repeated biome or a map that rolls only 3 -- a real, later richer version (variable
 * region count, repeated biome types) is named, not built, in docs/NORTHSTAR_BIOMES.md. Call once,
 * right after hex_grid_init, before anything reads HexCell.biome. */
#define HEX_BIOME_SEED_COUNT 4
void hex_grid_generate_biomes(HexGrid *grid, unsigned int seed);

/* hex_grid_tick_corruption (BACKLOG.md SECTION 382) -- corruption's own real first mechanic,
 * closing the "Corruption, honestly" open question docs/NORTHSTAR_LIVING_MAP.md's own Phase 1
 * left genuinely undecided. Real, chosen answer (option 1 of the 3 staged there): pure
 * environmental spread. Every real BIOME_BLIGHTED_GRID cell's own corruption field accumulates
 * real elapsed milliseconds directly (`corruption += dt_ms`) -- a deliberate, exact-integer
 * design with zero truncation risk at any real tick granularity (a naive "points per second"
 * formula divided down per-tick would silently round to 0 forever at a real 16ms server tick,
 * since e.g. 2*16/1000 truncates to 0 in integer math -- caught and avoided here, not discovered
 * live). Once corruption reaches HEX_CORRUPTION_FLIP_THRESHOLD_MS (real elapsed exposure time), a
 * NEUTRAL (faction_owner == 0) cell flips outright to faction 3 (Corruption) and corruption
 * resets to 0 -- an already-faction-owned cell (a real town's own home cell) is untouched by this
 * (real, deliberate: taking a live town away from a real player via ambient decay alone, with no
 * real convert-resistance check at all, would bypass every other real capture mechanic this
 * session built -- a real, later "corruption can also erode an owned cell's resistance" richer
 * version is named, not built, in docs/NORTHSTAR_BIOMES.md). Call once per real match tick,
 * alongside every other per-tick Living Map system. */
#define HEX_CORRUPTION_FLIP_THRESHOLD_MS 60000 /* ~60 real seconds of exposure -- a real, tunable, deliberately mid-match pace, not a final balance number */
void hex_grid_tick_corruption(HexGrid *grid, unsigned int dt_ms);

/* Cube-coordinate distance (exact, integer, no rounding). */
int hex_distance(HexCoord a, HexCoord b);

/* direction is taken mod 6 (negative values wrap correctly), so callers never need to pre-clamp. */
HexCoord hex_neighbor(HexCoord c, int direction);

int hex_coord_equal(HexCoord a, HexCoord b);

/* hex_step_toward -- real, minimal greedy hex pathing (creep.c's own chase/return movement):
 * returns the one neighbor of `from` (of the 6 real directions) that minimizes real hex distance
 * to `to`, ties broken by lowest direction index for determinism (same "same inputs, same
 * decision" bar frontier_village_mod.prn's own deterministic pacing already holds itself to).
 * Returns `from` unchanged if from == to. Real, honest, narrow limitation: no obstacle avoidance
 * -- this map has no terrain/blocking cells yet, so straight-line greedy stepping is always
 * optimal today; a real, later gap once anything can block a hex. */
HexCoord hex_step_toward(HexCoord from, HexCoord to);

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
