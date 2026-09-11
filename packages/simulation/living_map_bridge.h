/* packages/simulation/living_map_bridge.h -- the real arena<->Living Map integration point
 * (BACKLOG.md SECTION 377 Phase 7). Founder real-time: "can we make sure we get these updates in
 * the client and the server? ... im not seeing frontier village ... i dont see a hex grid" --
 * every Living Map system built earlier this session (hex_grid/town/creep, packages/livingmap)
 * was real and tested but never connected to an actual running match. This file is that
 * connection: one real, live HexGrid/TownRegistry/CreepRegistry per match, founded at
 * arena_init/arena_init_teams, ticked every arena_update/arena_update_teams tick, and read by
 * apps/arena_server's own snapshot broadcast (protocol.h's ArenaSnapshotLivingMapMsg) so a real
 * client can finally render it.
 *
 * Module-level state (like arena_bot_enabled), not part of ArenaState -- deliberately kept
 * separate so packages/livingmap itself never has to know arena_game.h exists (the dependency
 * runs only this direction: this file depends on both, neither of the two systems it connects
 * depends on the other).
 */
#ifndef LIVING_MAP_BRIDGE_H
#define LIVING_MAP_BRIDGE_H

#include "arena_game.h"
#include "../livingmap/hex_grid.h"
#include "../livingmap/town.h"
#include "../livingmap/creep.h"

/* Bounded, wire-sized caps for apps/arena_server's own snapshot broadcast (protocol.h's
 * ArenaSnapshotLivingMapMsg uses the same numbers, duplicated there on purpose -- protocol.h
 * deliberately never includes packages/simulation or packages/livingmap headers, same
 * "network layer doesn't depend on the simulation layer" discipline ARENA_SNAPSHOT_SHOP_COUNT
 * etc. already established). A real, honest limit, not a crash: any town/creep beyond these caps
 * still exists and is simulated correctly, it just isn't sent over the wire this pass. */
#define LIVING_MAP_BRIDGE_MAX_SYNC_TOWNS 8
#define LIVING_MAP_BRIDGE_MAX_SYNC_CREEPS 16

/* Founds a fresh, real Living Map for a new match: a hex grid sized to fit within
 * ARENA_HALF_EXTENT, a handful of real towns (2 pre-owned by each of the 2 real player sides, 2
 * neutral/contestable), and a few real, wandering cows. Call once, right after
 * arena_init_with_heroes/arena_init_teams resets everything else -- both already do. Safe to call
 * from every existing test in this codebase that calls those two functions: this resets a small,
 * separate module-level instance nothing else reads unless something explicitly queries this
 * file's own accessors below. */
void living_map_bridge_init_match(void);

/* Advances every real town and creep by dt_ms. Call once per real arena_update/
 * arena_update_teams tick. Cannot itself set arena_state.winner or otherwise affect combat --
 * towns/creeps only ever change ownership via town_attempt_convert, which nothing in this file
 * (or anywhere in a live match yet) calls automatically, so this is safe to tick unconditionally
 * in every existing test without risking a spurious side effect. */
void living_map_bridge_tick(unsigned int dt_ms);

/* Returns the real Living Map faction (1..3) that currently owns every active town in this
 * match, or 0 if no one does yet (see town_registry_faction_has_full_control's own doc comment).
 * Does NOT set arena_state.winner itself -- the caller (arena_update/arena_update_teams) decides
 * what that means for the match overall, same "tick computes, caller decides" split every other
 * subsystem in arena_game.c already uses. */
int living_map_bridge_full_control_faction(void);

/* Maps a real Living Map faction id (1 or 2) back to the real "side" (owner in 1v1, team in team
 * mode -- both are 0/1) that faction belongs to. Returns -1 for faction 3 (Corruption): no real
 * player maps to it in a 2-sided match today, a real, honest, named limit -- see
 * living_map_bridge_init_match's own .c-side doc comment for the current 1=Dominion/2=Symbiosis
 * assignment. */
int living_map_bridge_faction_to_owner(int faction);

