#include "arena_game.h"
#include <math.h>
#include <string.h>

ArenaState arena_state;
int arena_bot_enabled = 1;

/* arena_creeps_reset (S170-51): shared init helper for both arena_init_*
 * entry points. memset already zeroes alive/respawn_ms_remaining to the
 * correct "spawn on the first tick" defaults; the one field that needs an
 * explicit non-zero sentinel is last_attacked_by_owner (0 would wrongly
 * mean "owner slot 0", not "never hit"). */
static void arena_creeps_reset(void) {
    for (int i = 0; i < ARENA_MAX_CREEPS; i++) {
        arena_state.creeps[i].last_attacked_by_owner = -1;
    }
}

/* ---- Tiny hand-authored feed-forward "brain" for the bot hero ----
 * Same shape as SHANKPIT's bot brain (packages/simulation/neural_net.h,
 * dense_layer(): out = activation(W*in + b)) rather than a copy of it --
 * SHANKPIT's net is trained (PyTorch-exported weights in brain_weights.h)
 * against FPS-specific inputs (yaw/pitch/strafe/shoot) that don't exist in
 * this top-down click-to-move arena. This is the same forward-pass
 * mechanism (dense layer -> ReLU -> dense layer -> Tanh) re-sized for this
 * game's inputs/outputs, with hand-picked (not trained) weights -- there's
 * no training pipeline wired up here yet. Real training data/pipeline is a
 * fast-follow; this is the honest "or equivalent" for tonight.
 */
static float dense_relu(const float *in, const float *w, const float *b, int i, int in_size) {
    float sum = b[i];
    for (int j = 0; j < in_size; j++) sum += in[j] * w[i * in_size + j];
    return sum > 0.0f ? sum : 0.0f;
}

/* inputs: [dx_norm, dz_norm, dist_norm, hp_frac_diff] */
static void bot_brain_forward(const float in[4], float out[2]) {
    /* Layer 1: 4 -> 6, ReLU. Neurons 0-3 split dx/dz into +/- halves so the
       output layer can recombine them with relu(x)-relu(-x) == x -- i.e.
       the net's steering output reduces to "turn toward the target,"
       computed through the same layered structure as a trained net would
       use. Neurons 4-5 carry distance/hp-diff signal, wired in with zero
       output weight for now -- left as the hook a future trained pass
       would use for kiting/retreat behavior. */
    static const float w1[6 * 4] = {
        1, 0, 0, 0,
        -1, 0, 0, 0,
        0, 1, 0, 0,
        0, -1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    static const float b1[6] = {0, 0, 0, 0, 0, 0};
    float h[6];
    for (int i = 0; i < 6; i++) h[i] = dense_relu(in, w1, b1, i, 4);

    /* Layer 2: 6 -> 2, Tanh. */
    static const float w2[2 * 6] = {
        1, -1, 0, 0, 0, 0,
        0, 0, 1, -1, 0, 0,
    };
    static const float b2[2] = {0, 0};
    for (int i = 0; i < 2; i++) {
        float sum = b2[i];
        for (int j = 0; j < 6; j++) sum += h[j] * w2[i * 6 + j];
        out[i] = tanhf(sum);
    }
}

/* S170-119: Arathi Basin-style 5-node spread -- two flanking nodes near each
 * team's spawn (heroes[0] at x=-6, heroes[1] at x=6, see below) plus one
 * contested center node, same "two-near-each-side plus a middle" shape as
 * the real Stables/Farm .. Blacksmith .. Lumber Mill/Gold Mine layout, just
 * along this arena's existing spawn axis instead of Arathi's own geography.
 * S170-139: coordinates scaled 1.5x (matching ARENA_HALF_EXTENT's own 20->30
 * bump) for "true Arathi Basin size" -- shape unchanged, just bigger. */
static void arena_nodes_reset_layout(void) {
    static const float layout[ARENA_NODE_COUNT][2] = {
        { -19.5f,  12.0f }, /* Stables */
        { -19.5f, -12.0f }, /* Farm */
        {    0.0f,   0.0f }, /* Blacksmith (center, contested) */
        {  19.5f,  12.0f }, /* Lumber Mill */
        {  19.5f, -12.0f }, /* Gold Mine */
    };
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        arena_state.nodes[n].x = layout[n][0];
        arena_state.nodes[n].z = layout[n][1];
        arena_state.nodes[n].marked_by_team = -1;
        arena_state.nodes[n].capturing_team = -1;
    }
}

void arena_init_with_heroes(ArenaHeroID player_hero, ArenaHeroID bot_hero) {
    memset(&arena_state, 0, sizeof(arena_state));

    arena_state.heroes[0].x = -6.0f;
    arena_state.heroes[0].z = 0.0f;
    arena_state.heroes[0].target_x = -6.0f;
    arena_state.heroes[0].target_z = 0.0f;
    arena_state.heroes[0].hp = arena_state.heroes[0].max_hp = 100;
    arena_state.heroes[0].mp = arena_state.heroes[0].max_mp = ARENA_MP_MAX;
    arena_state.heroes[0].owner = 0;
    arena_state.heroes[0].alive = 1;
    arena_state.heroes[0].active = 1;
    arena_state.heroes[0].team = 0;
    arena_state.heroes[0].hero_id = player_hero;

    arena_state.heroes[1].x = 6.0f;
    arena_state.heroes[1].z = 0.0f;
    arena_state.heroes[1].target_x = 6.0f;
    arena_state.heroes[1].target_z = 0.0f;
    arena_state.heroes[1].hp = arena_state.heroes[1].max_hp = 100;
    arena_state.heroes[1].mp = arena_state.heroes[1].max_mp = ARENA_MP_MAX;
    arena_state.heroes[1].owner = 1;
    arena_state.heroes[1].alive = 1;
    arena_state.heroes[1].active = 1;
    arena_state.heroes[1].team = 1;
    arena_state.heroes[1].hero_id = bot_hero;

    arena_nodes_reset_layout();
    arena_creeps_reset();

    arena_state.winner = 0;
}

void arena_init(void) {
    /* Player=Unicorn, bot=Duck: both slots carry a real kit (S170-31),
     * proving Phase D's "both sides" requirement rather than just adding a
     * second player-selectable option. */
    arena_init_with_heroes(ARENA_HERO_UNICORN, ARENA_HERO_DUCK);
}

void arena_set_move_target(int owner, float x, float z) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    if (x < -ARENA_HALF_EXTENT) x = -ARENA_HALF_EXTENT;
    if (x > ARENA_HALF_EXTENT) x = ARENA_HALF_EXTENT;
    if (z < -ARENA_HALF_EXTENT) z = -ARENA_HALF_EXTENT;
    if (z > ARENA_HALF_EXTENT) z = ARENA_HALF_EXTENT;
    arena_state.heroes[owner].target_x = x;
    arena_state.heroes[owner].target_z = z;
    arena_state.heroes[owner].moving = 1;
}

void arena_bot_tick(unsigned int dt_ms) {
    (void)dt_ms;
    ArenaHero *bot = &arena_state.heroes[1];
    ArenaHero *foe = &arena_state.heroes[0];
    if (!bot->alive || !foe->alive) return;

    float dx = foe->x - bot->x;
    float dz = foe->z - bot->z;
    float dist = sqrtf(dx * dx + dz * dz);

    float in[4];
    in[0] = dx / ARENA_HALF_EXTENT;
    in[1] = dz / ARENA_HALF_EXTENT;
    in[2] = dist / (ARENA_HALF_EXTENT * 2.0f);
    in[3] = ((float)bot->hp / bot->max_hp) - ((float)foe->hp / foe->max_hp);

    float out[2];
    bot_brain_forward(in, out);

    /* Steer a few units ahead in the net's suggested direction each tick --
       cheap re-evaluation gives continuous chase without full pathfinding. */
    float step = 3.0f;
    arena_set_move_target(1, bot->x + out[0] * step, bot->z + out[1] * step);
}

static void update_hero_motion(ArenaHero *h, float dt_sec) {
    /* rooted_ms (S170-46): a queued move command is preserved (not
       cancelled) but doesn't advance while rooted -- matches how silence
       blocks casting without clearing the ability off cooldown. */
    if (!h->alive || !h->moving || h->rooted_ms > 0) return;
    float dx = h->target_x - h->x;
    float dz = h->target_z - h->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.05f) {
        h->moving = 0;
        return;
    }
    float step = ARENA_HERO_SPEED * dt_sec;
    if (step >= dist) {
        h->x = h->target_x;
        h->z = h->target_z;
        h->moving = 0;
    } else {
        h->x += dx / dist * step;
        h->z += dz / dist * step;
    }
}

/* arena_hero_armor: effective armor including Full Disclosure's temporary
 * double. Only The Unicorn has passive armor (S170-18); The Duck (S170-31)
 * has none -- dispatch is by hero_id now, not by owner slot, so either side
 * gets Unicorn's armor if either side is playing Unicorn. */
float arena_hero_armor(const ArenaHero *h) {
    if (h->hero_id == ARENA_HERO_UNICORN) {
        float armor = (float)ARENA_UNICORN_ARMOR;
        if (h->r_active_ms > 0) armor *= 2.0f;
        return armor;
    }
    /* Tree's Grand Secret (R, S170-46): flat armor bonus while self-rooted. */
    if (h->hero_id == ARENA_HERO_TREE && h->r_active_ms > 0) {
        return (float)ARENA_TREE_R_ARMOR_BONUS;
    }
    /* Morrigan's Contested Ground (passive, S170-47): bonus armor while
       standing within capture radius of a node that's still contested
       (owner == 0, neither team has claimed it) -- a war goddess belongs
       to the unresolved fight, her jungler tie to the territory system. */
    if (h->hero_id == ARENA_HERO_MORRIGAN) {
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            const ArenaNode *node = &arena_state.nodes[n];
            if (node->owner != 0) continue;
            float dx = h->x - node->x, dz = h->z - node->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_NODE_CAPTURE_RADIUS) {
                return (float)ARENA_MORRIGAN_PASSIVE_ARMOR_BONUS;
            }
        }
    }
    /* Loki's Bound Where the Myth Says (W, S170-79): flat armor while toggled on. */
    if (h->hero_id == ARENA_HERO_LOKI && h->w_active) {
        return (float)ARENA_LOKI_W_ARMOR_BONUS;
    }
    /* Ada's frame plating (W, S170-103): flat armor while toggled on. */
    if (h->hero_id == ARENA_HERO_ADA && h->w_active) {
        return (float)ARENA_ADA_W_ARMOR_BONUS;
    }
    /* Tyler's Divided We Stand (R, S170-111): armor goes NEGATIVE for the window --
       apply_armor does raw_damage - armor, so a negative value increases damage taken.
       The real risk half of the risk/reward the OG clone-death rule was standing in for. */
    if (h->hero_id == ARENA_HERO_TYLER && h->r_active_ms > 0) {
        return -ARENA_TYLER_R_NEGATIVE_ARMOR;
    }
    /* Cain's founded city (passive, S170-105): flat, always-on -- "the man cast out to wander
       settled down and built civilization anyway," the one permanent thing about him. */
    if (h->hero_id == ARENA_HERO_CAIN) {
        return (float)ARENA_CAIN_PASSIVE_ARMOR;
    }
    /* Gunnr's shieldmaiden stance (passive, S170-93): flat, always-on, same shape as Cain's own. */
    if (h->hero_id == ARENA_HERO_GUNNR) {
        return (float)ARENA_GUNNR_PASSIVE_ARMOR;
    }
    /* Beleth's own survival (passive, S170-93): flat, always-on, same shape as Cain's/
       Gunnr's -- she's outlived every escalation she's ever caused. Lower than either of
       theirs; her kit's damage/control already does the heavy lifting. */
    if (h->hero_id == ARENA_HERO_BELETH) {
        return (float)ARENA_BELETH_PASSIVE_ARMOR;
    }
    /* MnM's shell (passive + W, S170-134): a flat always-on base, same shape as Cain's/Gunnr's/
       Beleth's own, PLUS a further toggle bonus while W is active (Loki's/Ada's own toggle
       shape) -- the two stack, unlike Loki/Ada whose entire armor value comes only from the
       toggle. The tank archetype's stat profile: consistently armored, more so at will. */
    if (h->hero_id == ARENA_HERO_MNM) {
        float armor = (float)ARENA_MNM_PASSIVE_ARMOR;
        if (h->w_active) armor += (float)ARENA_MNM_W_ARMOR_BONUS;
        return armor;
    }
    return 0.0f;
}

static int apply_armor(int raw_damage, float armor) {
    int dmg = raw_damage - (int)armor;
    return dmg < 1 ? 1 : dmg;
}

/* tyler_clone_cascade_kill (S170-141): the literal OG "one dies, all die"
 * rule -- force-kills every hero entry sharing `link_owner`'s clone link
 * (link_owner itself, plus every is_clone entry whose clone_owner points at
 * it), no exceptions, even bypassing a linked entity's own survive_floor_ms
 * (that mechanic protects against a hit landing on IT, not against this
 * separate shared-fate rule). Clone slots free immediately on death (same
 * "no respawn queue" idiom lane creeps use); the real Tyler, if he's not
 * already the one who triggered this, still gets the normal
 * ARENA_HERO_RESPAWN_MS queued like any other hero death. */
static void tyler_clone_cascade_kill(int link_owner) {
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive) continue;
        int is_linked = (i == link_owner) || (h->is_clone && h->clone_owner == link_owner);
        if (!is_linked) continue;
        h->hp = 0;
        h->alive = 0;
        if (h->is_clone) {
            h->active = 0;
        } else {
            h->respawn_ms_remaining = ARENA_HERO_RESPAWN_MS;
        }
    }
}

/* apply_damage (S170-46): centralizes "subtract HP, clamp at 0, mark dead"
 * across every damage call site, so Pizza's R (a real damage floor, not a
 * simplified-away shield like Doc Wheel's) only needs one place to check
 * survive_floor_ms rather than duplicating the check at every site. Armor
 * is already applied by the caller via apply_armor -- this only handles the
 * HP-floor/death half. Also (S170-51 cont'd) the single choke point for
 * "this hero took damage this tick," which arena_tick_nodes reads to
 * interrupt a capture channel -- every damage source in this file already
 * routes through here, so this needed no new call sites of its own. */
static void apply_damage(ArenaHero *target, int amount) {
    target->damaged_this_tick = 1;
    target->hp -= amount;
    if (target->hp <= 0) {
        if (target->survive_floor_ms > 0) {
            target->hp = 1;
        } else {
            target->hp = 0;
            target->alive = 0;
            target->respawn_ms_remaining = ARENA_HERO_RESPAWN_MS;
            /* S170-141: Tyler's real shared-fate death. Only pay the extra
               scan when the hero that just died is actually clone-linked
               (a clone itself, or a real Tyler who may have active clones
               out) -- every other hero's ordinary death in this 26-hero
               roster skips this entirely. */
            if (target->is_clone || target->hero_id == ARENA_HERO_TYLER) {
                int dead_index = (int)(target - arena_state.heroes);
                int link_owner = target->is_clone ? target->clone_owner : dead_index;
                tyler_clone_cascade_kill(link_owner);
            }
        }
    }
}

/* arena_nearest_enemy: the nearest active, living hero on a different team
 * than `owner` -- generalizes what used to be a hardcoded "the other slot"
 * lookup (1v1-only) so the same cast functions work for both the 1v1 local
 * demo (where it trivially resolves to the one other hero) and team mode
 * (where it picks a real target out of up to 19 others). Returns NULL if
 * owner is out of range or nobody qualifies (e.g. owner's whole team is the
 * only one left, or owner itself isn't active).
 *
 * S170-141: bound widened from ARENA_MAX_HEROES to ARENA_HEROES_ARRAY_SIZE
 * so this ALSO sees Tyler's puppet clones -- both directions: a real enemy
 * hero can find and target a clone through this exact same lookup (no
 * separate clone-targeting path needed), and a clone itself (called with
 * its own puppet-range index as `owner`) can find an enemy to fight. This
 * is the one shared lookup every kit cast and the team-mode melee loop
 * already goes through, so widening it here is what makes clones "just
 * fight like a real hero" rather than needing a parallel combat system. */
ArenaHero *arena_nearest_enemy(int owner) {
    if (owner < 0 || owner >= ARENA_HEROES_ARRAY_SIZE) return NULL;
    ArenaHero *self = &arena_state.heroes[owner];
    if (!self->active) return NULL;
    ArenaHero *best = NULL;
    float best_dist = 0.0f;
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *cand = &arena_state.heroes[i];
        if (!cand->active || !cand->alive) continue;
        if (cand->team == self->team) continue;
        float dx = cand->x - self->x, dz = cand->z - self->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (!best || dist < best_dist) { best = cand; best_dist = dist; }
    }
    return best;
}

