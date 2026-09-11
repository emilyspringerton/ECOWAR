/* packages/livingmap/town.h -- Town struct + registry for ECOWAR's Living Map
 * (docs/NORTHSTAR_LIVING_MAP.md, BACKLOG.md SECTION 377).
 *
 * TownType names all 4 real town types from the founder's own list. TOWN_TYPE_FRONTIER_VILLAGE
 * and TOWN_TYPE_WALLED_HAMLET both have real behavior in town_tick/town_attempt_convert below --
 * the remaining 2 are declared so the rest of this codebase (and whoever builds them next) has
 * one real enum to extend, not stub structs pretending to be implemented. See the NORTHSTAR doc's
 * own per-type table for what each one still needs.
 */
#ifndef LIVINGMAP_TOWN_H
#define LIVINGMAP_TOWN_H

#include "hex_grid.h"
#include "living_map_events.h"
#include "creep.h"

typedef enum {
    TOWN_TYPE_FRONTIER_VILLAGE = 0,   /* Spawns peasants -> militia. Avoids conflict. Converts easily. -- BUILT */
    TOWN_TYPE_WALLED_HAMLET = 1,      /* Defensive bias. Shoots hostile creeps. Slow to flip. -- BUILT */
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
    int population;          /* peasants (Frontier Village) or townsfolk (Walled Hamlet) */
    int militia;             /* raised defenders -- peasant militia (Frontier Village) or hamlet garrison (Walled Hamlet) */
    int spawn_timer_ms;      /* counts down to 0, then a spawn tick fires and it's reset */
    int conversion_progress; /* 0..100 toward the current attacking_faction; resets to 0 on any successful convert, or when a different faction attacks mid-attempt */
    int converting_faction;  /* the faction conversion_progress is currently accumulating against; meaningless (ignored) while conversion_progress == 0 */
    int defense_cooldown_ms; /* Walled Hamlet only ("shoots hostile creeps") -- meaningless (ignored, never read) for every other type */
    int militia_bonus;       /* extra militia/garrison raised per spawn tick that actually raises one -- see town_apply_militia_boost */
} Town;

/* town_apply_militia_boost caps militia_bonus here -- a real, deliberate balancing bound (not a
 * functional requirement): uncapped stacking would let a single card, played enough times, spawn
 * an unbounded wave of militia in one tick. 20 is a round, generous ceiling well above any
 * realistic single-match play count, not a tuned final balance number. */
#define TOWN_MILITIA_BONUS_CAP 20

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

/* Advances one town by dt_ms. TOWN_TYPE_FRONTIER_VILLAGE and TOWN_TYPE_WALLED_HAMLET both have
 * real population/militia growth this pass (see each type's own real PARENA mod) -- the remaining
 * 2 types are a documented no-op, not a crash or a silently-wrong default. Emits
 * LIVING_MAP_EVENT_TOWN_TICK (always) and LIVING_MAP_EVENT_UNIT_SPAWNED (only on a tick that
 * actually raises population/militia). No-op (does nothing, emits nothing) if town_id is inactive
 * or out of range. Does NOT include Walled Hamlet's own creep defense -- see
 * town_tick_with_creeps below for that. */
void town_tick(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log, int town_id, unsigned int dt_ms);

/* Real conversion model: attempt_strength accumulates into conversion_progress once it clears the
 * town's own real resistance value (Frontier Village and Walled Hamlet both compute a real one
 * today, Walled Hamlet's deliberately much higher -- "slow to flip" -- every other type falls
 * back to a fixed placeholder resistance of 100, i.e. effectively unconvertible, until its own
 * real mod lands -- named, not silently made convertible). Crossing 100 flips faction_owner (both
 * the Town and its home HexCell) and resets conversion_progress to 0. Switching which faction is
 * attacking resets progress to 0 first (no progress carries over between different attackers).
 * Always emits LIVING_MAP_EVENT_CONVERT_ATTEMPT; emits LIVING_MAP_EVENT_TOWN_CONVERTED only on an
 * actual flip. Returns 1 if this call caused a flip, 0 otherwise (including "town_id invalid" and
 * "attacking_faction already owns this town"). */
int town_attempt_convert(TownRegistry *reg, HexGrid *grid, LivingMapEventLog *log,
                          int town_id, int attacking_faction, int attempt_strength);