/* Read-only accessors for apps/arena_server's own snapshot broadcast. index must be in
 * [0, living_map_bridge_town_count())/[0, living_map_bridge_creep_count()) -- no bounds
 * checking here, same "caller already knows the real count" convention arena_state.heroes[]
 * array access uses throughout this codebase. */
int living_map_bridge_town_count(void); /* real count, capped at LIVING_MAP_BRIDGE_MAX_SYNC_TOWNS */
void living_map_bridge_town_world_pos(int index, float *out_x, float *out_z);
int living_map_bridge_town_type(int index);
int living_map_bridge_town_faction_owner(int index);
int living_map_bridge_town_population(int index);
int living_map_bridge_town_militia(int index);

int living_map_bridge_creep_count(void); /* real count, capped at LIVING_MAP_BRIDGE_MAX_SYNC_CREEPS */
void living_map_bridge_creep_world_pos(int index, float *out_x, float *out_z);
int living_map_bridge_creep_faction_owner(int index);
int living_map_bridge_creep_alive(int index);

/* living_map_bridge_spawn_hostile_creep_at_map_center -- Bloodflower's own "for now" real
 * behavior (EMILY/BACKLOG.md SECTION 380, founder: "for now just have it spawn a bunch of
 * hostile creeps into the map at exactly the midpoint"). Spawns one real, aggressive
 * LivingMapCreep at hex (0,0) (the map's real midpoint -- ArenaState.bloodflower_x/z are always
 * (0,0) too, see arena_game.h's own doc comment), owned by Living Map faction 3 (Corruption) --
 * its first real, live use anywhere in this codebase (every other Living Map system so far only
 * ever assigns faction 1/2/neutral). Hostile to every real player faction (1 and 2) via the same
 * faction-inequality aggro rule every other creep already uses -- no special-casing needed. The
 * real HOST-side caller (packages/simulation/arena_game.c's own
 * ecowar_tick_bloodflower_hostile_spawner) calls this once per creep
 * PARENA/stdlib/ecowar/bloodflower_hostile_spawner_mod.prn's own on-bloodflower-hostile-spawner-
 * creep-count names, in a real loop -- this function itself only ever spawns exactly one. Returns
 * the new creep's id, or -1 if the registry is full (same "real, honest limit, not a crash"
 * convention living_map_creep_spawn's own doc comment already uses). */
int living_map_bridge_spawn_hostile_creep_at_map_center(void);

/* living_map_bridge_faction_owned_count / _real_town_count (EMILY/BACKLOG.md SECTION 381, "even
 * the win con should be mods"): the real counts ecowar_tick_allcap_win_check (arena_game.c) feeds
 * into ecowar/allcap_mod.prn's own on-allcap-check. _real_town_count is the true total (unlike
 * living_map_bridge_town_count() above, which is capped at LIVING_MAP_BRIDGE_MAX_SYNC_TOWNS for
 * wire-sync purposes only). */
int living_map_bridge_faction_owned_count(int faction_owner);
int living_map_bridge_real_town_count(void);

/* living_map_bridge_attempt_convert_town -- the real, live entry point for "capture a node"
 * (docs/NORTHSTAR_LIVING_MAP.md's own Phase 2 section, "a card could literally be capture a node
 * - you drag it on pay the resource and it flips the base"). Wraps town_attempt_convert
 * (packages/livingmap/town.h) against this match's own real TownRegistry/HexGrid/event log --
 * town_index is a plain registry index (0..living_map_bridge_real_town_count()-1), NOT a
 * wire-capped snapshot index. Real, current caller: tests/test_allcap.c (proving the ALLCAP win
 * chain end to end); the real "capture a node" card itself is still real, separate, not-yet-built
 * work -- this function is what it will call once it exists. Returns 1 if this call caused a real
 * flip (same real return-value contract town_attempt_convert itself uses), 0 otherwise. */
int living_map_bridge_attempt_convert_town(int town_index, int attacking_faction, int attempt_strength);

#endif /* LIVING_MAP_BRIDGE_H */