/* arena_nearest_ally: the nearest active, living hero on the SAME team as
 * `owner`, excluding owner itself. Mirrors arena_nearest_enemy exactly
 * (S170-45) -- the enabling primitive for every ally-targeted kit piece
 * previously skipped for having no target in 1v1. */
ArenaHero *arena_nearest_ally(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return NULL;
    ArenaHero *self = &arena_state.heroes[owner];
    if (!self->active) return NULL;
    ArenaHero *best = NULL;
    float best_dist = 0.0f;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        if (i == owner) continue;
        ArenaHero *cand = &arena_state.heroes[i];
        if (!cand->active || !cand->alive) continue;
        if (cand->team != self->team) continue;
        float dx = cand->x - self->x, dz = cand->z - self->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (!best || dist < best_dist) { best = cand; best_dist = dist; }
    }
    return best;
}

/* arena_tick_nodes (S170-46, capture mechanic redesigned S170-50): advances
 * every ArenaNode's Arathi Basin-style channel by dt_ms. One pass per node:
 * classify which team(s) have living presence in radius, apply Flamel's
 * Overgrowth mark refresh/decay, then either start/continue a channel
 * (exclusive single-team presence), interrupt one (mixed presence, a
 * Pizza's corruption, or the channeling team leaving), or leave an
 * already-settled node alone. Generalizes across 1v1 and team mode with no
 * special-casing, same as arena_nearest_ally/arena_nearest_enemy -- 1v1
 * already sets team=0/1 on its two hardcoded heroes. */
void arena_tick_nodes(unsigned int dt_ms) {
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        int team_present[2] = {0, 0};
        int team_visible[2] = {0, 0}; /* present AND not currently stealthed (intangible_ms <= 0) */
        int team_damaged[2] = {0, 0}; /* any hero of this team, in radius, took damage this tick */
        int tree_on_team[2] = {0, 0};
        int pizza_in_radius = 0;
        int flamel_marker_team = -1;

        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->active || !h->alive) continue;
            float dx = h->x - node->x, dz = h->z - node->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_NODE_CAPTURE_RADIUS) continue;
            team_present[h->team] = 1;
            if (h->intangible_ms <= 0) team_visible[h->team] = 1;
            if (h->damaged_this_tick) team_damaged[h->team] = 1;
            if (h->hero_id == ARENA_HERO_TREE) tree_on_team[h->team] = 1;
            if (h->hero_id == ARENA_HERO_PIZZA) pizza_in_radius = 1;
            if (h->hero_id == ARENA_HERO_FLAMEL) flamel_marker_team = h->team;
        }

        if (flamel_marker_team >= 0) {
            node->marked_by_team = flamel_marker_team;
            node->mark_ms_remaining = ARENA_FLAMEL_MARK_MS;
        } else if (node->mark_ms_remaining > 0) {
            node->mark_ms_remaining -= (int)dt_ms;
            if (node->mark_ms_remaining <= 0) {
                node->mark_ms_remaining = 0;
                node->marked_by_team = -1;
            }
        }

        /* Exactly one team present (and Pizza isn't corrupting the attempt
           regardless of side) is the only condition that can start or
           continue a channel -- mixed presence, empty presence, or Pizza
           in radius all interrupt whatever's in progress.

           The stealth exception (S170-51 cont'd -- "a stealthed character
           sneaking in and solo-capping an objective while clueless
           opponents run around nearby," the archetypal WoW Arathi Basin
           moment): if a team's ENTIRE presence at this node is stealthed
           (intangible_ms > 0 -- Frog's R, which the doc itself describes as
           "vanishes... can't be targeted or seen"), the other team's
           presence, however visible, never even registers a contest --
           they don't know there's anything there to fight. A lone
           stealthed capper channels straight through a crowd of unaware
           enemies standing right on top of the node. This only ever lets
           ONE side capture undetected at a time: if both sides happen to be
           entirely stealthed simultaneously, or both are visible, the
           normal exclusive-presence rule below still applies unchanged. */
        int exclusive_team = -1;
        if (!pizza_in_radius) {
            if (team_present[0] != team_present[1]) {
                exclusive_team = team_present[0] ? 0 : 1;
            } else if (team_present[0] && team_present[1]) {
                int stealthed_only_0 = !team_visible[0];
                int stealthed_only_1 = !team_visible[1];
                if (stealthed_only_0 && !stealthed_only_1) exclusive_team = 0;
                else if (stealthed_only_1 && !stealthed_only_0) exclusive_team = 1;
            }
        }

        /* Damage interrupts the capture, same trigger as real WoW Arathi
           Basin's flag channel (S170-51 cont'd) -- checked before anything
           else so a hero who got hit this tick can't also make progress
           this same tick. */
        if (exclusive_team < 0 || team_damaged[exclusive_team]) {
            /* Interrupted (nothing was happening, mixed/corrupted presence,
               or the channeling team took damage) -- owner is left exactly
               as-is. A defender who denies an attacker doesn't get the node
               handed back for free; they still have to start their own
               channel, same as a would-be attacker who gets chased off. */
            node->capturing_team = -1;
            node->capture_progress_ms = 0;
            continue;
        }

        if (node->owner == exclusive_team + 1) {
            /* Already theirs -- nothing to capture, no channel to run.
               (+1: owner encodes 0=neutral/1=team0/2=team1, exclusive_team
               is the raw 0/1 team index -- comparing them directly would
               make a fresh neutral node (owner=0) collide with team index
               0, wrongly treated as "already owned by team 0".) */
            node->capturing_team = -1;
            node->capture_progress_ms = 0;
            continue;
        }

        if (node->capturing_team != exclusive_team) {
            /* A channel is starting (fresh, or switching from whichever
               team had been channeling) -- the node flips to neutral
               immediately, the "neutral period... as you wait for it to
               finish capturing" the channel spends open and uncaptured,
               not just at the moment it completes. */
            node->capturing_team = exclusive_team;
            node->capture_progress_ms = 0;
            node->owner = 0;

            /* Interacting with the flag breaks stealth (S170-51 cont'd) --
               real Arathi Basin's own rule. The sneaking-in part of the
               archetypal moment is over the instant the channel actually
               starts; whether the enemy crowd standing right there reacts
               in time is now down to their own attention/positioning, not
               a standing invisibility loophole. Only breaks the stealth of
               heroes on the team that just started this channel, in this
               node's radius -- an ally elsewhere on the map keeps theirs. */
            for (int i = 0; i < ARENA_MAX_HEROES; i++) {
                ArenaHero *h = &arena_state.heroes[i];
                if (!h->active || !h->alive || h->team != exclusive_team || h->intangible_ms <= 0) continue;
                float dx = h->x - node->x, dz = h->z - node->z;
                if (sqrtf(dx * dx + dz * dz) > ARENA_NODE_CAPTURE_RADIUS) continue;
                h->intangible_ms = 0;
            }
        }

        int progress = (int)dt_ms;
        if (tree_on_team[exclusive_team]) {
            progress = (int)((float)progress * ARENA_TREE_CHANNEL_SPEED_MULT);
        }
        if (node->marked_by_team == exclusive_team) {
            progress += ARENA_FLAMEL_MARK_CHANNEL_BONUS_MS;
        }
        node->capture_progress_ms += progress;

        if (node->capture_progress_ms >= ARENA_NODE_CAPTURE_CHANNEL_MS) {
            node->owner = exclusive_team + 1; /* encode team index -> owner (1=team0, 2=team1) */
            node->capturing_team = -1;
            node->capture_progress_ms = 0;
        }
    }

    /* damaged_this_tick is a single-tick flag -- cleared here, once, after
       every node has had a chance to read it this tick, not inside the
       per-node loop above (heroes are shared across nodes; clearing mid-
       loop would make node[1]'s check miss damage node[0]'s check already
       correctly saw). */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        arena_state.heroes[i].damaged_this_tick = 0;
    }
}

/* cast_cooldown: applies the generic next_cast_refund buff (S170-45,
 * Frog's Borrowed Time) -- returns 0 and consumes the buff if it's set on
 * h, else returns normal_ms unchanged. Every Q/W/R cooldown-assignment
 * site in this file routes through this so any future ally-buff kit gets
 * the same refund semantics for free. */
static int cast_cooldown(ArenaHero *h, int normal_ms) {
    if (h->next_cast_refund) {
        h->next_cast_refund = 0;
        return 0;
    }
    return normal_ms;
}

/* hero_is_hittable: The Ghost's W (S170-32) is the first ability in this
 * arena that needs a "can this hero currently be hit at all" concept,
 * distinct from just being alive -- used by auto-attacks and ability
 * damage alike so intangibility means the same thing everywhere. */
static int hero_is_hittable(const ArenaHero *h) {
    /* NULL-safe: arena_nearest_enemy (team mode) returns NULL when nobody
       qualifies (e.g. the last enemy died mid-tick) -- treat "no target" the
       same as "not hittable" rather than crashing. */
    return h && h->alive && h->intangible_ms <= 0;
}

/* arena_spawn_projectile (S170-136, returns a pointer S170-140): see header doc comment. */
ArenaProjectile *arena_spawn_projectile(int owner, int team, ArenaHeroID hero_id,
                             float x, float z, float target_x, float target_z,
                             float speed, float radius, int damage, float max_range) {
    ArenaProjectile *p = NULL;
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        if (!arena_state.projectiles[i].active) { p = &arena_state.projectiles[i]; break; }
    }
    if (!p) return NULL; /* pool exhausted -- generous headroom, should not happen in practice */

    float dx = target_x - x, dz = target_z - z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.0001f) { dx = 1.0f; dz = 0.0f; dist = 1.0f; } /* degenerate same-position cast: pick an arbitrary direction rather than a NaN velocity */

    p->active = 1;
    p->owner = owner;
    p->team = team;
    p->hero_id = hero_id;
    p->x = x;
    p->z = z;
    p->vx = (dx / dist) * speed;
    p->vz = (dz / dist) * speed;
    p->radius = radius;
    p->damage = damage;
    p->max_range = max_range;
    p->traveled = 0.0f;
    /* S170-140: reset every reused pool slot's on-hit effects -- a stale
       value left over from a PREVIOUS shot (e.g. Tyler's root+burn) must
       never leak onto a fresh plain-damage shot (e.g. Gary's Q) that just
       happened to land in the same recycled slot. */
    p->on_hit_silence_ms = 0;
    p->on_hit_root_ms = 0;
    p->on_hit_burn_ms = 0;
    p->on_hit_burn_dps = 0;
    return p;
}

/* arena_tick_projectiles (S170-136): see header doc comment. Enemy-ness is
 * determined by `team` (cached at spawn), not by looking the owner hero back
 * up -- correct even if the caster died or respawned into a different state
 * while the shot was still in flight. */
void arena_tick_projectiles(unsigned int dt_ms) {
    float dt_sec = (float)dt_ms / 1000.0f;
    for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
        ArenaProjectile *p = &arena_state.projectiles[i];
        if (!p->active) continue;

        float old_x = p->x, old_z = p->z;
        float step = sqrtf(p->vx * p->vx + p->vz * p->vz) * dt_sec;
        p->x += p->vx * dt_sec;
        p->z += p->vz * dt_sec;
        p->traveled += step;

        /* S170-140 bugfix: collision is checked against the SEGMENT this
           tick's move traced out (old_x,old_z)->(p->x,p->z), not just the
           end-of-tick position. A large dt_ms (this codebase's own test
           helpers routinely call *_update(1000) for "one full second" test
           steps, and a real frame hitch would do the same) can otherwise
           let a fast shot's position jump clean past a foe standing in its
           path without ever registering a hit -- a real tunneling bug,
           found via test_ghost_r_zone_damages_foe_over_time going from
           reliably-passing to failing the instant Ghost's Q became a
           projectile fired inside a dt_ms=1000 arena_update() call. Reduces
           to the old end-position check when the segment is ~0 length
           (small dt_ms, the common real-frame case), so this is a strict
           correctness fix, not a behavior change for the normal case. */
        float seg_dx = p->x - old_x, seg_dz = p->z - old_z;
        float seg_len_sq = seg_dx * seg_dx + seg_dz * seg_dz;

        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *foe = &arena_state.heroes[h];
            if (!foe->active || foe->team == p->team) continue;
            if (!hero_is_hittable(foe)) continue;

            float t = 0.0f;
            if (seg_len_sq > 0.0001f) {
                t = ((foe->x - old_x) * seg_dx + (foe->z - old_z) * seg_dz) / seg_len_sq;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
            }
            float closest_x = old_x + seg_dx * t;
            float closest_z = old_z + seg_dz * t;
            float dx = foe->x - closest_x, dz = foe->z - closest_z;
            if (sqrtf(dx * dx + dz * dz) > p->radius) continue;

            apply_damage(foe, apply_armor(p->damage, arena_hero_armor(foe)));
            if (p->on_hit_silence_ms > 0) foe->silenced_ms = p->on_hit_silence_ms;
            if (p->on_hit_root_ms > 0) foe->rooted_ms = p->on_hit_root_ms;
            if (p->on_hit_burn_ms > 0) {
                foe->burning_ms = p->on_hit_burn_ms;
                foe->burn_dps = p->on_hit_burn_dps;
            }
            p->active = 0;
            break;
        }
        if (p->active && p->traveled >= p->max_range) p->active = 0; /* whiffed */
    }
}

/* creep_spawn: (re)rolls flavor/HP from the creep's own node's CURRENT
 * owner and places it at the node's position -- the "jungle reacts to who
 * controls the ground under it" half of S170-51's design. */
static void creep_spawn(ArenaCreep *creep, const ArenaNode *node) {
    creep->flavor = (ArenaCreepFlavor)node->owner; /* owner 0/1/2 map directly onto the flavor enum */
    creep->max_hp = creep->hp = (creep->flavor == ARENA_CREEP_NEUTRAL) ? ARENA_CREEP_NEUTRAL_HP : ARENA_CREEP_TEAM_HP;
    creep->x = node->x;
    creep->z = node->z;
    creep->alive = 1;
    creep->attack_cooldown_ms = 0;
    creep->last_attacked_by_owner = -1;
}

/* arena_tick_creeps (S170-51): see the header declaration's doc comment. */
void arena_tick_creeps(unsigned int dt_ms) {
    for (int i = 0; i < ARENA_MAX_CREEPS; i++) {
        ArenaCreep *creep = &arena_state.creeps[i];
        ArenaNode *node = &arena_state.nodes[i];

        if (!creep->alive) {
            creep->respawn_ms_remaining -= (int)dt_ms;
            if (creep->respawn_ms_remaining <= 0) creep_spawn(creep, node);
            continue;
        }

        if (creep->attack_cooldown_ms > 0) creep->attack_cooldown_ms -= (int)dt_ms;

        ArenaHero *target = NULL;
        float best_dist = 0.0f;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *cand = &arena_state.heroes[h];
            if (!cand->active || !hero_is_hittable(cand)) continue;
            float dx = cand->x - creep->x, dz = cand->z - creep->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_CREEP_AGGRO_RADIUS) continue;
            if (!target || dist < best_dist) { target = cand; best_dist = dist; }
        }
        if (target && creep->attack_cooldown_ms <= 0) {
            apply_damage(target, ARENA_CREEP_DAMAGE);
            creep->attack_cooldown_ms = ARENA_CREEP_ATTACK_COOLDOWN_MS;
        }
    }
}

/* creep_die: applies this creep's flavor-specific reward to whoever landed
 * the killing blow (tracked via last_attacked_by_owner -- the LAST hit
 * lands the kill in this arena's simple damage model, so "last attacker"
 * and "killer" are the same thing here), then queues its respawn. See the
 * ARENA_MAX_CREEPS header comment for the full reward design. */
static void creep_die(ArenaCreep *creep, ArenaNode *node) {
    creep->alive = 0;
    creep->respawn_ms_remaining = (creep->flavor == ARENA_CREEP_NEUTRAL)
        ? ARENA_CREEP_NEUTRAL_RESPAWN_MS : ARENA_CREEP_TEAM_RESPAWN_MS;

    if (creep->last_attacked_by_owner < 0) return;
    ArenaHero *killer = &arena_state.heroes[creep->last_attacked_by_owner];

    if (creep->flavor == ARENA_CREEP_NEUTRAL) {
        /* The contested prize: a big swing toward capturing THIS node,
           but only if the killer's team is actually the one channeling it
           right now -- the reward is for jungle-and-territory synergy, not
           an unconditional stat pad disconnected from what's happening at
           the flag itself. */
        if (node->capturing_team == killer->team) {
            node->capture_progress_ms += ARENA_CREEP_NEUTRAL_KILL_CAPTURE_BONUS_MS;
        }
        return;
    }

    /* Team-flavored creep: owner index 1/2 map back to team index 0/1. */
    int owning_team = creep->flavor - 1;
    if (killer->team == owning_team) {
        /* Home-turf resupply. */
        killer->hp += ARENA_CREEP_TEAM_KILL_HEAL;
        if (killer->hp > killer->max_hp) killer->hp = killer->max_hp;
    } else if (node->capturing_team == killer->team) {
        /* Counter-play: farming the enemy's own jungle creep helps flip
           their node, same "only if actually channeling it" gate as above. */
        node->capture_progress_ms += ARENA_CREEP_TEAM_KILL_DENY_CAPTURE_BONUS_MS;
    }
}

