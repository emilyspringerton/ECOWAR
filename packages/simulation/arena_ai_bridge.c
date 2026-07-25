#include "arena_ai_bridge.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const char *arena_hero_name(ArenaHeroID hero_id) {
    switch (hero_id) {
    case ARENA_HERO_UNICORN: return "unicorn";
    case ARENA_HERO_DUCK:    return "duck";
    case ARENA_HERO_GHOST:   return "ghost";
    case ARENA_HERO_FROG:    return "frog";
    case ARENA_HERO_DOC_WHEEL: return "doc_wheel";
    case ARENA_HERO_TREE:    return "tree";
    case ARENA_HERO_PIZZA:   return "pizza";
    case ARENA_HERO_FLAMEL:  return "flamel";
    case ARENA_HERO_MORRIGAN: return "morrigan";
    case ARENA_HERO_DAGDA:   return "dagda";
    case ARENA_HERO_COURIER: return "courier";
    case ARENA_HERO_LOKI:    return "loki";
    case ARENA_HERO_GARY:    return "gary";
    case ARENA_HERO_FLUTE_DEBT: return "flute_debt";
    case ARENA_HERO_BACON_PUCK: return "bacon_puck";
    case ARENA_HERO_ABRAHAM: return "abraham";
    case ARENA_HERO_ADA:     return "ada";
    case ARENA_HERO_TYLER:   return "tyler";
    case ARENA_HERO_PAIMON:  return "paimon";
    case ARENA_HERO_NOOR1:   return "noor1";
    case ARENA_HERO_CAIN:    return "cain";
    case ARENA_HERO_GUNNR:   return "gunnr";
    case ARENA_HERO_VASSAGO: return "vassago";
    case ARENA_HERO_HE_XIANGU: return "he_xiangu";
    case ARENA_HERO_BELETH:  return "beleth";
    default:                 return "unknown";
    }
}

/* Real ability names, one row per hero, matching docs/HEROES_VS0.md exactly (S170-96/S170-112
 * follow-up: the HUD only ever showed generic "Q READY/CD", never which real ability that was).
 * {Q, W, R} -- kept short enough to fit the existing cooldown-strip HUD slots. */
const char *arena_ability_name(ArenaHeroID hero_id, int slot) {
    static const char *NAMES[ARENA_HERO_COUNT][3] = {
        [ARENA_HERO_UNICORN]    = {"DIAGNOSTIC CHARGE", "SPAGHETTI VENT", "FULL DISCLOSURE"},
        [ARENA_HERO_DUCK]       = {"TELEKINETIC YANK", "GOVERNMENT CLEARANCE", "TOTAL TELEKINESIS"},
        [ARENA_HERO_GHOST]      = {"ALIEN FREQUENCY", "NOT A GHOST", "RECITAL"},
        [ARENA_HERO_FROG]       = {"LOOP BACK", "BORROWED TIME", "THE SECRET"},
        [ARENA_HERO_DOC_WHEEL]  = {"BEDSIDE MANNER", "HOUSE CALL", "NO COMBAT POWER"},
        [ARENA_HERO_TREE]       = {"VINE LASH", "UNTRANSLATED", "GRAND SECRET"},
        [ARENA_HERO_PIZZA]      = {"NOBODY CHECKED", "I AM THE CHOSEN ONE", "NOBODY EVER CHECKS"},
        [ARENA_HERO_FLAMEL]     = {"VINE GROWTH", "PHILOSOPHER'S BLOOM", "ELIXIR OF WILD GROWTH"},
        [ARENA_HERO_MORRIGAN]   = {"THE WASHER'S STRIKE", "THREE FORMS", "THE CROW CONFIRMS IT"},
        [ARENA_HERO_DAGDA]      = {"THE WHEELED CLUB", "UAITHNE, CALLED BY NAME", "THE PORRIDGE"},
        [ARENA_HERO_COURIER]    = {"THE INSULT, LIGHTLY EDITED", "BETWEEN EAGLE AND SERPENT", "THE DEBT COLLECTOR'S DUE"},
        [ARENA_HERO_LOKI]       = {"INTERFERENCE, NOT A SIGNAL", "BOUND WHERE THE MYTH SAYS", "HELD FOR AS LONG AS THE MYTH DEMANDS"},
        [ARENA_HERO_GARY]       = {"THE PROPERTY", "WATCHING THE BRIDGE", "SLOW DOWN, TRACK MEET"},
        [ARENA_HERO_FLUTE_DEBT] = {"THE WRONG NOTE", "RECOUPING INTEREST", "EVENTUALLY COLLECTS"},
        [ARENA_HERO_BACON_PUCK] = {"ASK AGAIN LATER", "WHICH ONE IS REAL", "THE TRICK WAS ALWAYS THE SAME"},
        [ARENA_HERO_ABRAHAM]    = {"THE SACRED MAGIC", "THE BOOK, UNATTESTED", "THE GUARDIAN ANGEL, CONTACTED"},
        [ARENA_HERO_ADA]        = {"THE ANALYTICAL ENGINE", "POETICAL SCIENCE", "FIRST PROGRAM, RUN LATE"},
        [ARENA_HERO_TYLER]      = {"EARTHBIND", "POOF", "DIVIDED WE STAND"},
        [ARENA_HERO_PAIMON]     = {"TEACHES ALL ARTS", "SPEAKS WITH TOTAL AUTHORITY", "TWO HUNDRED LEGIONS"},
        [ARENA_HERO_NOOR1]      = {"FILE WHAT IS ACTUALLY THERE", "SENT IN CLEAN", "DO NOT APPROACH"},
        [ARENA_HERO_CAIN]       = {"THE FIRST MURDER", "CURSED TO WANDER", "THE MARK"},
        [ARENA_HERO_GUNNR]      = {"ARGUED WITH A RAVEN", "THREE MORE THINGS", "VALHALLA HAS YET TO ADMIT IT"},
        [ARENA_HERO_VASSAGO]    = {"REVEAL THE GENTLE MAYBE", "THE SOFT FORESIGHT", "THE GENTLE MAYBE"},
        [ARENA_HERO_HE_XIANGU]  = {"MOTHER-OF-PEARL AND MOONLIGHT", "SELF-DENIAL", "NEVER FRAMED AS SACRIFICE"},
        [ARENA_HERO_BELETH]     = {"EVERY LOVE TRIANGLE", "HOPE IS A TERROR I LEASH WITH SONG", "THE DETONATION"},
    };
    if (hero_id < 0 || hero_id >= ARENA_HERO_COUNT || slot < 0 || slot > 2) return "?";
    const char *name = NAMES[hero_id][slot];
    return name ? name : "?";
}

