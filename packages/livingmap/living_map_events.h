/* packages/livingmap/living_map_events.h -- the real event-announcement layer for ECOWAR's
 * Living Map (docs/NORTHSTAR_LIVING_MAP.md's own "Mod event model, honestly" section).
 *
 * Founder: "everything that happens in the game needs to announce events and then mods can
 * subscribe to those events." Checked directly against VS0's real, current emitter: no function
 * pointers/closures exist, so a true dynamic multi-subscriber registration table isn't buildable
 * today -- see the NORTHSTAR doc for the full, honest accounting. What IS real and shipped here:
 * an always-on, append-only ring buffer every lifecycle transition writes into unconditionally,
 * same shape as redgarden/combat_log_mod.prn's own ArenaCombatLogEntry -- any future mod, tool,
 * or the eventual map editor can read exactly what happened, in order, without the host needing
 * to know who's reading. Mod CALLBACKS (the other half of "register functions to be called") are
 * the existing, real, call-by-name convention every mod in this repo already uses -- see town.c's
 * calls into the compiled frontier_village_mod for the live example.
 */
#ifndef LIVINGMAP_EVENTS_H
#define LIVINGMAP_EVENTS_H

typedef enum {
    LIVING_MAP_EVENT_TOWN_FOUNDED = 0,
    LIVING_MAP_EVENT_TOWN_TICK,
    LIVING_MAP_EVENT_UNIT_SPAWNED,
    LIVING_MAP_EVENT_CONVERT_ATTEMPT,
    LIVING_MAP_EVENT_TOWN_CONVERTED,
    LIVING_MAP_EVENT_KIND_COUNT
} LivingMapEventKind;

/* a/b are event-specific payload, plain ints (no struct/Vec crossing into anything PARENA-side
 * reads this from later, matching VS0's own scalar-only ABI):
 *   TOWN_FOUNDED:     a = town_type, b = faction_owner
 *   TOWN_TICK:        a = population, b = militia (post-tick values)
 *   UNIT_SPAWNED:     a = 0 (peasant) or 1 (militia), b = unused (0)
 *   CONVERT_ATTEMPT:  a = attacking_faction, b = attempt_strength
 *   TOWN_CONVERTED:   a = old_faction_owner, b = new_faction_owner
 */
typedef struct {
    LivingMapEventKind kind;
    int town_id;
    int a;
    int b;
} LivingMapEvent;

#define LIVING_MAP_EVENT_LOG_CAPACITY 256

typedef struct {
    LivingMapEvent events[LIVING_MAP_EVENT_LOG_CAPACITY];
    int total_emitted; /* every emit ever, even past capacity -- distinguishes "log is full" from "nothing happened yet" */
} LivingMapEventLog;

void living_map_event_log_reset(LivingMapEventLog *log);
void living_map_emit(LivingMapEventLog *log, LivingMapEventKind kind, int town_id, int a, int b);

/* Number of events currently retained (min(total_emitted, CAPACITY)). */
int living_map_event_log_size(const LivingMapEventLog *log);

/* index 0 = oldest still-retained event, living_map_event_log_size()-1 = most recent. NULL if
 * index is out of that range. */
const LivingMapEvent *living_map_event_log_at(const LivingMapEventLog *log, int index);

#endif /* LIVINGMAP_EVENTS_H */