/* arena_hero_attack_creeps (S170-51): see the header declaration's doc
 * comment. */
void arena_hero_attack_creeps(unsigned int dt_ms) {
    (void)dt_ms; /* attack_cooldown_ms is ticked in tick_hero_kit/resolve_combat already; this only spends it */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->attack_cooldown_ms > 0) continue;

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue; /* already busy with an enemy hero this tick */
        }

        for (int c = 0; c < ARENA_MAX_CREEPS; c++) {
            ArenaCreep *creep = &arena_state.creeps[c];
            if (!creep->alive) continue;
            float dx = creep->x - h->x, dz = creep->z - h->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;

            /* Creeps have no armor stat -- flat damage, no apply_armor call needed. */
            creep->hp -= ARENA_ATTACK_DAMAGE;
            creep->last_attacked_by_owner = i;
            h->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
            if (creep->hp <= 0) {
                creep->hp = 0;
                creep_die(creep, &arena_state.nodes[c]);
            }
            break; /* one creep target per hero per attack, same as hero-vs-hero */
        }
    }
}

/* lane_creep_waypoint (S170-138): see the ARENA_LANE_WAYPOINT_COUNT header
 * comment in arena_game.h -- a straight 3-point path along the existing
 * spawn axis, each team's own spawn line to the contested center node to the
 * enemy's spawn line. team 0 and team 1 walk the same three points in
 * opposite order. Clamps out-of-range indices defensively rather than
 * reading past the static array. */
static void lane_creep_waypoint(int team, int index, float *out_x, float *out_z) {
    /* S170-139: +-8 -> +-12, matching arena_init_teams'/arena_find_owned_node_for_respawn's own spawn-line bump. */
    static const float path_team0[ARENA_LANE_WAYPOINT_COUNT][2] = { { -12.0f, 0.0f }, { 0.0f, 0.0f }, { 12.0f, 0.0f } };
    static const float path_team1[ARENA_LANE_WAYPOINT_COUNT][2] = { { 12.0f, 0.0f }, { 0.0f, 0.0f }, { -12.0f, 0.0f } };
    if (index < 0) index = 0;
    if (index >= ARENA_LANE_WAYPOINT_COUNT) index = ARENA_LANE_WAYPOINT_COUNT - 1;
    const float (*path)[2] = (team == 0) ? path_team0 : path_team1;
    *out_x = path[index][0];
    *out_z = path[index][1];
}

/* lane_creep_spawn_wave: fills up to ARENA_LANE_CREEPS_PER_WAVE free pool
 * slots with fresh creeps at `team`'s spawn line (waypoint 0), spread along z
 * so a wave doesn't spawn perfectly stacked on one point. If fewer free
 * slots exist than a full wave (a previous wave hasn't fully cleared out),
 * spawns as many as fit rather than blocking the whole wave on pool space --
 * ARENA_MAX_LANE_CREEPS' 4x-a-single-wave headroom should make that rare in
 * practice, not something worth a harder failure mode for. */
static void lane_creep_spawn_wave(int team) {
    int spawned = 0;
    float wx, wz;
    lane_creep_waypoint(team, 0, &wx, &wz);
    for (int i = 0; i < ARENA_MAX_LANE_CREEPS && spawned < ARENA_LANE_CREEPS_PER_WAVE; i++) {
        ArenaLaneCreep *creep = &arena_state.lane_creeps[i];
        if (creep->active) continue;
        creep->active = 1;
        creep->alive = 1;
        creep->team = team;
        creep->waypoint_index = 0;
        creep->hp = creep->max_hp = ARENA_LANE_CREEP_HP;
        creep->x = wx;
        creep->z = wz + (spawned - (ARENA_LANE_CREEPS_PER_WAVE - 1) / 2.0f) * 1.0f;
        creep->attack_cooldown_ms = 0;
        spawned++;
    }
}

/* arena_tick_lane_creeps (S170-138): see the header declaration's doc
 * comment. */
void arena_tick_lane_creeps(unsigned int dt_ms) {
    float dt_sec = (float)dt_ms / 1000.0f;

    for (int t = 0; t < 2; t++) {
        arena_state.lane_wave_timer_ms[t] -= (int)dt_ms;
        if (arena_state.lane_wave_timer_ms[t] > 0) continue;
        arena_state.lane_wave_timer_ms[t] = ARENA_LANE_WAVE_INTERVAL_MS;
        lane_creep_spawn_wave(t);
    }

    for (int i = 0; i < ARENA_MAX_LANE_CREEPS; i++) {
        ArenaLaneCreep *creep = &arena_state.lane_creeps[i];
        if (!creep->active || !creep->alive) continue;

        if (creep->attack_cooldown_ms > 0) creep->attack_cooldown_ms -= (int)dt_ms;

        /* Aggro: nearest hittable enemy hero, or nearest opposing-team lane
           creep if that's closer -- a wave clash is the actual "push" this
           subsystem exists for, not just a hero-harassment tool. Stops to
           fight instead of marching past, same "passive-until-approached
           becomes active-once-engaged" idiom as jungle creeps (S170-51),
           just with a real opposing target instead of only heroes. */
        ArenaHero *nearest_hero = NULL;
        float hero_dist = 0.0f;
        for (int h = 0; h < ARENA_MAX_HEROES; h++) {
            ArenaHero *cand = &arena_state.heroes[h];
            if (!cand->active || cand->team == creep->team || !hero_is_hittable(cand)) continue;
            float dx = cand->x - creep->x, dz = cand->z - creep->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_LANE_CREEP_AGGRO_RADIUS) continue;
            if (!nearest_hero || dist < hero_dist) { nearest_hero = cand; hero_dist = dist; }
        }

        ArenaLaneCreep *nearest_creep = NULL;
        float creep_dist = 0.0f;
        for (int c = 0; c < ARENA_MAX_LANE_CREEPS; c++) {
            if (c == i) continue;
            ArenaLaneCreep *cand = &arena_state.lane_creeps[c];
            if (!cand->active || !cand->alive || cand->team == creep->team) continue;
            float dx = cand->x - creep->x, dz = cand->z - creep->z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > ARENA_LANE_CREEP_AGGRO_RADIUS) continue;
            if (!nearest_creep || dist < creep_dist) { nearest_creep = cand; creep_dist = dist; }
        }

        ArenaHero *atk_hero = NULL;
        ArenaLaneCreep *atk_creep = NULL;
        if (nearest_hero && (!nearest_creep || hero_dist <= creep_dist)) atk_hero = nearest_hero;
        else if (nearest_creep) atk_creep = nearest_creep;

        if ((atk_hero || atk_creep) && creep->attack_cooldown_ms <= 0) {
            if (atk_hero) {
                apply_damage(atk_hero, ARENA_LANE_CREEP_DAMAGE); /* no armor stat on lane-creep attacks, same as jungle creeps */
            } else {
                atk_creep->hp -= ARENA_LANE_CREEP_DAMAGE;
                if (atk_creep->hp <= 0) { atk_creep->hp = 0; atk_creep->alive = 0; atk_creep->active = 0; }
            }
            creep->attack_cooldown_ms = ARENA_LANE_CREEP_ATTACK_COOLDOWN_MS;
            continue; /* stopped to fight -- no movement this tick */
        }
        if (atk_hero || atk_creep) continue; /* mid-swing (cooldown not ready yet) -- holds position, still doesn't march */

        float wx, wz;
        lane_creep_waypoint(creep->team, creep->waypoint_index, &wx, &wz);
        float dx = wx - creep->x, dz = wz - creep->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist < ARENA_LANE_CREEP_WAYPOINT_EPSILON) {
            if (creep->waypoint_index >= ARENA_LANE_WAYPOINT_COUNT - 1) {
                /* Reached the enemy spawn line with nothing left to fight --
                   no structure/tower exists yet for the wave to actually
                   push against (see the header comment), so it despawns
                   here rather than damaging anything, flagged not faked. */
                creep->alive = 0;
                creep->active = 0;
            } else {
                creep->waypoint_index++;
            }
        } else {
            float step = ARENA_LANE_CREEP_SPEED * dt_sec;
            if (step >= dist) { creep->x = wx; creep->z = wz; }
            else { creep->x += dx / dist * step; creep->z += dz / dist * step; }
        }
    }
}

/* arena_hero_attack_lane_creeps (S170-138): see the header declaration's doc
 * comment. */
void arena_hero_attack_lane_creeps(unsigned int dt_ms) {
    (void)dt_ms; /* attack_cooldown_ms is ticked in tick_hero_kit/resolve_combat already; this only spends it, same idiom as arena_hero_attack_creeps */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || !h->alive || h->attack_cooldown_ms > 0) continue;

        ArenaHero *foe = arena_nearest_enemy(i);
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_ATTACK_RANGE) continue; /* already busy with an enemy hero this tick */
        }

        for (int c = 0; c < ARENA_MAX_LANE_CREEPS; c++) {
            ArenaLaneCreep *creep = &arena_state.lane_creeps[c];
            if (!creep->active || !creep->alive || creep->team == h->team) continue;
            float dx = creep->x - h->x, dz = creep->z - h->z;
            if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;

            creep->hp -= ARENA_ATTACK_DAMAGE; /* no armor stat on lane creeps, same as jungle creeps */
            h->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
            if (creep->hp <= 0) {
                creep->hp = 0;
                creep->alive = 0;
                creep->active = 0;
            }
            break; /* one creep target per hero per attack, same as jungle creeps/hero-vs-hero */
        }
    }
}

static void resolve_combat(unsigned int dt_ms) {
    ArenaHero *a = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    if (a->attack_cooldown_ms > 0) a->attack_cooldown_ms -= (int)dt_ms;
    if (b->attack_cooldown_ms > 0) b->attack_cooldown_ms -= (int)dt_ms;
    if (!a->alive || !b->alive) return;

    float dx = b->x - a->x;
    float dz = b->z - a->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > ARENA_ATTACK_RANGE) return;

    if (a->attack_cooldown_ms <= 0) {
        if (hero_is_hittable(b)) apply_damage(b, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(b)));
        a->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
    if (b->attack_cooldown_ms <= 0) {
        if (hero_is_hittable(a)) apply_damage(a, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(a)));
        b->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }
}

/* --- Kit dispatch (S170-31 generalized this from S170-18's Unicorn-only,
   owner-hardcoded version -- everything below dispatches on hero_id, so
   either owner slot can carry either hero). --- */

static void unicorn_cast_q(ArenaHero *h, ArenaHero *foe) {
    /* Dash toward the current move target if moving, else toward the foe --
       a dash ability needs a direction, and "toward whatever you last
       clicked, or the enemy if you didn't" is the simplest honest default.
       foe may be NULL in team mode (no living enemy at all right now) --
       fall back to "moving" only in that case; if neither gives a
       direction, there's nothing to dash toward, so just no-op. */
    float dx, dz;
    if (h->moving) {
        dx = h->target_x - h->x;
        dz = h->target_z - h->z;
    } else if (foe) {
        dx = foe->x - h->x;
        dz = foe->z - h->z;
    } else {
        return;
    }
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return; /* no meaningful direction, e.g. already on top of foe */
    dx /= len; dz /= len;

    float nx = h->x + dx * ARENA_UNICORN_Q_DASH_DIST;
    float nz = h->z + dz * ARENA_UNICORN_Q_DASH_DIST;
    if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
    if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
    if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
    if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
    h->x = nx;
    h->z = nz;
    h->moving = 0;

    if (foe && hero_is_hittable(foe)) {
        float fdx = foe->x - h->x, fdz = foe->z - h->z;
        if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_UNICORN_Q_HIT_RADIUS) {
            apply_damage(foe, apply_armor(ARENA_UNICORN_Q_DAMAGE, arena_hero_armor(foe)));
        }
    }
    h->q_cooldown_ms = cast_cooldown(h, ARENA_UNICORN_Q_COOLDOWN_MS);
    h->mp -= ARENA_MP_COST_Q;
}

/* duck_pull_foe: shared logic for Telekinetic Yank (Q) and the bigger
 * Total Telekinesis (R) -- both pull the foe toward the Duck by pull_dist
 * (clamped so it can't overshoot past the Duck) and deal damage, only if
 * the foe starts out within max_range. Returns 1 if it landed (so the
 * caller only consumes the cooldown on an actual hit, not a whiff), 0 if
 * the foe was out of range or currently unhittable (e.g. Ghost's W). */
static int duck_pull_foe(ArenaHero *duck, ArenaHero *foe, float pull_dist, int damage, float max_range) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = duck->x - foe->x;
    float dz = duck->z - foe->z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > max_range) return 0; /* out of range -- no whiff-damage, no partial pull */
    /* rooted_ms (S170-46): Tree's Grand Secret is "immune to displacement" --
       the pull is skipped but damage still lands, same as any other root not
       blocking incoming damage. */
    if (dist > 0.01f && foe->rooted_ms <= 0) {
        float pull = pull_dist < dist ? pull_dist : dist; /* never pull the foe past the Duck */
        foe->x += dx / dist * pull;
        foe->z += dz / dist * pull;
    }
    apply_damage(foe, apply_armor(damage, arena_hero_armor(foe)));
    return 1;
}

/* ghost_cast_q: Alien Frequency. S170-140: converted from an instant
 * hit-if-in-range check to a real travelling projectile (docs/HEROES_VS0.md
 * already calls this a "skillshot" -- it just wasn't implemented as one
 * until now), same "requires a real shot lined up at cast time, but landing
 * it is no longer guaranteed" convention as Gary's Q (S170-136). Returns 1
 * once the shot is fired (cooldown spent either way, same as any real
 * skill-shot), 0 if there was no hittable foe in range to fire at at all. */
static int ghost_cast_q(ArenaHero *ghost, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - ghost->x, dz = foe->z - ghost->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GHOST_Q_RANGE) return 0;
    ArenaProjectile *p = arena_spawn_projectile(ghost->owner, ghost->team, ARENA_HERO_GHOST,
                             ghost->x, ghost->z, foe->x, foe->z,
                             ARENA_GHOST_Q_PROJECTILE_SPEED, ARENA_GHOST_Q_PROJECTILE_RADIUS,
                             ARENA_GHOST_Q_DAMAGE, ARENA_GHOST_Q_RANGE);
    if (p) p->on_hit_silence_ms = ARENA_GHOST_Q_SILENCE_MS;
    return 1;
}

/* frog_cast_q: Loop Back, rewinds the Frog's own position/HP to
 * ARENA_FROG_Q_REWIND_MS ago using the loopback ring buffer any hero
 * accumulates in tick_hero_kit. Self-targeted, so it always "lands" once
 * called (unlike Duck/Ghost, there's no range/hittability check -- you
 * can't whiff a rewind on yourself). If less history exists than the full
 * rewind window (e.g. cast in the first few seconds of a match), rewinds
 * as far back as is actually recorded rather than refusing to cast at all. */
static void frog_cast_q(ArenaHero *frog) {
    if (frog->loopback_count == 0) return; /* no history yet at all */
    int slots_back = ARENA_FROG_Q_REWIND_MS / ARENA_FROG_LOOPBACK_SAMPLE_MS;
    if (slots_back >= frog->loopback_count) slots_back = frog->loopback_count - 1;
    int idx = ((frog->loopback_next_slot - 1 - slots_back) % ARENA_FROG_LOOPBACK_SLOTS
               + ARENA_FROG_LOOPBACK_SLOTS) % ARENA_FROG_LOOPBACK_SLOTS;
    frog->x = frog->loopback_x[idx];
    frog->z = frog->loopback_z[idx];
    frog->hp = frog->loopback_hp[idx];
    frog->moving = 0;
}

/* doc_wheel_heal_amount: Extremely Good At Medicine -- linearly scales from
 * ARENA_DOC_WHEEL_Q_HEAL_BASE at 100% target HP up to
 * ARENA_DOC_WHEEL_Q_HEAL_LOW_HP at 0% target HP (S170-45). */
