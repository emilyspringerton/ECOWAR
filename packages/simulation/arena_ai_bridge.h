#ifndef ARENA_AI_BRIDGE_H
#define ARENA_AI_BRIDGE_H

#include <stddef.h>
#include "arena_game.h"

/* Game AI bridge, Milestone-6 equivalent (EMILY/BACKLOG.md S170-36,
 * NORTHSTAR §12 Phase E). Extends gpt2-alpine-c/docs/GAME_AI_NORTHSTAR.md's
 * state-serializer/action-decoder pattern to arena's own state, rather than
 * inventing a REDGARDEN-specific format from scratch -- same natural-
 * language token style as that doc's SHANKPIT/BedWars examples, just a new
 * vocabulary for arena's hero/ability state.
 *
 * This is the contract milestone only: serialize state, decode an action
 * string back into arena's control primitives. It does not call the GPT-2
 * inference server (:8088) -- wiring live inference in, and the fine-tune/
 * self-play loop behind it, are later, separate slices (that pipeline needs
 * an external Colab GPU run and a human to trigger it, not buildable
 * end-to-end in this environment). */

/* arena_hero_name returns a short lowercase token name for hero_id
 * ("unicorn", "duck", "ghost", "frog"), matching the vocabulary style
 * GAME_AI_NORTHSTAR.md uses for its own domain tokens (weapon names, etc).
 * Returns "unknown" for an out-of-range value rather than a garbage
 * pointer or crash. */
const char *arena_hero_name(ArenaHeroID hero_id);

/* arena_hero_w_is_toggle (S170-181): 1 if hero_id's W is a genuine hold-on/hold-off toggle
 * (drains mana over time via ARENA_MP_DRAIN_W_PER_SEC while active), 0 if it's an instant
 * effect on cooldown that reuses the W slot (still charges the flat ARENA_MP_COST_W). The
 * client HUD needs this to know which mana-cost model applies to a given hero's W tile. */
int arena_hero_w_is_toggle(ArenaHeroID hero_id);

/* arena_ability_name returns the real ability name for hero_id's Q/W/R slot
 * (slot 0/1/2), matching docs/HEROES_VS0.md's own naming ("EARTHBIND", "POOF",
 * not a generic "Q"/"W"/"R" label) -- S170-96/S170-112, the client HUD only
 * ever showed cooldown state, never which real ability was on cooldown.
 * Returns "?" for an out-of-range hero_id or slot rather than garbage. */
const char *arena_ability_name(ArenaHeroID hero_id, int slot);

/* arena_ability_description (S170-151, "H should show an overlay with
 * character ability descriptions"): a short, plain-language blurb of what
 * hero_id's Q/W/R slot actually does mechanically in this arena (not the
 * full docs/HEROES_VS0.md lore prose -- a quick in-match reference, same
 * "short enough to actually read at a glance" bar as arena_ability_name's
 * own tiles). Returns "?" for an out-of-range hero_id or slot. */
const char *arena_ability_description(ArenaHeroID hero_id, int slot);

/* arena_hero_tags_string (S170-194, NORTHSTAR §18.6's cross-hero-transfer tags): writes a
 * space-separated list of hero_id's mechanical-shape tags into out (NUL-terminated, truncated
 * to out_len-1 if needed) -- "ranged"/"melee" (always exactly one), then any of
 * has_homing_attack/has_knockback/has_heal/has_dash/has_stealth that are true, same "only show
 * what's active" idiom hero_status_label already uses. Empty string for an out-of-range
 * hero_id. Describes WHAT a kit mechanically does, not WHICH hero it is, so training experience
 * against/with one hero transfers to any other hero sharing the same tags. */
void arena_hero_tags_string(ArenaHeroID hero_id, char *out, size_t out_len);

/* arena_serialize_state writes a stable, natural-language state token
 * string for the match as seen from owner's point of view ("self" =
 * owner's hero, "foe" = the nearest living enemy, S170-194: any real team-mode
 * opponent via arena_nearest_enemy, not just the 1v1 local demo's hardcoded
 * "the other slot") into out (NUL-terminated, truncated to out_len-1 if
 * needed). owner must be a real, active hero slot (0..ARENA_MAX_HEROES-1);
 * invalid input, or a slot with no living enemy left, writes a self-only or
 * empty string rather than garbage. Includes each side's own
 * arena_hero_tags_string() output (S170-194) so the model learns a kit
 * shape's pattern from both playing it and facing it. Same input always
 * produces the same output (no timestamps/randomness beyond the tick
 * counter itself). */
void arena_serialize_state(int owner, unsigned int tick_ms, char *out, size_t out_len);

/* arena_corpus_record (S170-194, NORTHSTAR §18.4's own "state -> the action that was actually
 * taken next" unsupervised-pretraining corpus format): writes one training-ready JSONL record
 * for owner's current tick into out -- arena_serialize_state's own output, followed by an
 * "action:" line built from owner's CURRENT in-flight action (move target if h->moving, which
 * Q/W/R cast_flash_slot fired this exact tick if any), in the exact same
 * "move:x,z cast_q:0/1 cast_w:0/1 cast_r:0/1" shape arena_decode_action already parses -- the
 * same string format works as both this corpus's action label and (later, at inference time)
 * a real Tier-1 policy net's own generated output, one format for both directions. Wrapped as
 * a single-line `{"text":"..."}` JSON object, the exact record shape
 * gpt2-alpine-c/scripts/colab_train.py's own record_to_text() already expects (text field,
 * newlines escaped) -- this repo's corpus can train with that same, already-proven script
 * unmodified. Empty string (writes nothing) if owner is out of range or inactive, same
 * "no garbage" convention arena_serialize_state itself already follows. */
void arena_corpus_record(int owner, unsigned int tick_ms, char *out, size_t out_len);

typedef struct {
    float move_x, move_z;
    int has_move; /* 1 if the action string included a move: token */
    int cast_q, cast_w, cast_r;
} ArenaAction;

/* arena_decode_action parses a GAME_AI_NORTHSTAR.md-style action token
 * string (e.g. "move:4.20,1.00 cast_q:1 cast_w:0 cast_r:0") into out.
 * Missing fields default to "no move, no casts" rather than garbage --
 * a policy that only emits "cast_q:1" still gets a safe, valid action.
 * Returns 1 if at least one recognized token was found, 0 if the string
 * had nothing usable in it at all (fails closed: caller should treat that
 * as "do nothing this tick," not retry with garbage). */
int arena_decode_action(const char *action_str, ArenaAction *out);

#endif
