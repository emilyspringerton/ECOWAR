/* packages/livingmap/town.h -- Town struct + registry for ECOWAR's Living Map
 * (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377).
 *
 * TownType names all 4 real town types from the founder's own list, but only
 * TOWN_TYPE_FRONTIER_VILLAGE has real behavior in town_tick/town_attempt_convert below -- the
 * other 3 are declared so the rest of this codebase (and whoever builds them next) has one real
 * enum to extend, not stub structs pretending to be implemented. See the NORTHSTAR doc's own
 * per-type table for what each one still needs.
 */
#ifndef LIVINGMAP_TOWN_H
#define LIVINGMAP_TOWN_H

#include "hex_grid.h"
#include "living_map_events.h"

typedef enum {
    TOWN_TYPE_FRONTIER_VILLAGE = 0,   /* Spawns peasants -> militia. Avoids conflict. Converts easily. -- BUILT */
    TOWN_TYPE_WALLED_HAMLET = 1,      /* Defensive bias. Shoots hostile creeps. Slow to flip. -- named only */
    TOWN_TYPE_JUNGLE_ENCLAVE = 2,     /* Symbiotic with creeps. Spawns hunters. Expands naturally. -- named only */
    TOWN_TYPE_BLIGHTED_SETTLEMENT = 3 /* Corrupted over time. Spawns cultists. Unstable, explosive. -- named only */
} TownType;

#define TOWN_MAX_COUNT 64

/* faction_owner: 0 = neutral/unaligned, 1 = Dominion, 2 = Symbiosis, 3 = Corruption -- same
 * numbering as HexCell.faction_owner (hex_grid.h), since a town's owner and its home cell's
 * owner are always kept in sync by town_found/town_attempt_convert. */
typedef struct {
    int active;
    TownType type;
    HexCoord coord;
    int faction_owner;
    int population;          /* peasants */
    int militia;             /* raised from peasants -- see on-frontier-village-should-raise-militia */
    int spawn_timer_ms;      /* counts down to 0, then a spawn tick fires and it's reset */
    int conversion_progress; /* 0..100 toward the current attacking_faction; resets to 0 on any successful convert, or when a different faction attacks mid-attempt */
    int converting_faction;  /* the faction conversion_progress is currently accumulating against; meaningless (ignored) while conversion_progress == 0 */
} Town;

typedef struct {
    Town towns[TOWN_MAX_COUNT];
    int town_count; /* high-water mark of ever-founded towns -- town ids are never reused within one registry's lifetime, even after a hypothetical future "town destroyed" (not built yet) */
} TownRegistry;

void town_registry_init(TownRegistry *reg);

/* Founds a new town of the given type at coord, owned by faction_owner (0 = found it neutral).
 * Fails (-1) if coord is outside the hex map, already has a town, or the registry is full.
 * Emits LIVING_MAP_EVENT_TOWN_FOUNDED. Returns the new town's id on success. */
int town_found(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log,
               TownType type, HexCoord coord, int faction_owner);

/* Advances one town by dt_ms. Only TOWN_TYPE_FRONTIER_VILLAGE has real behavior today (see
 * hex_grid.h/town.h header comments) -- every other type is a documented no-op, not a crash or a
 * silently-wrong default. Emits LIVING_MAP_EVENT_TOWN_TICK (always) and
 * LIVING_MAP_EVENT_UNIT_SPAWNED (only on a tick that actually raises population/militia). No-op
 * (does nothing, emits nothing) if town_id is inactive or out of range. */
void town_tick(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log, int town_id, unsigned int dt_ms);

/* Real conversion model: attempt_strength accumulates into conversion_progress once it clears the
 * town's own real resistance value (only Frontier Village computes a real, low one today --
 * every other type falls back to a fixed placeholder resistance of 100, i.e. effectively
 * unconvertible, until its own real mod lands -- named, not silently made convertible). Crossing
 * 100 flips faction_owner (both the Town and its home HexCell) and resets conversion_progress to
 * 0. Switching which faction is attacking resets progress to 0 first (no progress carries over
 * between different attackers). Always emits LIVING_MAP_EVENT_CONVERT_ATTEMPT; emits
 * LIVING_MAP_EVENT_TOWN_CONVERTED only on an actual flip. Returns 1 if this call caused a flip,
 * 0 otherwise (including "town_id invalid" and "attacking_faction already owns this town"). */
int town_attempt_convert(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log,
                          int town_id, int attacking_faction, int attempt_strength);

#endif /* LIVINGMAP_TOWN_H */