static int doc_wheel_heal_amount(const ArenaHero *target) {
    if (target->max_hp <= 0) return ARENA_DOC_WHEEL_Q_HEAL_BASE;
    float hp_pct = (float)target->hp / (float)target->max_hp;
    if (hp_pct < 0.0f) hp_pct = 0.0f;
    if (hp_pct > 1.0f) hp_pct = 1.0f;
    float heal = ARENA_DOC_WHEEL_Q_HEAL_BASE +
                 (ARENA_DOC_WHEEL_Q_HEAL_LOW_HP - ARENA_DOC_WHEEL_Q_HEAL_BASE) * (1.0f - hp_pct);
    return (int)heal;
}

static void doc_wheel_heal_and_cleanse(ArenaHero *target, int amount) {
    target->hp += amount;
    if (target->hp > target->max_hp) target->hp = target->max_hp;
    target->silenced_ms = 0; /* Bedside Manner: "cleanses one debuff" -- the only debuff arena has today */
}

/* tree_cast_q: Vine Lash, simplified from "AoE root in a cone in front" to
 * an instant hit-if-in-range check, same precedent as Ghost's Alien
 * Frequency. Returns 1 if it landed (cooldown only consumed on a hit), 0 on
 * a whiff. */
static int tree_cast_q(ArenaHero *tree, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - tree->x, dz = foe->z - tree->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_TREE_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_TREE_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_TREE_Q_ROOT_MS;
    return 1;
}

/* pizza_cast_q: Nobody Checked, simplified from "throw a burning slice +
 * ground patch" to direct damage + a burn DoT applied straight to the foe --
 * no persistent ground-hazard system exists in this arena, so the
 * lingering-patch half is dropped, not faked. Returns 1 if it landed, 0 on
 * a whiff. */
static int pizza_cast_q(ArenaHero *pizza, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - pizza->x, dz = foe->z - pizza->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_PIZZA_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_PIZZA_Q_DAMAGE, arena_hero_armor(foe)));
    foe->burning_ms = ARENA_PIZZA_Q_BURN_MS;
    foe->burn_dps = ARENA_PIZZA_Q_BURN_DPS;
    return 1;
}

/* flamel_cast_q: Vine Growth (absorbed from the former Druid), simplified
 * from "wall of vines in a line" to an instant root-if-in-range check on
 * the nearest enemy -- same cone/line-to-single-target simplification as
 * Tree's Q. Pure crowd control, no damage, matching the doc's own ability
 * (blocks movement, nothing else). Returns 1 if it landed, 0 on a whiff. */
static int flamel_cast_q(ArenaHero *flamel, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - flamel->x, dz = foe->z - flamel->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_FLAMEL_Q_RANGE) return 0;
    foe->rooted_ms = ARENA_FLAMEL_Q_ROOT_MS;
    return 1;
}

/* flamel_cast_w: Philosopher's Bloom (Bloom + Philosopher's Batch merged,
 * S170-46) -- heals every living ally within radius at once, healing for
 * more if Flamel himself is standing within capture radius of a node his
 * own team has marked (Overgrowth, absorbed from Druid). Always "lands"
 * and consumes the cooldown, same always-commits convention as Doc Wheel's
 * R -- an AoE heal isn't a single-target poke that can whiff. */
static void flamel_cast_w(ArenaHero *flamel, int owner) {
    int on_marked_ground = 0;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        if (node->marked_by_team != flamel->team) continue;
        float ndx = flamel->x - node->x, ndz = flamel->z - node->z;
        if (sqrtf(ndx * ndx + ndz * ndz) <= ARENA_NODE_CAPTURE_RADIUS) { on_marked_ground = 1; break; }
    }
    int heal = on_marked_ground ? ARENA_FLAMEL_W_HEAL_MARKED : ARENA_FLAMEL_W_HEAL_BASE;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *ally = &arena_state.heroes[i];
        if (i == owner || !ally->active || !ally->alive) continue;
        if (ally->team != flamel->team) continue;
        float dx = ally->x - flamel->x, dz = ally->z - flamel->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_FLAMEL_W_RADIUS) continue;
        ally->hp += heal;
        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
    }
}

/* execute_scale_damage: linearly scales from base_dmg at 100% target HP up
 * to low_hp_dmg at ~0% target HP -- same shape as doc_wheel_heal_amount,
 * inverted for damage instead of healing (Morrigan's death-omen kit,
 * S170-47: "the crow confirms the kill"). */
static int execute_scale_damage(const ArenaHero *target, int base_dmg, int low_hp_dmg) {
    if (target->max_hp <= 0) return base_dmg;
    float hp_pct = (float)target->hp / (float)target->max_hp;
    if (hp_pct < 0.0f) hp_pct = 0.0f;
    if (hp_pct > 1.0f) hp_pct = 1.0f;
    float dmg = base_dmg + (low_hp_dmg - base_dmg) * (1.0f - hp_pct);
    return (int)dmg;
}

/* morrigan_cast_q: The Washer's Strike, instant hit-if-in-range (same
 * precedent as Ghost/Tree/Pizza's Q), execute-scaled via
 * execute_scale_damage. Returns 1 if it landed, 0 on a whiff. */
static int morrigan_cast_q(ArenaHero *morrigan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - morrigan->x, dz = foe->z - morrigan->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_MORRIGAN_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(execute_scale_damage(foe, ARENA_MORRIGAN_Q_DAMAGE_BASE, ARENA_MORRIGAN_Q_DAMAGE_LOW_HP),
                                   arena_hero_armor(foe)));
    return 1;
}

/* morrigan_cast_w: Three Forms -- teleports to the nearest enemy's position
 * and roots them on arrival ("she appears where he doesn't expect, in
 * another form"). No range check -- a sudden appearance, not a skillshot.
 * Returns 1 if it landed, 0 with no living enemy at all. */
static int morrigan_cast_w(ArenaHero *morrigan, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    morrigan->x = foe->x;
    morrigan->z = foe->z;
    morrigan->moving = 0;
    foe->rooted_ms = ARENA_MORRIGAN_W_ROOT_MS;
    return 1;
}

/* dagda_cast_q: "the same tool, either direction, depending only on which
 * end swings first" -- built literally. A hittable enemy in range takes
 * priority (the killing end); absent that, a hurt living ally in range
 * gets the reviving end, simplified to a heal since no respawn system
 * exists to revive a dead ally into. Returns 1 if either end landed, 0 on
 * a full whiff (nothing valid in range at all). */
static int dagda_cast_q(ArenaHero *dagda, ArenaHero *foe, ArenaHero *ally) {
    if (foe && hero_is_hittable(foe)) {
        float dx = foe->x - dagda->x, dz = foe->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_DAGDA_Q_RANGE) {
            apply_damage(foe, apply_armor(ARENA_DAGDA_Q_KILL_DAMAGE, arena_hero_armor(foe)));
            return 1;
        }
    }
    if (ally && ally->alive && ally->hp < ally->max_hp) {
        float dx = ally->x - dagda->x, dz = ally->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_DAGDA_Q_RANGE) {
            ally->hp += ARENA_DAGDA_Q_REVIVE_HEAL;
            if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
            return 1;
        }
    }
    return 0;
}

/* dagda_cast_w: Uaithne, called by name -- all three master strains played
 * over the whole hall in one go. One AoE cast, everyone in radius
 * experiences a different strain depending on side: allies get joy (heal),
 * hittable enemies get sorrow+sleep (root+silence) at once. Always lands
 * and consumes the cooldown, same always-commits convention as other AoE
 * ultimates in this roster (Doc Wheel's R) -- a hall-filling cast isn't a
 * single-target poke that can whiff. */
static void dagda_cast_w(ArenaHero *dagda, int owner) {
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *other = &arena_state.heroes[i];
        if (i == owner || !other->active || !other->alive) continue;
        float dx = other->x - dagda->x, dz = other->z - dagda->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_DAGDA_W_RADIUS) continue;
        if (other->team == dagda->team) {
            other->hp += ARENA_DAGDA_W_ALLY_HEAL;
            if (other->hp > other->max_hp) other->hp = other->max_hp;
        } else if (hero_is_hittable(other)) {
            other->rooted_ms = ARENA_DAGDA_W_ROOT_MS;
            other->silenced_ms = ARENA_DAGDA_W_SILENCE_MS;
        }
    }
}

/* courier_cast_q: The Insult, Lightly Edited -- dashes a fixed distance
 * toward the nearest enemy (same shape as Unicorn's Diagnostic Charge:
 * fixed dash length, not clamped to the foe's own distance, so it can
 * overshoot past a close target same as Unicorn's does) and deals damage
 * if it lands within hit radius. Also cleanses The Courier's own active
 * debuffs on cast (Lightly Edited passive) -- "editing the message"
 * addressed back to him, regardless of whether the damage half connects.
 * Returns 1 if there was a living enemy to dash toward at all, 0 if not. */
static int courier_cast_q(ArenaHero *courier, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - courier->x, dz = foe->z - courier->z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 0.01f) {
        float nx = courier->x + dx / len * ARENA_COURIER_Q_DASH_DIST;
        float nz = courier->z + dz / len * ARENA_COURIER_Q_DASH_DIST;
        if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
        if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
        if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
        if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
        courier->x = nx;
        courier->z = nz;
        courier->moving = 0;
    }
    float fdx = foe->x - courier->x, fdz = foe->z - courier->z;
    if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_COURIER_Q_HIT_RADIUS) {
        apply_damage(foe, apply_armor(ARENA_COURIER_Q_DAMAGE, arena_hero_armor(foe)));
    }
    courier->silenced_ms = 0;
    courier->rooted_ms = 0;
    return 1;
}

/* courier_toggle_w: Between Eagle and Serpent -- instantly repositions to
 * whichever map node is farthest from The Courier's current position,
 * always making real progress "along the tree" rather than bouncing back
 * and forth to the same one. Pure fixed-geography teleport, distinct from
 * every other hero's ally/foe-relative one. Always lands (there is always
 * at least one node) -- no whiff case. S170-119: generalized from a
 * hardcoded "farther of the two nodes" to farthest-of-N when the map grew
 * from 2 nodes to 5 -- the "always real progress" property holds the same
 * way for any N. */
static void courier_toggle_w(ArenaHero *courier) {
    int target = 0;
    float best_dist = -1.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        float dx = arena_state.nodes[n].x - courier->x, dz = arena_state.nodes[n].z - courier->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > best_dist) { best_dist = dist; target = n; }
    }
    courier->x = arena_state.nodes[target].x;
    courier->z = arena_state.nodes[target].z;
    courier->moving = 0;
}

/* courier_cast_r: The Debt Collector's Due -- a flat life-drain execute on
 * the nearest enemy in range. "A job that was never meant to involve
 * judgment, and has, over a very long tenure, started to" -- The Courier
 * takes a cut off what passes through him by force. Returns 1 if it
 * landed, 0 on a whiff. */
static int courier_cast_r(ArenaHero *courier, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - courier->x, dz = foe->z - courier->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_COURIER_R_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_COURIER_R_DRAIN, arena_hero_armor(foe)));
    courier->hp += ARENA_COURIER_R_DRAIN;
    if (courier->hp > courier->max_hp) courier->hp = courier->max_hp;
    return 1;
}

/* loki_cast_q: Interference, Not a Signal -- an instant positional swap with
 * the nearest enemy, no travel time (unlike every dash-shaped Q in this
 * file). Loki simply arrives where the enemy was and the enemy where he
 * was, then a small hit lands on arrival if the swap put them in range of
 * each other anyway. Returns 1 if there was a living enemy to swap with. */
static int loki_cast_q(ArenaHero *loki, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float ox = loki->x, oz = loki->z;
    loki->x = foe->x;
    loki->z = foe->z;
    foe->x = ox;
    foe->z = oz;
    loki->moving = 0;
    float fdx = foe->x - loki->x, fdz = foe->z - loki->z;
    if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_LOKI_Q_HIT_RADIUS) {
        apply_damage(foe, apply_armor(ARENA_LOKI_Q_DAMAGE, arena_hero_armor(foe)));
    }
    return 1;
}

/* loki_cast_r: Held For As Long As The Myth Demands -- self-cast survive
 * floor, same mechanic Pizza/Dagda already use (S170-46), reused here as
 * Sigyn's endurance rather than either of their reasons for it. */
static void loki_cast_r(ArenaHero *loki) {
    loki->survive_floor_ms = ARENA_LOKI_R_FLOOR_MS;
}

/* gary_cast_q: The Property -- a stationary long-range precision shot at the nearest enemy.
 * No dash, no movement at all (unlike every other Q in this file) -- Gary doesn't chase, he
 * watches from where he's standing. Range is longer while W (Watching the Bridge) is toggled
 * on. Returns 1 if a living enemy was in range, 0 on a whiff (range gates this one, not a
 * hit-radius after a dash, since there's no dash to begin with).
 *
 * S170-136: no longer an instant hit -- fires a real projectile straight at
 * the foe's position at cast time (see arena_spawn_projectile). The cast
 * still requires a hittable foe within range at the MOMENT of casting (Gary
 * has to actually have a shot lined up to fire at all -- he's not spraying
 * blind), but landing the hit is no longer guaranteed: the foe can step off
 * the line before the shot arrives. Cooldown is spent on cast either way,
 * same as a real skill-shot -- you don't get it back just because you
 * missed. */
static int gary_cast_q(ArenaHero *gary, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float range = gary->w_active ? ARENA_GARY_Q_RANGE_WATCHING : ARENA_GARY_Q_RANGE;
    float dx = foe->x - gary->x, dz = foe->z - gary->z;
    if (sqrtf(dx * dx + dz * dz) > range) return 0;
    arena_spawn_projectile(gary->owner, gary->team, ARENA_HERO_GARY,
                           gary->x, gary->z, foe->x, foe->z,
                           ARENA_GARY_Q_PROJECTILE_SPEED, ARENA_GARY_Q_PROJECTILE_RADIUS,
                           ARENA_GARY_Q_DAMAGE, range);
    return 1;
}

/* gary_cast_r: "Slow Down, This Isn't a Track Meet" -- a fixed-duration root on the nearest
 * enemy, the same "slow simplified to a full stop" convention Tree's R/Flamel's R already use
 * rather than adding a real speed-multiplier system. Returns 1 if it landed. */
static int gary_cast_r(ArenaHero *gary, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - gary->x, dz = foe->z - gary->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GARY_R_RANGE) return 0;
    foe->rooted_ms = ARENA_GARY_R_ROOT_MS;
    return 1;
}

/* flute_debt_cast_q: The Wrong Note -- modest immediate damage plus the shared burning_ms/
 * burn_dps DoT fields (S170-46, already generically ticked by tick_hero_kit for any hero),
 * standing in for the debt accruing. Returns 1 if it landed. */
static int flute_debt_cast_q(ArenaHero *fd, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - fd->x, dz = foe->z - fd->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_FLUTE_DEBT_Q_HIT_RADIUS) return 0;
    apply_damage(foe, apply_armor(ARENA_FLUTE_DEBT_Q_DAMAGE, arena_hero_armor(foe)));
    foe->burning_ms = ARENA_FLUTE_DEBT_Q_BURN_MS;
    foe->burn_dps = ARENA_FLUTE_DEBT_Q_BURN_DPS;
    return 1;
}

/* flute_debt_cast_r: Eventually Collects -- always lands and consumes the cooldown (same
 * "always commits" convention as Doc Wheel's/Flamel's R), but deals real bonus damage if the
 * target still has the Q's debt (burning_ms > 0) active, base damage otherwise. The actual
 * payoff of the kit's whole theme: the debt has to still be open for it to collect big. */
static void flute_debt_cast_r(ArenaHero *fd, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return;
    float dx = foe->x - fd->x, dz = foe->z - fd->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_FLUTE_DEBT_R_RANGE) return;
    int amount = (foe->burning_ms > 0) ? ARENA_FLUTE_DEBT_R_DAMAGE_DEBT : ARENA_FLUTE_DEBT_R_DAMAGE_BASE;
    apply_damage(foe, apply_armor(amount, arena_hero_armor(foe)));
}

/* bacon_puck_cast_q: Ask Again Later -- self intangible_ms, the shared can't-be-hit status
 * (S170-32), for longer while W is toggled on. Always "lands" (there's no foe/range check --
 * it's purely self-targeted), same as Ghost's W/Frog's R. */
static void bacon_puck_cast_q(ArenaHero *bp) {
    bp->intangible_ms = bp->w_active ? ARENA_BACON_PUCK_Q_INTANGIBLE_MS_WATCHING
                                      : ARENA_BACON_PUCK_Q_INTANGIBLE_MS;
}

