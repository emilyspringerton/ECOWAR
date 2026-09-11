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
    /* Creep system (BACKLOG.md SECTION 377 Phase 2, creep.h/.c) -- real classic-RTS aggro/chase/
     * leash/reset, subject_id below is a creep id for all 5 of these, NOT a town id. */
    LIVING_MAP_EVENT_CREEP_SPAWNED,
    LIVING_MAP_EVENT_CREEP_AGGRO,
    LIVING_MAP_EVENT_CREEP_ATTACK,
    LIVING_MAP_EVENT_CREEP_KILLED,
    LIVING_MAP_EVENT_CREEP_RESET,
    /* A passive creep (a cow) actually moved one hex while wandering -- never fires for an
     * aggressive creep, which only ever moves via CHASING/RETURNING (already covered by
     * CREEP_AGGRO/CREEP_ATTACK/CREEP_RESET). */
    LIVING_MAP_EVENT_CREEP_WANDERED,
    /* Walled Hamlet's own "shoots hostile creeps" -- subject_id is a TOWN id here (the shooter),
     * unlike every CREEP_* event above. */
    LIVING_MAP_EVENT_TOWN_DEFENSE_FIRE,
    LIVING_MAP_EVENT_KIND_COUNT
} LivingMapEventKind;

/* a/b are event-specific payload, plain ints (no struct/Vec crossing into anything PARENA-side
 * reads this from later, matching VS0's own scalar-only ABI). subject_id's own meaning
 * (town id vs. creep id) varies by kind -- see each kind's own comment above and below:
 *   TOWN_FOUNDED:      subject=town_id.  a = town_type, b = faction_owner
 *   TOWN_TICK:         subject=town_id.  a = population, b = militia (post-tick values)
 *   UNIT_SPAWNED:      subject=town_id.  a = 0 (peasant/townsfolk) or 1 (militia/garrison),
 *                       b = how many were actually raised this tick (always 1 for a=0; for a=1,
 *                       1 + the town's own real militia_bonus, see town_apply_militia_boost)
 *   CONVERT_ATTEMPT:   subject=town_id.  a = attacking_faction, b = attempt_strength
 *   TOWN_CONVERTED:    subject=town_id.  a = old_faction_owner, b = new_faction_owner
 *   CREEP_SPAWNED:     subject=creep_id. a = faction_owner, b = max_hp
 *   CREEP_AGGRO:       subject=creep_id. a = target creep_id, b = unused (0)
 *   CREEP_ATTACK:      subject=creep_id (attacker). a = target creep_id, b = damage dealt
 *   CREEP_KILLED:      subject=creep_id (the one killed). a = killer creep_id, or -1 if killed by
 *                       a town's defense fire (see TOWN_DEFENSE_FIRE). b = unused (0)
 *   CREEP_RESET:       subject=creep_id. a = unused (0), b = unused (0) -- back home, full hp
 *   CREEP_WANDERED:    subject=creep_id. a = new q, b = new r (the cow's real post-move HexCoord)
 *   TOWN_DEFENSE_FIRE: subject=town_id (the shooter). a = target creep_id, b = damage dealt
 */
typedef struct {
    LivingMapEventKind kind;
    int subject_id;
    int a;
    int b;
} LivingMapEvent;

#define LIVING_MAP_EVENT_LOG_CAPACITY 256

typedef struct {
    LivingMapEvent events[LIVING_MAP_EVENT_LOG_CAPACITY];
    int total_emitted; /* every emit ever, even past capacity -- distinguishes "log is full" from "nothing happened yet" */
} LivingMapEventLog;

void living_map_event_log_reset(LivingMapEventLog *log);
void living_map_emit(LivingMapEventLog *log, LivingMapEventKind kind, int subject_id, int a, int b);

/* Number of events currently retained (min(total_emitted, CAPACITY)). */
int living_map_event_log_size(const LivingMapEventLog *log);

/* index 0 = oldest still-retained event, living_map_event_log_size()-1 = most recent. NULL if
 * index is out of that range. */
const LivingMapEvent *living_map_event_log_at(const LivingMapEventLog *log, int index);

#endif /* LIVINGMAP_EVENTS_H */