/* town_tick_with_creeps -- same as town_tick, plus Walled Hamlet's own real "shoots hostile
 * creeps" defense: once per real defense cooldown, the town fires on the nearest active, alive,
 * different-faction creep within its own real, garrison-scaled defense range (on-walled-hamlet-
 * defense-range/-damage), dealing real damage and killing it outright if that drops its hp to 0
 * or below (LIVING_MAP_EVENT_CREEP_KILLED with a=-1, distinguishing "killed by a town" from
 * "killed by another creep" -- see living_map_events.h). A whiffed cooldown (nothing in range)
 * spends no cooldown, same "no free swing, but no wasted resource either" convention
 * card_effect_mod.prn's own host callers already hold themselves to. A plain, generic
 * town_tick(...) call (no CreepRegistry in scope, or a scenario that doesn't care about defense)
 * is still completely valid for every town type including Walled Hamlet -- economy/conversion
 * behavior never depends on this function having been called. No-op beyond the plain town_tick
 * effects for every type except Walled Hamlet. */
void town_tick_with_creeps(TownRegistry *reg, HexGrid *grid, CreepRegistry *creeps,
                            LivingMapEventLog *log, int town_id, unsigned int dt_ms);

/* town_apply_militia_boost -- "cards should be able to tie into stuff" (founder real-time,
 * BACKLOG.md SECTION 377 continued): the real Living Map-side mechanic behind a future card that
 * increases the number of militia/garrison a town raises per spawn tick, by +1, stacking
 * (clamped at TOWN_MILITIA_BONUS_CAP). Applies to whichever raise-a-defender path the town's own
 * type uses (Frontier Village's militia, Walled Hamlet's garrison) -- both read militia_bonus the
 * same way. No-op if town_id is inactive or out of range. Deliberately takes no faction/caster
 * argument and does not know anything about cards, PARENA, or arena_game.c's own card system --
 * this is the single, narrow entry point a future caller (once ECOWAR's live match loop actually
 * runs a Living Map instance to target) calls once per real, resolved card cast. That live
 * wiring does not exist yet -- no running match today initializes a HexGrid/TownRegistry at all
 * (Phase 1/2 are headless, see docs/NORTHSTAR_LIVING_MAP.md) -- so this function is real and
 * fully tested, but nothing in a live game calls it yet. */
void town_apply_militia_boost(TownRegistry *reg, int town_id);

/* town_registry_faction_has_full_control -- ECOWAR's base game-mode win condition (founder
 * real-time: "the wincon for ECOWAR - cap all of the control points - thats the base game mode",
 * a deliberate contrast with REDGARDEN's own pace/comeback-at-the-end dynamic). Returns 1 if
 * faction_owner owns EVERY active town in the registry, 0 otherwise -- including when there are
 * no active towns at all (an empty board is not a "win," it just hasn't started). faction_owner
 * 0 (neutral) can never "win" this way even if every town happened to be neutral -- 0 is treated
 * as a real, ordinary faction value everywhere else in this package (HexCell/Town/creep.h all
 * already do the same equality-based comparison), so this is a deliberate, narrow exception
 * documented here rather than silently inconsistent. Real, honest, deliberately NOT decided
 * here: whether "control points" means every founded Town (this function's own real answer) or
 * every one of the map's 469 hex cells including empty terrain -- the founder's own "cap all of
 * the control points" reads as the former (towns ARE the control points), matching the existing
 * Frontier Village/Walled Hamlet precedent where only a town's own home cell ever changes
 * faction_owner; a future "empty cells matter too" ruling would need a different function, not a
 * silent change to this one's meaning. */
int town_registry_faction_has_full_control(const TownRegistry *reg, int faction_owner);

/* town_registry_owned_count -- how many active towns faction_owner currently owns (EMILY/
 * BACKLOG.md SECTION 381). Real, host-side counting -- VS0's own scalar-only ABI can't loop over
 * structured TownRegistry data, so this real loop stays C; ecowar/allcap_mod.prn's own
 * on-allcap-check receives the resulting counts as plain I32 params, matching the real
 * "PARENA does the decision rule, host does the structural work" split every mod in this repo
 * already uses. */
int town_registry_owned_count(const TownRegistry *reg, int faction_owner);

/* town_registry_active_count -- how many towns are active in total (both convertible AND
 * neutral/any faction) -- the real denominator on-allcap-check's own total_count parameter needs.
 * Distinct from living_map_bridge_town_count(), which is capped for wire-sync purposes. */
int town_registry_active_count(const TownRegistry *reg);

#endif /* LIVINGMAP_TOWN_H */