/* bacon_puck_cast_r: The Trick Was Always the Same -- real damage plus a self-heal off a
 * fraction of it. Returns 1 if it landed. */
static int bacon_puck_cast_r(ArenaHero *bp, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - bp->x, dz = foe->z - bp->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_BACON_PUCK_R_RANGE) return 0;
    int dmg = apply_armor(ARENA_BACON_PUCK_R_DAMAGE, arena_hero_armor(foe));
    apply_damage(foe, dmg);
    bp->hp += (int)(dmg * ARENA_BACON_PUCK_R_HEAL_PCT);
    if (bp->hp > bp->max_hp) bp->hp = bp->max_hp;
    return 1;
}

/* abraham_cast_q: The Sacred Magic -- a real ranged magic bolt, stronger while W (channeling
 * the book) is toggled on. Returns 1 if it landed. */
static int abraham_cast_q(ArenaHero *abraham, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - abraham->x, dz = foe->z - abraham->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ABRAHAM_Q_RANGE) return 0;
    int dmg = abraham->w_active ? ARENA_ABRAHAM_Q_DAMAGE_CHANNELING : ARENA_ABRAHAM_Q_DAMAGE;
    apply_damage(foe, apply_armor(dmg, arena_hero_armor(foe)));
    return 1;
}

/* abraham_cast_r: The Guardian Angel, Contacted -- a full self-cleanse (every debuff field
 * this roster tracks) plus a real heal, the ritual's actual promised payoff. Always "lands"
 * (self-targeted, no foe check) -- same always-commits convention as Doc Wheel's/Flamel's R. */
static void abraham_cast_r(ArenaHero *abraham) {
    abraham->silenced_ms = 0;
    abraham->rooted_ms = 0;
    abraham->burning_ms = 0;
    abraham->burn_dps = 0;
    abraham->burn_tick_ms = 0;
    abraham->hp += ARENA_ABRAHAM_R_HEAL;
    if (abraham->hp > abraham->max_hp) abraham->hp = abraham->max_hp;
}

/* ada_cast_q: computes the nearest enemy's movement to a halt -- a real root, same "slow
 * simplified to a stop" convention Tree's/Flamel's/Gary's R already use, here on Q instead.
 * Returns 1 if it landed. */
static int ada_cast_q(ArenaHero *ada, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - ada->x, dz = foe->z - ada->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ADA_Q_RANGE) return 0;
    foe->rooted_ms = ARENA_ADA_Q_ROOT_MS;
    return 1;
}

/* ada_cast_r: The First Program, Run a Century Late -- the engine finally executes: real
 * damage plus a short follow-up root. Returns 1 if it landed. */
static int ada_cast_r(ArenaHero *ada, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - ada->x, dz = foe->z - ada->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_ADA_R_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_ADA_R_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_ADA_R_ROOT_MS;
    return 1;
}

/* tyler_cast_q: Earthbind -- roots + a DoT (Geostrike's poison, folded in here since there's
 * no generic per-melee-attack passive hook to hang it off separately). Returns 1 if it landed. */
static int tyler_cast_q(ArenaHero *tyler, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - tyler->x, dz = foe->z - tyler->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_TYLER_Q_RANGE) return 0;
    ArenaProjectile *p = arena_spawn_projectile(tyler->owner, tyler->team, ARENA_HERO_TYLER,
                             tyler->x, tyler->z, foe->x, foe->z,
                             ARENA_TYLER_Q_PROJECTILE_SPEED, ARENA_TYLER_Q_PROJECTILE_RADIUS,
                             ARENA_TYLER_Q_DAMAGE, ARENA_TYLER_Q_RANGE);
    if (p) {
        p->on_hit_root_ms = ARENA_TYLER_Q_ROOT_MS;
        p->on_hit_burn_ms = ARENA_TYLER_Q_BURN_MS;
        p->on_hit_burn_dps = ARENA_TYLER_Q_BURN_DPS;
    }
    return 1;
}

/* tyler_cast_w: Poof -- an instant blink to the nearest enemy plus real damage on arrival.
 * One body, one blink, not "every clone" -- see the header comment on why. Returns 1 if a
 * living enemy was there to blink to. */
static int tyler_cast_w(ArenaHero *tyler, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    tyler->x = foe->x;
    tyler->z = foe->z;
    tyler->moving = 0;
    float fdx = foe->x - tyler->x, fdz = foe->z - tyler->z;
    if (sqrtf(fdx * fdx + fdz * fdz) <= ARENA_TYLER_W_HIT_RADIUS) {
        apply_damage(foe, apply_armor(ARENA_TYLER_W_DAMAGE, arena_hero_armor(foe)));
    }
    return 1;
}

/* tyler_spawn_clones (S170-141): claims up to ARENA_TYLER_R_CLONE_COUNT free
 * slots from the dedicated puppet-clone range (ARENA_MAX_HEROES..
 * ARENA_HEROES_ARRAY_SIZE-1 -- never a real client's slot, see that
 * constant's own doc comment) and spawns each as a real, fightable
 * ArenaHero at Tyler's own position, sharing his team and hero_id (so it
 * renders identically to Tyler client-side, no new visual needed). Spawns
 * fewer than the full count if the pool is short on free slots rather than
 * refusing the whole cast -- same "generous headroom, graceful if it's ever
 * tight" tone as arena_spawn_projectile's own pool-exhaustion handling. */
static void tyler_spawn_clones(ArenaHero *tyler) {
    int tyler_owner = (int)(tyler - arena_state.heroes);
    int spawned = 0;
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE && spawned < ARENA_TYLER_R_CLONE_COUNT; i++) {
        ArenaHero *clone = &arena_state.heroes[i];
        if (clone->active) continue;
        memset(clone, 0, sizeof(*clone));
        clone->active = 1;
        clone->alive = 1;
        clone->is_clone = 1;
        clone->clone_owner = tyler_owner;
        clone->team = tyler->team;
        clone->hero_id = ARENA_HERO_TYLER;
        clone->owner = i;
        clone->x = clone->target_x = tyler->x;
        clone->z = clone->target_z = tyler->z;
        clone->max_hp = (int)(tyler->max_hp * ARENA_TYLER_CLONE_HP_PCT);
        clone->hp = clone->max_hp;
        spawned++;
    }
}

/* tyler_cast_r: Divided We Stand. S170-141: real puppet clones on top of the
 * existing self-buff (see docs/HEROES_VS0.md's Tyler section for the full
 * design/scope note) -- hits hard right now, stays more fragile (own armor
 * goes negative -- see arena_hero_armor()) for the window after, AND
 * spawns the clone army. Always "lands" (self-buff + clones) even on a
 * whiff against the foe check, same convention as before. */
static void tyler_cast_r(ArenaHero *tyler, ArenaHero *foe) {
    if (hero_is_hittable(foe)) {
        float dx = foe->x - tyler->x, dz = foe->z - tyler->z;
        if (sqrtf(dx * dx + dz * dz) <= ARENA_TYLER_R_RANGE) {
            apply_damage(foe, apply_armor(ARENA_TYLER_R_DAMAGE, arena_hero_armor(foe)));
        }
    }
    tyler->r_active_ms = ARENA_TYLER_R_VULNERABLE_MS;
    tyler_spawn_clones(tyler);
}

/* paimon_cast_q: Teaches All Arts -- a ranged bolt that damages and roots, same instant-hit-if-
 * in-range simplification as Ghost's/Tree's/Flamel's Q. Returns 1 if it landed. */
static int paimon_cast_q(ArenaHero *paimon, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - paimon->x, dz = foe->z - paimon->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_PAIMON_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_PAIMON_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_PAIMON_Q_ROOT_MS;
    return 1;
}

/* paimon_cast_w: Speaks With Total Authority -- an instant decree, damage + silence, same shape
 * as Ghost's Q but on the W slot with its own cooldown. Returns 1 if it landed. */
static int paimon_cast_w(ArenaHero *paimon, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - paimon->x, dz = foe->z - paimon->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_PAIMON_W_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_PAIMON_W_DAMAGE, arena_hero_armor(foe)));
    foe->silenced_ms = ARENA_PAIMON_W_SILENCE_MS;
    return 1;
}

/* noor1_cast_q: File What Is Actually There -- a ranged bolt that damages and roots, same
 * instant-hit-if-in-range shape as Paimon's/Ghost's/Tree's Q. Returns 1 if it landed. */
static int noor1_cast_q(ArenaHero *noor1, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - noor1->x, dz = foe->z - noor1->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_NOOR1_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_NOOR1_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_NOOR1_Q_ROOT_MS;
    return 1;
}

/* cain_cast_q: The First Murder -- instant hit-if-in-range, execute-scaled via
 * execute_scale_damage, same shape as Morrigan's Q. Returns 1 if it landed. */
static int cain_cast_q(ArenaHero *cain, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - cain->x, dz = foe->z - cain->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_CAIN_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(execute_scale_damage(foe, ARENA_CAIN_Q_DAMAGE_BASE, ARENA_CAIN_Q_DAMAGE_LOW_HP),
                                   arena_hero_armor(foe)));
    return 1;
}

/* cain_cast_w: Cursed to Wander -- dashes a fixed distance directly AWAY from the nearest enemy
 * (the mirror of Courier's Q, which dashes toward) and cleanses self debuffs, same self-cleanse
 * as Courier's own Q. Works even with no foe present (still cleanses, just doesn't reposition) --
 * always returns 1, this is a self-only effect that can't whiff the way a targeted cast can. */
static int cain_cast_w(ArenaHero *cain, ArenaHero *foe) {
    if (foe) {
        float dx = cain->x - foe->x, dz = cain->z - foe->z;
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 0.01f) {
            float nx = cain->x + dx / len * ARENA_CAIN_W_DASH_DIST;
            float nz = cain->z + dz / len * ARENA_CAIN_W_DASH_DIST;
            if (nx < -ARENA_HALF_EXTENT) nx = -ARENA_HALF_EXTENT;
            if (nx > ARENA_HALF_EXTENT) nx = ARENA_HALF_EXTENT;
            if (nz < -ARENA_HALF_EXTENT) nz = -ARENA_HALF_EXTENT;
            if (nz > ARENA_HALF_EXTENT) nz = ARENA_HALF_EXTENT;
            cain->x = nx;
            cain->z = nz;
            cain->moving = 0;
        }
    }
    cain->silenced_ms = 0;
    cain->rooted_ms = 0;
    return 1;
}

/* gunnr_cast_q: Argued With a Raven -- a plain melee-range correction, damage only, no status
 * effect. Returns 1 if it landed. */
static int gunnr_cast_q(ArenaHero *gunnr, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - gunnr->x, dz = foe->z - gunnr->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_GUNNR_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_GUNNR_Q_DAMAGE, arena_hero_armor(foe)));
    return 1;
}

/* vassago_cast_q: Reveal the Gentle Maybe -- a ranged bolt, damage + silence, same shape as
 * Ghost's Q. Returns 1 if it landed. */
static int vassago_cast_q(ArenaHero *vassago, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - vassago->x, dz = foe->z - vassago->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_VASSAGO_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_VASSAGO_Q_DAMAGE, arena_hero_armor(foe)));
    foe->silenced_ms = ARENA_VASSAGO_Q_SILENCE_MS;
    return 1;
}

/* he_xiangu_cast_q: Subsisting on Mother-of-Pearl and Moonlight -- a ranged bolt that heals her
 * for a fraction of the damage it deals, same heal-off-a-fraction mechanic as Bacon+Puck's R,
 * repeatable on Q instead of a one-off burst. Returns 1 if it landed. */
static int he_xiangu_cast_q(ArenaHero *he_xiangu, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - he_xiangu->x, dz = foe->z - he_xiangu->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_HE_XIANGU_Q_RANGE) return 0;
    int dmg = apply_armor(ARENA_HE_XIANGU_Q_DAMAGE, arena_hero_armor(foe));
    apply_damage(foe, dmg);
    he_xiangu->hp += (int)(dmg * ARENA_HE_XIANGU_Q_HEAL_PCT);
    if (he_xiangu->hp > he_xiangu->max_hp) he_xiangu->hp = he_xiangu->max_hp;
    return 1;
}

/* beleth_cast_q: a ranged bolt + burn, same shape as Pizza's Q (S170-46) -- damage that keeps
 * paying out after contact. Returns 1 if it landed. */
static int beleth_cast_q(ArenaHero *beleth, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - beleth->x, dz = foe->z - beleth->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_BELETH_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_BELETH_Q_DAMAGE, arena_hero_armor(foe)));
    foe->burning_ms = ARENA_BELETH_Q_BURN_MS;
    foe->burn_dps = ARENA_BELETH_Q_BURN_DPS;
    return 1;
}

/* beleth_cast_w: an instant decree, same in-range shape as Paimon's Speaks With Total
 * Authority (S170-55) but silence-only, no damage component -- pure escalation-denial.
 * Returns 1 if it landed. */
static int beleth_cast_w(ArenaHero *beleth, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - beleth->x, dz = foe->z - beleth->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_BELETH_W_RANGE) return 0;
    foe->silenced_ms = ARENA_BELETH_W_SILENCE_MS;
    return 1;
}

/* mnm_cast_q: a melee-range clamp+damage, same shape as Paimon's Q. Returns 1 if it landed. */
static int mnm_cast_q(ArenaHero *mnm, ArenaHero *foe) {
    if (!hero_is_hittable(foe)) return 0;
    float dx = foe->x - mnm->x, dz = foe->z - mnm->z;
    if (sqrtf(dx * dx + dz * dz) > ARENA_MNM_Q_RANGE) return 0;
    apply_damage(foe, apply_armor(ARENA_MNM_Q_DAMAGE, arena_hero_armor(foe)));
    foe->rooted_ms = ARENA_MNM_Q_ROOT_MS;
    return 1;
}

