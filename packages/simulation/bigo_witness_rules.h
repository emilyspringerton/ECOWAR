/* Declarations for the PARENA-generated witness rules (core/witness_rules.c, generated from
 * PARENA/stdlib/big_o/witness_rules.prn -- do not edit the .c by hand). Spec: docs/B1_WITNESS_RULES.md. */
#ifndef BIGO_WITNESS_RULES_H
#define BIGO_WITNESS_RULES_H
enum { WS_UNAWARE = 0, WS_DENIAL, WS_COMPROMISED, WS_SILENCING, WS_PANIC, WS_ENGAGE };
enum { DA_SMALL_TALK = 0, DA_SAY_APOCALYPSE, DA_CARRY_GEAR, DA_WRONG_COSTUME, DA_ATTRIBUTED_EVENT, DA_QUIET_TICK,
       DA_CAUGHT_SNOOPING /* NORTHSTAR.md §39: a bystander saw you reading someone's screen */ };
enum { BAND_OK = 0, BAND_SUSPICION, BAND_HYSTERIC, BAND_CANCELLED };
enum { COS_SUIT = 0, COS_LAB_SMOCK, COS_JANITOR, COS_STREET };
enum { ZONE_PUBLIC = 0, ZONE_LAB, ZONE_EXEC, ZONE_GENERATOR, ZONE_VAULT };
enum { TAG_DIRT = 0, TAG_CONCRETE, TAG_REINFORCED };
enum { ZS_PASSIVE_HEEL = 0, ZS_SUBTERRANEAN_SWIM, ZS_WALL_BREACH, ZS_SURFACE_SURGE, ZS_BLOCKED };
int silence_threshold(void);
int panic_arrogance_max(void);
int engage_arrogance_min(void);
int decorum_start(void);
int decorum_cap(void);
int suspicion_below(void);
int hysteric_below(void);
int wall_hp_concrete(void);
int breach_dps_super(void);
int snoop_penalty(void);
int effective_witnesses(int, int);
int witness_state(int, int, int, int);
int escalation_rank(int);
int npc_next_state(int, int, int, int, int, int);
int is_legal_transition(int, int);
int engage_outcome(int, int);
int silence_target_mask(int, int, int);
int clamp_decorum(int);
int decorum_delta(int);
int decorum_after(int, int);
int decorum_band(int);
int zone_access(int, int, int);
int conspicuousness(int, int);
int snoop_conspicuousness(int, int);
int noticed(int, int, int);
int move_speed_pct(int);
int wall_max_hp(int);
int breach_dps(int, int);
int wall_hp_after(int, int, int);
int zombie_next_state(int, int, int, int, int, int);
#endif