void arena_serialize_state(int owner, unsigned int tick_ms, char *out, size_t out_len) {
    if (out_len == 0) return;
    out[0] = '\0';
    if (owner < 0 || owner > 1) return;

    const ArenaHero *self = &arena_state.heroes[owner];
    const ArenaHero *foe = &arena_state.heroes[owner == 0 ? 1 : 0];
    float dx = foe->x - self->x, dz = foe->z - self->z;
    float dist = sqrtf(dx * dx + dz * dz);

    snprintf(out, out_len,
        "redgarden arena tick:%u\n"
        "self hero:%s pos:%.2f,%.2f hp:%d max_hp:%d alive:%d "
        "q_cd:%d w_active:%d w_cd:%d r_cd:%d r_active:%d silenced:%d intangible:%d\n"
        "foe hero:%s pos:%.2f,%.2f hp:%d max_hp:%d alive:%d dist:%.2f "
        "q_cd:%d w_cd:%d r_cd:%d r_active:%d silenced:%d intangible:%d",
        tick_ms,
        arena_hero_name(self->hero_id), self->x, self->z, self->hp, self->max_hp, self->alive,
        self->q_cooldown_ms, self->w_active, self->w_cooldown_ms, self->r_cooldown_ms,
        self->r_active_ms, self->silenced_ms, self->intangible_ms,
        arena_hero_name(foe->hero_id), foe->x, foe->z, foe->hp, foe->max_hp, foe->alive, dist,
        foe->q_cooldown_ms, foe->w_cooldown_ms, foe->r_cooldown_ms, foe->r_active_ms,
        foe->silenced_ms, foe->intangible_ms);
}

int arena_decode_action(const char *action_str, ArenaAction *out) {
    memset(out, 0, sizeof(*out));
    if (!action_str) return 0;
    int found = 0;

    const char *mp = strstr(action_str, "move:");
    if (mp) {
        float mx, mz;
        if (sscanf(mp, "move:%f,%f", &mx, &mz) == 2) {
            out->move_x = mx;
            out->move_z = mz;
            out->has_move = 1;
            found = 1;
        }
    }

    const char *p;
    if ((p = strstr(action_str, "cast_q:")) != NULL) {
        out->cast_q = (p[7] == '1');
        found = 1;
    }
    if ((p = strstr(action_str, "cast_w:")) != NULL) {
        out->cast_w = (p[7] == '1');
        found = 1;
    }
    if ((p = strstr(action_str, "cast_r:")) != NULL) {
        out->cast_r = (p[7] == '1');
        found = 1;
    }

    return found;
}