void arena_cast_q(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    ArenaHero *foe = arena_nearest_enemy(owner);
    if (!h->alive || h->silenced_ms > 0 || h->q_cooldown_ms > 0 || h->mp < ARENA_MP_COST_Q) return;
    h->cast_flash_slot = 1;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        unicorn_cast_q(h, foe);
        break;
    case ARENA_HERO_DUCK:
        if (duck_pull_foe(h, foe, ARENA_DUCK_Q_PULL_DIST, ARENA_DUCK_Q_DAMAGE, ARENA_DUCK_Q_RANGE)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DUCK_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_GHOST:
        if (ghost_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_GHOST_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_FROG:
        frog_cast_q(h);
        h->q_cooldown_ms = cast_cooldown(h, ARENA_FROG_Q_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_Q;
        break;
    case ARENA_HERO_DOC_WHEEL: {
        /* Bedside Manner: single-target heal + cleanse, on the nearest
           ally. No ally (1v1, or ally already dead) -- no-op, cooldown not
           consumed, same "whiff doesn't cost you the cooldown" convention
           as Duck/Ghost's Q. */
        ArenaHero *ally = arena_nearest_ally(owner);
        if (ally && ally->alive) {
            doc_wheel_heal_and_cleanse(ally, doc_wheel_heal_amount(ally));
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    }
    case ARENA_HERO_TREE:
        if (tree_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_TREE_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_PIZZA:
        if (pizza_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_PIZZA_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_FLAMEL:
        if (flamel_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_MORRIGAN:
        if (morrigan_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_DAGDA:
        if (dagda_cast_q(h, foe, arena_nearest_ally(owner))) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_COURIER:
        if (courier_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_COURIER_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_LOKI:
        if (loki_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_LOKI_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_GARY:
        if (gary_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_GARY_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        if (flute_debt_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_FLUTE_DEBT_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_BACON_PUCK:
        bacon_puck_cast_q(h);
        h->q_cooldown_ms = cast_cooldown(h, ARENA_BACON_PUCK_Q_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_Q;
        break;
    case ARENA_HERO_ABRAHAM:
        if (abraham_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_ABRAHAM_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_ADA:
        if (ada_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_ADA_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_TYLER:
        if (tyler_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_TYLER_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_PAIMON:
        if (paimon_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_PAIMON_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_NOOR1:
        if (noor1_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_NOOR1_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_CAIN:
        if (cain_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_CAIN_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_GUNNR:
        if (gunnr_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_GUNNR_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_VASSAGO:
        if (vassago_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_VASSAGO_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_HE_XIANGU:
        if (he_xiangu_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_HE_XIANGU_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_BELETH:
        if (beleth_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_BELETH_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    case ARENA_HERO_MNM:
        if (mnm_cast_q(h, foe)) {
            h->q_cooldown_ms = cast_cooldown(h, ARENA_MNM_Q_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_Q;
        }
        break;
    }
}

void arena_toggle_w(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    if (!h->alive || h->silenced_ms > 0) return;
    /* w_cooldown_ms is 0 (and never touched) for the pure-toggle heroes
       below, so this passes for them unconditionally -- correctly gates
       only the instant-cast-with-cooldown heroes (Ghost, Tyler, Paimon,
       etc.), whose own internal `if (w_cooldown_ms > 0) return;` a few
       lines into their case would otherwise let a blocked cast still flash. */
    if (h->w_cooldown_ms <= 0) h->cast_flash_slot = 2;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_GHOST:
        /* Not a Ghost: an instant-use buff on its own cooldown, not a
           toggle -- reuses the W slot but isn't a hold-on/hold-off state
           like Unicorn's regen. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        h->intangible_ms = ARENA_GHOST_W_INTANGIBLE_MS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_GHOST_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_FROG: {
        /* Borrowed Time: places the refund buff on the nearest ally --
           wired for real now that arena_nearest_ally exists (was skipped
           for no ally target in 1v1, S170-33). No-op, cooldown not
           consumed, if there's no living ally to target. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        ArenaHero *ally = arena_nearest_ally(owner);
        if (ally && ally->alive) {
            ally->next_cast_refund = 1;
            h->w_cooldown_ms = cast_cooldown(h, ARENA_FROG_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    }
    case ARENA_HERO_DOC_WHEEL:
        /* House Call: instant teleport to the nearest ally's location, on
           a long cooldown ("always shows up"). No-op if there's no ally. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        {
            ArenaHero *ally = arena_nearest_ally(owner);
            if (ally && ally->alive) {
                h->x = ally->x;
                h->z = ally->z;
                h->moving = 0;
                h->w_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_W_COOLDOWN_MS);
                h->mp -= ARENA_MP_COST_W;
            }
        }
        break;
    case ARENA_HERO_FLAMEL:
        /* Philosopher's Bloom: AoE ally heal, always lands (see
           flamel_cast_w) -- same always-commits convention as Doc Wheel's
           R, not a whiff-refunded single-target poke. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        flamel_cast_w(h, owner);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_MORRIGAN:
        /* Three Forms: gap-close + root on the nearest enemy. No-op,
           cooldown not consumed, if there's no living enemy at all
           (1v1's own bot could still die mid-match). */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (morrigan_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_DAGDA:
        /* Uaithne, called by name: AoE hits everyone in radius, always
           lands (see dagda_cast_w) -- same always-commits convention as
           Flamel's W above. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        dagda_cast_w(h, owner);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_COURIER:
        /* Between Eagle and Serpent: always lands, jumps to whichever
           of the ARENA_NODE_COUNT nodes is farthest right now. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        courier_toggle_w(h);
        h->w_cooldown_ms = cast_cooldown(h, ARENA_COURIER_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_LOKI:
        /* Bound Where the Myth Says: free toggle, no cooldown, same
           convention as Unicorn's W -- arena_hero_armor() reads w_active
           directly for the actual bonus. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_GARY:
        /* Watching the Bridge: free toggle, no cooldown -- gary_cast_q()
           reads w_active directly for Q's extended range, not a stat bonus. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_FLUTE_DEBT:
        /* Recouping Interest: free toggle self-heal-over-time, same shape
           as Unicorn's W -- see tick_hero_kit for the actual regen tick. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_BACON_PUCK:
        /* Which One Is The Real One: free toggle, no cooldown --
           bacon_puck_cast_q() reads w_active directly for Q's extended
           intangibility duration, not a stat bonus. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_ABRAHAM:
        /* The Book, Unattested: free toggle, no cooldown -- abraham_cast_q()
           reads w_active directly for Q's boosted damage while channeling. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_ADA:
        /* The frame's own plating: free toggle, no cooldown --
           arena_hero_armor() reads w_active directly for the bonus. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_TYLER:
        /* Poof: an instant-use blink-strike on its own cooldown, not a toggle --
           same shape as Ghost's W. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (tyler_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_TYLER_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_PAIMON:
        /* Speaks With Total Authority: instant decree on its own cooldown, not a toggle. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (paimon_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_PAIMON_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_NOOR1:
        /* Sent In Clean: same instant-use intangibility as Ghost's Not a Ghost --
           she goes quiet and unreadable herself for a moment. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        h->intangible_ms = ARENA_NOOR1_W_INTANGIBLE_MS;
        h->w_cooldown_ms = cast_cooldown(h, ARENA_NOOR1_W_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_CAIN:
        /* Cursed to Wander: instant-use dash-away + self-cleanse on its own cooldown. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (cain_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_CAIN_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_GUNNR:
        /* Three More Things: free toggle, no cooldown -- tick_hero_kit reads w_active
           directly for the regen, same shape as Flute Debt's Recouping Interest. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_VASSAGO: {
        /* The Soft Foresight, extended: grants the nearest ally next_cast_refund, same
           mechanic as Frog's Borrowed Time. No-op, cooldown not consumed, with no living
           ally to target (1v1 local demo). */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        ArenaHero *ally = arena_nearest_ally(owner);
        if (ally && ally->alive) {
            ally->next_cast_refund = 1;
            h->w_cooldown_ms = cast_cooldown(h, ARENA_VASSAGO_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    }
    case ARENA_HERO_HE_XIANGU:
        /* Self-Denial Taken Past the Point: free toggle, no cooldown -- tick_hero_kit
           reads w_active directly for the regen, same shape as Flute Debt's Recouping
           Interest. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return; /* insufficient MP to activate; toggling off is always free */
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    case ARENA_HERO_BELETH:
        /* Hope Is a Terror I Leash With Song: instant silence-only decree on its own
           cooldown, same in-range shape as Paimon's Speaks With Total Authority but with
           the damage stripped out -- escalation denied, not a hit landed. */
        if (h->w_cooldown_ms > 0 || h->mp < ARENA_MP_COST_W) return;
        if (beleth_cast_w(h, arena_nearest_enemy(owner))) {
            h->w_cooldown_ms = cast_cooldown(h, ARENA_BELETH_W_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_W;
        }
        break;
    case ARENA_HERO_MNM:
        /* Wasn't That Shape A Second Ago: free toggle bonus armor, same shape as Loki's/Ada's
           own -- arena_hero_armor() reads w_active directly for the bonus. */
        if (!h->w_active && h->mp < ARENA_MP_COST_W) return;
        h->w_active = !h->w_active;
        if (h->w_active) h->mp -= ARENA_MP_COST_W;
        break;
    default:
        /* No-op for any hero without a real W in this arena, not a crash
           or a silent wrong kit: Duck's W (Government Clearance) needs
           objective structures that don't exist here. Tree's W
           (Untranslated) and Pizza's W (I Am The Chosen One) both fall here
           too -- unbuildable/pure-visual, flagged in the header comments. */
        break;
    }
}

void arena_cast_r(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    ArenaHero *h = &arena_state.heroes[owner];
    ArenaHero *foe = arena_nearest_enemy(owner);
    if (!h->alive || h->silenced_ms > 0 || h->r_cooldown_ms > 0 || h->mp < ARENA_MP_COST_R) return;
    h->cast_flash_slot = 3;

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        h->r_active_ms = ARENA_UNICORN_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_UNICORN_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_DUCK:
        if (duck_pull_foe(h, foe, ARENA_DUCK_R_PULL_DIST, ARENA_DUCK_R_DAMAGE, ARENA_DUCK_R_RANGE)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_DUCK_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_GHOST:
        /* Recital: the ally-heal side (docs/HEROES_VS0.md: "same zone,
           opposite effect depending on team") is wired for real now that
           arena_nearest_ally exists (S170-45) -- see tick_hero_kit's zone
           tick below for the actual heal application, since it needs the
           `ally` parameter that loop already threads through. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_GHOST_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_GHOST_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_FROG:
        /* The Secret, simplified: reuses the intangible_ms mechanic at a
           longer duration. "Reappear at any visited location" needs its
           own location-memory system -- not implemented, so this reappears
           in place, flagged as a simplification rather than the full
           ability. */
        h->intangible_ms = ARENA_FROG_R_VANISH_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FROG_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_DOC_WHEEL:
        /* No Combat Power, As Advertised: teamwide cleanse + heal within
           radius, simplified from a literal shield (see header comment).
           Unlike Q's single-target heal, this always "lands" and consumes
           the cooldown even with zero allies in range -- a real ultimate
           commitment, not a whiff-refunded poke. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *ally = &arena_state.heroes[i];
            if (i == owner || !ally->active || !ally->alive) continue;
            if (ally->team != h->team) continue;
            float dx = ally->x - h->x, dz = ally->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_DOC_WHEEL_R_RADIUS) {
                doc_wheel_heal_and_cleanse(ally, ARENA_DOC_WHEEL_R_HEAL);
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_DOC_WHEEL_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_TREE:
        /* Grand Secret, simplified from "roots until recast, min 8s" to a
           fixed-duration self-root + armor buff + heal -- same
           fixed-duration simplification already used for Frog's R and
           Ghost's R zone. rooted_ms doubles as "immune to displacement"
           (see duck_pull_foe). */
        h->rooted_ms = ARENA_TREE_R_ROOT_MS;
        h->r_active_ms = ARENA_TREE_R_ROOT_MS;
        h->hp += ARENA_TREE_R_HEAL;
        if (h->hp > h->max_hp) h->hp = h->max_hp;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_TREE_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_PIZZA:
        /* Nobody Ever Checks: HP cannot drop below 1 for the duration -- a
           real damage floor via apply_damage's survive_floor_ms check, not
           a simplified-away shield (contrast Doc Wheel's R, deferred for
           exactly that reason). */
        h->survive_floor_ms = ARENA_PIZZA_R_FLOOR_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_PIZZA_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_FLAMEL:
        /* Elixir of Wild Growth (Elixir of Life + Wild Growth merged): a
           fixed zone (reusing Ghost's r_zone_x/z/tick_ms fields) that roots
           enemies and heals allies each tick for its duration -- see
           tick_hero_kit's zone tick below -- plus a one-time mass-mark of
           nodes in radius at cast time. The doc's "heavy slow" is
           simplified to a full root: no per-hero movement-speed-multiplier
           system exists in this arena yet, flagged. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_FLAMEL_R_DURATION_MS;
        for (int n = 0; n < ARENA_NODE_COUNT; n++) {
            ArenaNode *node = &arena_state.nodes[n];
            float ndx = h->x - node->x, ndz = h->z - node->z;
            if (sqrtf(ndx * ndx + ndz * ndz) <= ARENA_FLAMEL_R_RADIUS) {
                node->marked_by_team = h->team;
                node->mark_ms_remaining = ARENA_FLAMEL_MARK_MS;
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FLAMEL_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_MORRIGAN:
        /* The Crow Confirms It: a fixed zone (reusing Ghost's
           r_zone_x/z/tick_ms fields) that deals execute-scaled DPS to
           enemies inside for its duration -- see tick_hero_kit's zone tick
           below. No ally-heal side (unlike Ghost/Flamel's R) -- a war
           goddess's ultimate isn't a support tool. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_MORRIGAN_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_MORRIGAN_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_DAGDA:
        /* The force-fed porridge: a real damage floor (like Pizza's R) plus
           a real heal on top -- "eats every bite, unhurt, fights the next
           day regardless," enduring AND coming out ahead, not just
           surviving. */
        h->survive_floor_ms = ARENA_DAGDA_R_FLOOR_MS;
        h->hp += ARENA_DAGDA_R_HEAL;
        if (h->hp > h->max_hp) h->hp = h->max_hp;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_DAGDA_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_COURIER:
        if (courier_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_COURIER_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_LOKI:
        loki_cast_r(h);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_LOKI_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_GARY:
        if (gary_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_GARY_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        flute_debt_cast_r(h, foe);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_FLUTE_DEBT_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_BACON_PUCK:
        if (bacon_puck_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_BACON_PUCK_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_ABRAHAM:
        abraham_cast_r(h);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_ABRAHAM_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_ADA:
        if (ada_cast_r(h, foe)) {
            h->r_cooldown_ms = cast_cooldown(h, ARENA_ADA_R_COOLDOWN_MS);
            h->mp -= ARENA_MP_COST_R;
        }
        break;
    case ARENA_HERO_TYLER:
        tyler_cast_r(h, foe);
        h->r_cooldown_ms = cast_cooldown(h, ARENA_TYLER_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_PAIMON:
        /* Two Hundred Legions: fixed zone, same shape as Ghost's Recital/
           Flamel's Elixir of Wild Growth -- see tick_hero_kit's zone tick
           below for the actual periodic damage/heal. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_PAIMON_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_PAIMON_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_NOOR1:
        /* Do Not Approach: fixed cold zone, damage-only (no ally-heal side --
           the instruction is one-sided) -- see tick_hero_kit's zone tick below. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_NOOR1_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_NOOR1_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_CAIN:
        /* The Mark: survive-floor panic button, same shape as Pizza's/Loki's R --
           "a mark that is a curse and a protection at the same time," made literal. */
        h->survive_floor_ms = ARENA_CAIN_R_FLOOR_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_CAIN_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_GUNNR:
        /* Valhalla Has Yet To Admit It: instant hit-if-in-range, execute-scaled via
           execute_scale_damage, same shape as Morrigan's/Cain's Q -- the vindication
           finally lands hardest against a target who's already nearly beaten. */
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_GUNNR_R_RANGE) {
                apply_damage(foe, apply_armor(execute_scale_damage(foe, ARENA_GUNNR_R_DAMAGE_BASE, ARENA_GUNNR_R_DAMAGE_LOW_HP),
                                               arena_hero_armor(foe)));
            }
        }
        h->r_cooldown_ms = cast_cooldown(h, ARENA_GUNNR_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_VASSAGO:
        /* The Gentle Maybe: fixed zone, same shape as Ghost's Recital/Paimon's Two Hundred
           Legions -- see tick_hero_kit's zone tick below. No damage component at all, the
           one hero on this roster whose ultimate is pure control: not a hit, a held breath. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_VASSAGO_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_VASSAGO_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_HE_XIANGU:
        /* Never Once Framed It As Sacrifice: fixed zone, same shape as Flamel's Elixir of
           Wild Growth, heal-only -- no enemy damage component at all, the mirror of
           Vassago's purely-controlling R: she shares her sustenance, doesn't hurt anyone. */
        h->r_zone_x = h->x;
        h->r_zone_z = h->z;
        h->r_zone_tick_ms = 0;
        h->r_active_ms = ARENA_HE_XIANGU_R_DURATION_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_HE_XIANGU_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    case ARENA_HERO_BELETH:
        /* The Detonation: marks the foe's CURRENT position at cast time (not a
           continuously-tracked target) and starts a silent fuse -- reuses Ghost's own
           r_zone_x/z fields but not r_zone_tick_ms's periodic-tick idiom, since this fires
           exactly once, in tick_hero_kit, the instant r_active_ms counts down to zero.
           Whiffs (consumes no cooldown) with no foe in range -- same "real commitment, not
           a guaranteed poke" shape as every other ranged cast on this roster. */
        if (foe && hero_is_hittable(foe)) {
            float dx = foe->x - h->x, dz = foe->z - h->z;
            if (sqrtf(dx * dx + dz * dz) <= ARENA_BELETH_R_RANGE) {
                h->r_zone_x = foe->x;
                h->r_zone_z = foe->z;
                h->r_active_ms = ARENA_BELETH_R_FUSE_MS;
                h->r_cooldown_ms = cast_cooldown(h, ARENA_BELETH_R_COOLDOWN_MS);
                h->mp -= ARENA_MP_COST_R;
            }
        }
        break;
    case ARENA_HERO_MNM:
        /* Absorbing Hits Meant For Somebody Else: self-root + a guaranteed-survival window,
           same combining-two-generic-fields shape as Tree's Grand Secret (rooted_ms + a buff),
           with survive_floor_ms standing in for Tree's armor bonus -- the literal mechanical
           translation of the lore's own line that the shapeshifting is just what happens to a
           body that's absorbed hits meant for someone else. Always lands, same "real ultimate
           commitment" convention as every other unconditional self-buff R on this roster. */
        h->rooted_ms = ARENA_MNM_R_ROOT_MS;
        h->survive_floor_ms = ARENA_MNM_R_SURVIVE_FLOOR_MS;
        h->r_cooldown_ms = cast_cooldown(h, ARENA_MNM_R_COOLDOWN_MS);
        h->mp -= ARENA_MP_COST_R;
        break;
    }
}

static void tick_hero_kit(ArenaHero *h, ArenaHero *foe, ArenaHero *ally, unsigned int dt_ms) {
    /* Cooldowns and status effects (silence, intangibility) are generic --
       any hero can carry them, not just whichever kit currently applies
       them (S170-32). */
    if (h->q_cooldown_ms > 0) h->q_cooldown_ms -= (int)dt_ms;
    if (h->w_cooldown_ms > 0) h->w_cooldown_ms -= (int)dt_ms;
    if (h->r_cooldown_ms > 0) h->r_cooldown_ms -= (int)dt_ms;
    /* Mana regen (S170-132): generic and roster-wide, same "runs for every hero every tick
       regardless of kit" reasoning as the cooldown decrements just above. */
    if (h->alive && h->mp < h->max_mp) {
        float regen = ARENA_MP_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
        h->mp += (int)regen;
        if (h->mp > h->max_mp) h->mp = h->max_mp;
    }
    if (h->silenced_ms > 0) {
        h->silenced_ms -= (int)dt_ms;
        if (h->silenced_ms < 0) h->silenced_ms = 0;
    }
    if (h->intangible_ms > 0) {
        h->intangible_ms -= (int)dt_ms;
        if (h->intangible_ms < 0) h->intangible_ms = 0;
    }
    /* rooted_ms/survive_floor_ms (S170-46): generic status effects, any
       kit's ability can apply them, same reasoning as silence/intangible
       above. */
    if (h->rooted_ms > 0) {
        h->rooted_ms -= (int)dt_ms;
        if (h->rooted_ms < 0) h->rooted_ms = 0;
    }
    if (h->survive_floor_ms > 0) {
        h->survive_floor_ms -= (int)dt_ms;
        if (h->survive_floor_ms < 0) h->survive_floor_ms = 0;
    }
    /* burning_ms/burn_dps (S170-46, Pizza's Q): fixed-interval DoT tick,
       same 1000ms-accumulator pattern as Ghost's R zone. burn_tick_ms
       resets when the burn ends so a later re-application starts clean. */
    if (h->burning_ms > 0) {
        if (h->alive) {
            h->burn_tick_ms += (int)dt_ms;
            while (h->burn_tick_ms >= 1000 && h->burning_ms > 0) {
                h->burn_tick_ms -= 1000;
                apply_damage(h, h->burn_dps);
            }
        }
        h->burning_ms -= (int)dt_ms;
        if (h->burning_ms <= 0) { h->burning_ms = 0; h->burn_tick_ms = 0; }
    }

    /* Loop Back's history ring buffer (S170-33) is sampled for every hero,
       not just whoever's playing Frog -- same "generic state, only one
       kit reads it today" reasoning as the status-effect fields. Samples
       while alive only: rewinding into a pre-death state is the ability's
       whole point, but there's nothing meaningful to record once a match
       has already ended for this hero. */
    if (h->alive) {
        h->loopback_since_sample_ms += (int)dt_ms;
        while (h->loopback_since_sample_ms >= ARENA_FROG_LOOPBACK_SAMPLE_MS) {
            h->loopback_since_sample_ms -= ARENA_FROG_LOOPBACK_SAMPLE_MS;
            int slot = h->loopback_next_slot;
            h->loopback_x[slot] = h->x;
            h->loopback_z[slot] = h->z;
            h->loopback_hp[slot] = h->hp;
            h->loopback_next_slot = (slot + 1) % ARENA_FROG_LOOPBACK_SLOTS;
            if (h->loopback_count < ARENA_FROG_LOOPBACK_SLOTS) h->loopback_count++;
        }
    }

    switch (h->hero_id) {
    case ARENA_HERO_UNICORN:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        if (h->w_active && h->alive) {
            float regen = ARENA_UNICORN_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_GHOST:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            /* Fixed-interval zone tick (once per 1000ms of accumulated
               time in the zone's duration), not fractional-per-tick DPS --
               correct at any real frame rate, same reasoning as the match
               event log's snapshot interval elsewhere in this codebase. */
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_GHOST_R_RADIUS) {
                        apply_damage(foe, apply_armor(ARENA_GHOST_R_DPS, arena_hero_armor(foe)));
                    }
                }
                /* Ally-heal side (S170-45): "same zone, opposite effect
                   depending on team" -- the nearest living ally standing in
                   the zone heals for the same rate the foe takes damage. */
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_GHOST_R_RADIUS) {
                        ally->hp += ARENA_GHOST_R_DPS;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_TREE:
        /* Grand Secret's fixed-duration armor/root window (see arena_cast_r) --
           rooted_ms already decrements generically above; this only owns
           r_active_ms, same pattern as Unicorn/Ghost. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        break;
    case ARENA_HERO_PIZZA:
        /* Uninvestigated Fire: an always-on burn aura, not a cast -- ticks
           independently of Q/W/R cooldowns. Pizza is immune to its own
           burn (per the doc) since this only ever damages `foe`, never h
           itself. The node-corruption half of this passive is handled
           generically in arena_tick_nodes, not here. Only checks the
           single nearest-foe parameter (same limitation as Ghost's R zone
           in team mode -- an existing, accepted precedent, not a new one). */
        if (h->alive) {
            h->aura_tick_ms += (int)dt_ms;
            while (h->aura_tick_ms >= 1000) {
                h->aura_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_PIZZA_AURA_RADIUS) {
                        apply_damage(foe, ARENA_PIZZA_AURA_DPS);
                    }
                }
            }
        }
        break;
    case ARENA_HERO_FLAMEL:
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_FLAMEL_R_RADIUS) {
                        foe->rooted_ms = ARENA_FLAMEL_R_ROOT_MS;
                    }
                }
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_FLAMEL_R_RADIUS) {
                        ally->hp += ARENA_FLAMEL_R_HEAL_PER_TICK;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_MORRIGAN:
        /* The Crow Confirms It: execute-scaled DPS zone tick, same
           fixed-interval pattern as Ghost/Flamel's R. No ally-heal side. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_MORRIGAN_R_RADIUS) {
                        apply_damage(foe, apply_armor(
                            execute_scale_damage(foe, ARENA_MORRIGAN_R_DAMAGE_BASE, ARENA_MORRIGAN_R_DAMAGE_LOW_HP),
                            arena_hero_armor(foe)));
                    }
                }
            }
        }
        break;
    case ARENA_HERO_DAGDA:
        /* The Undry: passive self HP regen, always on, no cooldown/cast
           gate at all -- "no one leaves it unsatisfied." */
        if (h->alive) {
            float regen = ARENA_DAGDA_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        /* Recouping Interest: same toggle-regen shape as Unicorn's W. */
        if (h->w_active && h->alive) {
            float regen = ARENA_FLUTE_DEBT_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_GUNNR:
        /* Three More Things: same toggle-regen shape as Flute Debt's Recouping Interest --
           being quietly right keeps paying off over time. */
        if (h->w_active && h->alive) {
            float regen = ARENA_GUNNR_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        break;
    case ARENA_HERO_VASSAGO:
        /* Passive: same always-on regen shape as Dagda's Undry -- ambient restorative
           foresight, sensing and softening harm before it fully lands. */
        if (h->alive) {
            float regen = ARENA_VASSAGO_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        /* The Gentle Maybe: fixed zone, silence-only tick, no damage -- re-applies the
           silence to any foe still standing in it, so leaving and re-entering is the
           only way out, same "you're in it or you're not" logic every other zone uses. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_VASSAGO_R_RADIUS) {
                        foe->silenced_ms = ARENA_VASSAGO_R_SILENCE_MS;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_HE_XIANGU:
        /* Passive: same always-on regen shape as Dagda's Undry -- subsisting on almost
           nothing. */
        if (h->alive) {
            float regen = ARENA_HE_XIANGU_PASSIVE_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        /* W: same toggle-regen shape as Flute Debt's Recouping Interest -- self-denial as
           discipline, a second layer of sustain on top of the passive while active. */
        if (h->w_active && h->alive) {
            float regen = ARENA_HE_XIANGU_W_REGEN_PER_SEC * ((float)dt_ms / 1000.0f);
            h->hp += (int)regen;
            if (h->hp > h->max_hp) h->hp = h->max_hp;
        }
        /* Never Once Framed It As Sacrifice: fixed zone, heal-only tick, no damage --
           re-applies each tick, so an ally has to actually stay in it, same "you're in it
           or you're not" logic every other zone uses. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_HE_XIANGU_R_RADIUS) {
                        ally->hp += ARENA_HE_XIANGU_R_HEAL_PER_TICK;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_BELETH:
        /* The Detonation: NOT a periodic zone tick like Ghost/Vassago/He Xiangu's own R
           zones above -- the fuse counts down once, and the instant it crosses from >0 to
           <=0 (this exact branch only ever runs on that one tick, since r_active_ms then
           sits at 0 and the outer guard stops re-entry until the next real cast) it deals
           ONE large burst to whoever's still standing in the marked zone. "The threat
           builds in total silence and only resolves once, all at once." */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms <= 0) {
                h->r_active_ms = 0;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_BELETH_R_RADIUS) {
                        apply_damage(foe, apply_armor(ARENA_BELETH_R_DAMAGE, arena_hero_armor(foe)));
                    }
                }
            }
        }
        break;
    case ARENA_HERO_TYLER:
        /* Divided We Stand's vulnerability window -- arena_hero_armor() reads r_active_ms
           directly for the negative-armor effect; this just counts it down. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
        }
        break;
    case ARENA_HERO_PAIMON:
        /* Keeping the Peace: always-on passive, same aura-tick idiom as
           Pizza's burn aura -- periodically silences the nearest enemy in
           range without being cast, talking a fight down before it
           escalates rather than burning it. */
        if (h->alive) {
            h->aura_tick_ms += (int)dt_ms;
            while (h->aura_tick_ms >= ARENA_PAIMON_PASSIVE_INTERVAL_MS) {
                h->aura_tick_ms -= ARENA_PAIMON_PASSIVE_INTERVAL_MS;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_PAIMON_PASSIVE_AURA_RADIUS) {
                        foe->silenced_ms = ARENA_PAIMON_PASSIVE_SILENCE_MS;
                    }
                }
            }
        }
        /* Two Hundred Legions: fixed zone, damage-to-enemy + heal-to-ally
           tick, same shape as Ghost's Recital / Flamel's Elixir of Wild
           Growth -- the literal presence of a commanded army felt by both
           sides at once. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_PAIMON_R_RADIUS) {
                        apply_damage(foe, ARENA_PAIMON_R_DPS);
                    }
                }
                if (ally && ally->alive) {
                    float adx = ally->x - h->r_zone_x, adz = ally->z - h->r_zone_z;
                    if (sqrtf(adx * adx + adz * adz) <= ARENA_PAIMON_R_RADIUS) {
                        ally->hp += ARENA_PAIMON_R_HEAL_PER_TICK;
                        if (ally->hp > ally->max_hp) ally->hp = ally->max_hp;
                    }
                }
            }
        }
        break;
    case ARENA_HERO_NOOR1:
        /* About Four Days Behind: always-on passive, same aura-tick idiom as
           Pizza's/Paimon's -- periodically silences the nearest enemy in
           range, reading their next move before they've committed to it. */
        if (h->alive) {
            h->aura_tick_ms += (int)dt_ms;
            while (h->aura_tick_ms >= ARENA_NOOR1_PASSIVE_INTERVAL_MS) {
                h->aura_tick_ms -= ARENA_NOOR1_PASSIVE_INTERVAL_MS;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->x, dz = foe->z - h->z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_NOOR1_PASSIVE_AURA_RADIUS) {
                        foe->silenced_ms = ARENA_NOOR1_PASSIVE_SILENCE_MS;
                    }
                }
            }
        }
        /* Do Not Approach: fixed cold zone, damage-only tick -- no ally-heal
           side, the instruction is one-sided. */
        if (h->r_active_ms > 0) {
            h->r_active_ms -= (int)dt_ms;
            if (h->r_active_ms < 0) h->r_active_ms = 0;
            h->r_zone_tick_ms += (int)dt_ms;
            while (h->r_zone_tick_ms >= 1000) {
                h->r_zone_tick_ms -= 1000;
                if (foe && hero_is_hittable(foe)) {
                    float dx = foe->x - h->r_zone_x, dz = foe->z - h->r_zone_z;
                    if (sqrtf(dx * dx + dz * dz) <= ARENA_NOOR1_R_RADIUS) {
                        apply_damage(foe, ARENA_NOOR1_R_DPS);
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

/* bot_cast_kit_if_ready: simple heuristic AI for whichever hero the bot is
 * playing -- cast Q (then R, once available) whenever off cooldown and the
 * foe is within that ability's range. Not a real decision-making bot brain
 * (that's Phase E's problem, GAME_AI_NORTHSTAR.md), just enough to prove
 * the bot side can actually use a kit at all (Phase D's "both sides"). */
static void bot_cast_kit_if_ready(ArenaHero *bot, ArenaHero *foe) {
    if (!bot->alive || !foe->alive) return;
    float dx = foe->x - bot->x, dz = foe->z - bot->z;
    float dist = sqrtf(dx * dx + dz * dz);

    switch (bot->hero_id) {
    case ARENA_HERO_DUCK:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_DUCK_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_DUCK_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_UNICORN:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_UNICORN_Q_HIT_RADIUS * 2.0f) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_GHOST:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_GHOST_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_GHOST_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FROG:
        /* Defensive kit, so the heuristic is defensive too: rewind when
           hurt, vanish when critical -- not "attack when in range" like
           the other three, since Frog has no damage-dealing ability. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->hp < bot->max_hp / 2 && bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_DOC_WHEEL:
        /* This heuristic is 1v1-only local-demo AI, and Doc Wheel's entire
           kit is ally-targeted -- no useful action exists with no ally
           present (S170-45). Doc Wheel is a real, working pick in team
           mode via apps/arena_bot's own simpler "cast Q periodically"
           heuristic, which the server-side dispatch already handles
           correctly regardless of hero. Intentional no-op here, not a
           missing case. */
        break;
    case ARENA_HERO_TREE:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_TREE_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_PIZZA:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_PIZZA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FLAMEL:
        /* Q is the only foe-targeted piece of this kit -- W/R are ally-AoE
           and have no useful action in the 1v1 local demo's bot heuristic,
           same reasoning as Doc Wheel above. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_FLAMEL_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_MORRIGAN:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_MORRIGAN_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner); /* Three Forms: closes distance on its own */
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_MORRIGAN_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_DAGDA:
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_DAGDA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_COURIER:
        if (bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner); /* dash-strike, closes distance on its own like Morrigan's W */
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_COURIER_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_LOKI:
        /* Q has no range gate (it's a swap, not a dash) so it's always
           usable off cooldown. W is a defensive stance -- toggle on under
           pressure, like Frog's heuristic. R is the survive-floor panic
           button, same threshold as Pizza/Dagda's. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner);
        } else if (!bot->w_active && bot->hp < bot->max_hp / 2) {
            arena_toggle_w(bot->owner);
        }
        break;
    case ARENA_HERO_GARY:
        /* Stationary marksman -- toggle W on early for the extended range,
           then just poke with Q whenever in range and off cooldown. R when
           the foe is close enough to actually want rooted. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_GARY_Q_RANGE_WATCHING) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_GARY_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_FLUTE_DEBT:
        /* Apply the debt with Q, then collect with R once it's landed --
           R deliberately checked first isn't right (R needs foe->burning_ms
           set by a prior Q), so Q leads and R follows once off cooldown. W
           is passive sustain, toggle on early like Loki's early instinct
           but without the pressure gate since it's just regen, not armor. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_FLUTE_DEBT_Q_HIT_RADIUS) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_FLUTE_DEBT_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_BACON_PUCK:
        /* Q is defensive (self intangible, Frog's escape shape) -- use it
           when hurt, not on cooldown for its own sake. R is the primary
           damage/heal source whenever in range and off cooldown. */
        if (bot->hp < bot->max_hp / 3 && bot->q_cooldown_ms <= 0) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_BACON_PUCK_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_ABRAHAM:
        /* Toggle W on early for the channeled Q damage, poke with Q
           whenever in range and off cooldown, cleanse+heal with R when hurt
           or carrying a debuff. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_ABRAHAM_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 &&
                   (bot->hp < bot->max_hp / 2 || bot->silenced_ms > 0 || bot->rooted_ms > 0 || bot->burning_ms > 0)) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_ADA:
        /* Toggle W on early for the frame's armor, root with Q at range,
           finish with R once close enough. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_ADA_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_ADA_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_TYLER:
        /* Root+DoT with Q at range, blink-strike with W to close distance,
           R (the vulnerability window) only when confident -- healthy and
           already in range, not a panic button like Loki's/Bacon+Puck's Q. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_TYLER_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_TYLER_R_RANGE && bot->hp > bot->max_hp / 2) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_PAIMON:
        /* Q for the ranged root+damage poke, W as the instant-decree
           follow-up, R (the zone) when the foe is close enough for it to
           matter -- same "Q leads, W/R follow once in range" shape as
           Ghost/Gary above. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_PAIMON_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->w_cooldown_ms <= 0 && dist <= ARENA_PAIMON_W_RANGE) {
            arena_toggle_w(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_PAIMON_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_NOOR1:
        /* Q for the ranged root+damage poke when in range, R (the zone)
           when the foe is close enough for it to matter -- same shape as
           Paimon above. W is a defensive self-intangibility, not a foe-
           ranged ability, so it's gated on low HP instead, same panic-
           button pattern as Loki's/Bacon+Puck's Q. */
        if (bot->hp < bot->max_hp / 4 && bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_NOOR1_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_NOOR1_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_CAIN:
        /* R is the survive-floor panic button, same threshold as every
           other hero that carries one. Q whenever in range and off
           cooldown. W (dash away) is defensive, not offensive, so it's
           gated on low HP too rather than proximity to a foe. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (bot->hp < bot->max_hp / 3 && bot->w_cooldown_ms <= 0) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_CAIN_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_GUNNR:
        /* W is free sustain, toggle on early like Loki's/Flute Debt's own
           instinct. Q whenever in melee range and off cooldown. R when the
           foe is close enough for the execute to matter. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_GUNNR_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_GUNNR_R_RANGE) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_VASSAGO:
        /* W is ally-targeted -- no useful action in the 1v1 local demo's bot
           heuristic (no ally present), same reasoning as Doc Wheel/Flamel's
           own W above. Q whenever in range and off cooldown, R when the foe
           is close enough for the zone to matter. */
        if (bot->q_cooldown_ms <= 0 && dist <= ARENA_VASSAGO_Q_RANGE) {
            arena_cast_q(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_VASSAGO_R_RADIUS) {
            arena_cast_r(bot->owner);
        }
        break;
    case ARENA_HERO_HE_XIANGU:
        /* R is ally-only (a heal zone) -- no useful action in the 1v1 local
           demo's bot heuristic (no ally present), same reasoning as Doc
           Wheel/Flamel/Vassago's own ally-only slots above. W is free
           self-sustain, toggle on early like Loki's/Flute Debt's own
           instinct. Q whenever in range and off cooldown -- it self-heals
           too, always worth using. */
        if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_HE_XIANGU_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_BELETH:
        /* W first when in range and off cooldown -- the silence buys the window
           everything else needs. R next: marks the zone and starts the fuse the instant
           a foe is close enough to be worth the long cooldown. Q whenever in range and
           off cooldown otherwise -- the reliable poke+burn. */
        if (bot->w_cooldown_ms <= 0 && dist <= ARENA_BELETH_W_RANGE) {
            arena_toggle_w(bot->owner);
        } else if (bot->r_cooldown_ms <= 0 && dist <= ARENA_BELETH_R_RANGE) {
            arena_cast_r(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_BELETH_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    case ARENA_HERO_MNM:
        /* R is the survive-floor panic button, same low-HP threshold as every other hero that
           carries one (Cain's own). W is free sustained-tankiness, toggle on early like
           Loki's/Ada's own instinct. Q whenever in melee range and off cooldown. */
        if (bot->hp < bot->max_hp / 4 && bot->r_cooldown_ms <= 0) {
            arena_cast_r(bot->owner);
        } else if (!bot->w_active) {
            arena_toggle_w(bot->owner);
        } else if (bot->q_cooldown_ms <= 0 && dist <= ARENA_MNM_Q_RANGE) {
            arena_cast_q(bot->owner);
        }
        break;
    }
}

void arena_update(unsigned int dt_ms) {
    if (arena_state.winner != 0) return;
    float dt_sec = (float)dt_ms / 1000.0f;

    if (arena_bot_enabled) arena_bot_tick(dt_ms);

    /* If the player's hero is close enough to the bot, treat the last
       move-target as an attack-move: keep closing until in range. */
    ArenaHero *a = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    if (a->alive && b->alive) {
        float dx = b->x - a->x, dz = b->z - a->z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (a->moving && dist <= ARENA_HALF_EXTENT * 4.0f) {
            float tdx = a->target_x - b->x, tdz = a->target_z - b->z;
            if (sqrtf(tdx * tdx + tdz * tdz) < ARENA_ATTACK_RANGE * 3.0f && dist > ARENA_ATTACK_RANGE) {
                a->target_x = b->x;
                a->target_z = b->z;
            }
        }
    }

    update_hero_motion(&arena_state.heroes[0], dt_sec);
    update_hero_motion(&arena_state.heroes[1], dt_sec);
    arena_tick_creeps(dt_ms);
    arena_hero_attack_creeps(dt_ms);
    /* Lane creep waves (S170-138) are team-mode only, unlike jungle creeps --
       "pushing toward the enemy spawn" isn't a meaningful concept in this
       1v1 practice demo (no team-wide push objective exists here at all),
       and running them here would just be an unrequested third-party
       combatant intruding on solo practice matches/tests. See
       arena_update_teams() for the real integration. */
    resolve_combat(dt_ms);
    /* No ally in the 1v1 local path (S170-45: arena_nearest_ally only
       exists for team mode) -- NULL is the correct value, same NULL-safety
       hero_is_hittable already relies on elsewhere. */
    tick_hero_kit(&arena_state.heroes[0], &arena_state.heroes[1], NULL, dt_ms);
    tick_hero_kit(&arena_state.heroes[1], &arena_state.heroes[0], NULL, dt_ms);
    /* Gated the same as arena_bot_tick (movement) above -- without this, a
       real second player (owner 1) would still get their kit cast
       autonomously by the internal bot AI (including Duck's Q, which pulls
       the foe), fighting their own real cast commands. Found live, 2026-07-24:
       hero0 (owner 0, no move command ever sent) still moved and took
       damage in a server with zero connected clients, because this call
       wasn't gated -- Duck's Q was yanking it every time it came off
       cooldown. */
    if (arena_bot_enabled) bot_cast_kit_if_ready(&arena_state.heroes[1], &arena_state.heroes[0]);
    arena_tick_projectiles(dt_ms);
    /* Runs last (S170-51 cont'd): a capture channel is interrupted by
       damage taken this same tick (real Arathi Basin's own rule), so node
       state needs to see everything above -- creeps, melee, kit ticks,
       projectile hits, and the bot's own casts -- before deciding whether
       anyone's channel survives this tick. */
    arena_tick_nodes(dt_ms);

    if (!arena_state.heroes[0].alive) arena_state.winner = 2;
    else if (!arena_state.heroes[1].alive) arena_state.winner = 1;
}

/* ---- Team mode (2026-07-24, NORTHSTAR §13 cont'd: 10v10, up to
   ARENA_TEAM_SIZE per side). Additive, not a replacement for the 1v1 local
   demo above -- arena_update()/arena_init_with_heroes() are untouched, so
   nothing about the existing solo-vs-bot practice mode changes. Every slot
   in team mode is filled by a real network client (human or a real
   apps/arena_bot process) -- there is no internal-bot-AI fallback here,
   unlike the 1v1 path's arena_bot_tick/bot_cast_kit_if_ready. ---- */

void arena_init_teams(void) {
    memset(&arena_state, 0, sizeof(arena_state));
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        int team = (i < ARENA_TEAM_SIZE) ? 0 : 1;
        int slot_in_team = (team == 0) ? i : (i - ARENA_TEAM_SIZE);
        /* Two spread-out spawn lines, one per side, mirroring the 1v1
           demo's -6/+6 split but fanned out along z so a full team doesn't
           spawn stacked on one point. S170-139: +-8 -> +-12, matching the
           map-expansion pass's 1.5x scale (arena_nodes_reset_layout, this
           team-mode spawn line, arena_find_owned_node_for_respawn's home_x,
           and S170-138's lane creep waypoints all move together). */
        h->x = (team == 0) ? -12.0f : 12.0f;
        h->z = (slot_in_team - (ARENA_TEAM_SIZE - 1) / 2.0f) * 2.0f;
        h->target_x = h->x;
        h->target_z = h->z;
        h->hp = h->max_hp = 100;
        h->mp = h->max_mp = ARENA_MP_MAX;
        h->owner = i;
        h->team = team;
        h->active = 1;
        h->alive = 1;
        h->hero_id = ARENA_HERO_UNICORN; /* placeholder until the real client's draft pick overrides it */
    }
    arena_nodes_reset_layout();
    arena_creeps_reset();
    /* S170-138: lane creep waves get a short grace period before the first
       wave, same real-MOBA precedent as ARENA_LANE_WAVE_INITIAL_DELAY_MS's
       own doc comment -- memset already zeroed this to 0 (instant spawn),
       overridden here explicitly. */
    arena_state.lane_wave_timer_ms[0] = ARENA_LANE_WAVE_INITIAL_DELAY_MS;
    arena_state.lane_wave_timer_ms[1] = ARENA_LANE_WAVE_INITIAL_DELAY_MS;
    arena_state.winner = 0;
}

/* arena_team_alive_count: how many active heroes on `team` are still
 * alive -- the team-wipe win condition needs this rather than a single
 * hardcoded pair (§ arena_update's `!heroes[0].alive` check above, which
 * only ever made sense for exactly 2 heroes). */
static int arena_team_alive_count(int team) {
    int count = 0;
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (h->active && h->alive && h->team == team) count++;
    }
    return count;
}

/* arena_team_owns_any_node (S170-121): node.owner is 1 = team 0, 2 = team 1
 * (see ArenaNode) -- this is the literal "controlling a node" gate the
 * founder asked for. */
static int arena_team_owns_any_node(int team) {
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        if (arena_state.nodes[n].owner == team + 1) return 1;
    }
    return 0;
}

/* arena_find_owned_node_for_respawn (S170-121): among nodes this team
 * currently owns, picks the one closest to that team's original spawn line
 * (x=-8 for team 0, x=+8 for team 1, matching arena_init_teams) -- a simple
 * stand-in for a real "nearest owned outpost" choice without needing a
 * dedicated fixed-base concept this map doesn't otherwise have. Returns
 * NULL if the team owns nothing (caller must already have checked
 * arena_team_owns_any_node). S170-139: home_x +-8 -> +-12, matching
 * arena_init_teams' own spawn-line bump in the same pass. */
static ArenaNode *arena_find_owned_node_for_respawn(int team) {
    float home_x = (team == 0) ? -12.0f : 12.0f;
    ArenaNode *best = NULL;
    float best_dist = 0.0f;
    for (int n = 0; n < ARENA_NODE_COUNT; n++) {
        ArenaNode *node = &arena_state.nodes[n];
        if (node->owner != team + 1) continue;
        float dist = fabsf(node->x - home_x);
        if (!best || dist < best_dist) {
            best = node;
            best_dist = dist;
        }
    }
    return best;
}

/* arena_tick_respawns (S170-121, "controlling a node enables its spawn for
 * your team"): before this, hero death was permanent for the rest of the
 * match -- arena_update_teams only ever checked team-wipe for the win
 * condition, there was no respawn system at all. Each dead hero counts its
 * own timer down independently of node control, but the respawn itself is
 * withheld until the team owns at least one node: territory is the actual
 * gate, matching the founder's framing literally, not just a speed bonus.
 * A team holding zero nodes simply stays dead and rechecks every tick,
 * same idiom as ArenaCreep's respawn_ms_remaining/creep_spawn pair. */
static void arena_tick_respawns(unsigned int dt_ms) {
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active || h->alive) continue;
        if (h->respawn_ms_remaining > 0) h->respawn_ms_remaining -= (int)dt_ms;
        if (h->respawn_ms_remaining > 0) continue;
        h->respawn_ms_remaining = 0;
        if (!arena_team_owns_any_node(h->team)) continue;
        ArenaNode *node = arena_find_owned_node_for_respawn(h->team);
        if (!node) continue;

        /* Full clear (status effects, cooldowns, ability state) except the
           fields that must survive death: which hero this slot is playing,
           and which team it's on. */
        ArenaHeroID hero_id = h->hero_id;
        int team = h->team;
        memset(h, 0, sizeof(*h));
        h->active = 1;
        h->alive = 1;
        h->hp = h->max_hp = 100;
        h->mp = h->max_mp = ARENA_MP_MAX;
        h->owner = i;
        h->team = team;
        h->hero_id = hero_id;
        h->x = h->target_x = node->x;
        h->z = h->target_z = node->z;
    }
}

void arena_update_teams(unsigned int dt_ms) {
    if (arena_state.winner != 0) return;
    float dt_sec = (float)dt_ms / 1000.0f;

    arena_tick_respawns(dt_ms);

    /* S170-141: Tyler's puppet clones mirror his own move-target every tick
       before motion runs -- click once, the whole clone army goes with him.
       A small per-clone offset (index-based, deterministic) keeps a clone
       army from stacking exactly on top of Tyler and each other. */
    for (int i = ARENA_MAX_HEROES; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *clone = &arena_state.heroes[i];
        if (!clone->active || !clone->alive) continue;
        ArenaHero *tyler = &arena_state.heroes[clone->clone_owner];
        float offset = (float)(i - ARENA_MAX_HEROES + 1) * 0.9f;
        clone->target_x = tyler->target_x + offset;
        clone->target_z = tyler->target_z + offset;
        clone->moving = tyler->moving;
    }

    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        update_hero_motion(h, dt_sec);
    }
    arena_tick_creeps(dt_ms);
    arena_hero_attack_creeps(dt_ms);
    arena_tick_lane_creeps(dt_ms);
    arena_hero_attack_lane_creeps(dt_ms);

    /* Melee combat: each active, alive hero independently attacks its own
       nearest enemy if one is in range and its cooldown is ready -- this is
       the N-hero generalization of the 1v1 resolve_combat's hardcoded pair,
       and multiple heroes on one side can converge on the same target
       (a real team-fight dynamic the 1v1 pairwise version never had to
       handle). S170-141: bound widened to ARENA_HEROES_ARRAY_SIZE so
       Tyler's puppet clones fight through this exact same generic loop --
       both as attackers and (via arena_nearest_enemy, also widened) as
       valid targets for real enemy heroes. Clones deal/take the same flat
       ARENA_ATTACK_DAMAGE as any hero's plain auto-attack; they don't cast
       Q/W/R (see the tick_hero_kit loop below, deliberately not widened). */
    for (int i = 0; i < ARENA_HEROES_ARRAY_SIZE; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        if (h->attack_cooldown_ms > 0) h->attack_cooldown_ms -= (int)dt_ms;
        if (!h->alive) continue;
        ArenaHero *foe = arena_nearest_enemy(i);
        if (!foe) continue;
        float dx = foe->x - h->x, dz = foe->z - h->z;
        if (sqrtf(dx * dx + dz * dz) > ARENA_ATTACK_RANGE) continue;
        if (h->attack_cooldown_ms > 0) continue;
        if (hero_is_hittable(foe)) apply_damage(foe, apply_armor(ARENA_ATTACK_DAMAGE, arena_hero_armor(foe)));
        h->attack_cooldown_ms = ARENA_ATTACK_COOLDOWN_MS;
    }

    /* Deliberately NOT widened to ARENA_HEROES_ARRAY_SIZE (S170-141): Tyler's
       puppet clones are melee-only auto-fighters, not independent casters --
       only the real, client-owned hero at clone_owner ever gets a genuine
       PACKET_ARENA_CAST/bot-AI cast decision, so ticking kits for the puppet
       range would just be dead weight (no cooldowns to advance, no aura to
       apply -- flagged in docs/HEROES_VS0.md's Tyler section as the one
       piece of "every clone shares TYLER's cooldowns" not built this pass). */
    for (int i = 0; i < ARENA_MAX_HEROES; i++) {
        ArenaHero *h = &arena_state.heroes[i];
        if (!h->active) continue;
        tick_hero_kit(h, arena_nearest_enemy(i), arena_nearest_ally(i), dt_ms);
    }
    arena_tick_projectiles(dt_ms);
    /* Runs last, same reasoning as arena_update()'s own call site: a
       capture channel needs to see this whole tick's damage (creeps,
       melee, kit ticks, projectile hits) before deciding whether it's
       interrupted. */
    arena_tick_nodes(dt_ms);

    /* S170-121: a team-wipe (0 alive) is no longer instantly final on its
       own -- a wiped team that still owns a node will respawn back in, so
       the match only actually ends once they're wiped AND locked out of
       respawning entirely (own nothing to come back onto). */
    int team0_alive = arena_team_alive_count(0);
    int team1_alive = arena_team_alive_count(1);
    if (team0_alive == 0 && !arena_team_owns_any_node(0)) arena_state.winner = 2;
    else if (team1_alive == 0 && !arena_team_owns_any_node(1)) arena_state.winner = 1;
}
