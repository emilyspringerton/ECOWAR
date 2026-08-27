// apps/arena_bot/src/main.c — a persistent, networked MOBA bot.
//
// NORTHSTAR §13 cont'd (2026-07-24): "22 bots in the pool" needs bots that
// are actual network clients of apps/arena_server -- not the sim's internal
// hand-authored bot brain (arena_game.c's arena_bot_tick/bot_cast_kit_if_ready,
// which only exists for local solo-vs-bot practice and is explicitly
// disabled the moment any real client connects). This is that real client:
// gets a real WOTAN identity (same register+ticket-mint flow as
// apps/client/bot_main.c, ported rather than shared -- see that file's own
// doc comment on why this codebase duplicates per-binary orchestration
// logic instead of linking .c files across build targets), queues via the
// arena matchmaker, drafts a hero, plays using the snapshot data any client
// receives (no access to the authoritative ArenaState -- this bot only ever
// sees what apps/arena's own SDL2 client would see), and loops back to the
// matchmaker after the match ends so it keeps queuing indefinitely.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "../../../packages/common/protocol.h"
#include "../../../packages/common/hmac_sha256.h"
#include "../../../packages/common/http_client.h"
#include "../../../packages/common/rl_policy_weights.h"
#include "../../../packages/common/rl_policy_weights_team.h"

#define TICKET_PAYLOAD_LEN 20
#define TICKET_MAC_LEN 16
#define TICKET_TOTAL_LEN (TICKET_PAYLOAD_LEN + TICKET_MAC_LEN)
#define ARENA_MATCHMAKER_PORT 7778 /* separate queue from the card-RTS matchmaker's 7777 */
#define ARENA_BOT_LOW_HP_FRACTION 0.25f /* S170-173: "seek out fountains when super low" -- real-MOBA "go top off" threshold */
/* ARENA_BOT_TOPPED_UP_FRACTION (2026-07-29, founder: "bots should consider healing more than
   one tick at the fountain sometimes"): the EXIT threshold for a fountain retreat, deliberately
   higher than ARENA_BOT_LOW_HP_FRACTION's own ENTRY threshold -- see the retreat decision's own
   doc comment below for the flapping bug this hysteresis gap fixes. */
#define ARENA_BOT_TOPPED_UP_FRACTION 0.9f
/* Mirrors packages/simulation/arena_game.h's ARENA_HERO_COUNT -- this file is a pure network
   client and deliberately doesn't include the sim header (no direct ArenaState access, wire
   protocol only), so the roster size has to be kept in sync by hand here. Bump this alongside
   ARENA_HERO_COUNT whenever a new hero is added. */
#define ARENA_HERO_COUNT 30 /* Drifted stale a third time (S170-230 already fixed this once,
                               26->28) -- Warrior (28) and Cart (29) landed after that bump and
                               nobody bumped this file alongside them, so (my_owner + draft_offset)
                               % ARENA_HERO_COUNT below could never land on either hero_id: no bot
                               has ever been able to play Warrior or Cart -- only a real human
                               client can (the draft screen reads arena_game.h's own real
                               ARENA_HERO_COUNT=30 directly, not this file's separate hand-synced
                               copy). Found 2026-08-02 investigating a real, live "match_start then
                               frozen, arena_server crashes with zero snapshots" bug that only ever
                               reproduced with a real human in the lobby, never with 20 bots alone
                               -- correlated, not yet proven as the crash's exact mechanism, but a
                               real, definite bug on its own regardless. */

/* ARENA_BOT_ITEM_COSTS (S170-175 Sprint 5, "bot AI shop interaction" -- explicitly deferred at
 * the time, "bots simply won't buy anything yet, flagged not faked"): same "pure network
 * client, kept in sync by hand" idiom as ARENA_HERO_COUNT and the fountain positions just
 * below -- this file deliberately doesn't link packages/simulation/arena_game.c, so it can't
 * read ARENA_ITEMS directly. Only cost is needed (not name/slot/stats): the bot doesn't reason
 * about WHICH item helps its build, it just cycles through the catalog in order
 * (shop_next_item_id below) and lets arena_shop_buy's own server-side validation (proximity,
 * affordability, auto-sell-then-replace on a filled slot) do the real work -- a genuinely
 * simple first pass, not a build-optimizing bot brain. Costs copied verbatim from
 * packages/simulation/arena_game.c's own ARENA_ITEMS array; bump alongside it if the catalog
 * ever changes. */
#define ARENA_BOT_ITEM_COUNT 24
static const int ARENA_BOT_ITEM_COSTS[ARENA_BOT_ITEM_COUNT] = {
    300, 1000, 950, 1100, 950, 900, 850, 1000, 900, 900, 500, 800,
    1200, 1100,
    400, 450, 400, 400, 400, 350, 400, 350, 400, 350
};

/* Memorable bot names (founder: "prep for an observation phase... the bots
 * should have interesting memorable names"), so the leaderboard reads as a
 * real cast of characters worth watching evolve over time rather than
 * "player-arenabot-..." IDUNA's own register endpoint defaults to when no
 * display_name is sent. Drawn from this roster's own mythological well
 * (Irish/Norse names already in the hero lineup, plus a few flavor originals
 * in the same register) rather than generic placeholders. */
static const char *ARENA_BOT_NAMES[] = {
    "Copper Crow", "Split Antler", "Rootbound", "Ash Ratatoskr", "Last Ember",
    "Hollow Bell", "Nine Roads", "Whetstone", "Loose Thread", "The Undry Cup",
    "Broken Compass", "Third Wolf", "Kettlebite", "Long Fetch", "Grey Ledger",
    "Salt Circuit", "Two-Faced Coin", "Wandering Anvil", "Iron Sparrow", "Debt Collector",
    "Overgrowth", "Quiet Riot", "Backline Ghost", "Feral Accountant", "The Sneak Cap",
};
#define ARENA_BOT_NAME_COUNT (int)(sizeof(ARENA_BOT_NAMES) / sizeof(ARENA_BOT_NAMES[0]))

/* g_bot_name: set once in main() from --index (deterministic, so
 * scripts/launch_arena_pools.sh's persistent pool gets a stable, non-
 * colliding roster across restarts) or a pid-based fallback for anyone
 * running the binary directly without the flag. Read by
 * register_wotan_identity_once(), defined earlier in this file than main(). */
static const char *g_bot_name = NULL;

static int sock = -1;
static struct sockaddr_in server_addr;
static int my_owner = -1;

// ---- WOTAN identity (ported from apps/client/bot_main.c) ----
static char iduna_host[128] = "127.0.0.1";
static int iduna_port = 8080;
static char iduna_agent_name[128] = "";
static char iduna_agent_secret[256] = "";
static int iduna_agent_configured = 0;

static void load_iduna_agent_config(void) {
    const char *base_url = getenv("IDUNA_BASE_URL");
    if (base_url && base_url[0]) {
        const char *host_start = base_url;
        if (strncmp(host_start, "http://", 7) == 0) host_start += 7;
        else if (strncmp(host_start, "https://", 8) == 0) host_start += 8;
        char host_buf[128];
        strncpy(host_buf, host_start, sizeof(host_buf) - 1);
        host_buf[sizeof(host_buf) - 1] = '\0';
        char *slash = strchr(host_buf, '/');
        if (slash) *slash = '\0';
        char *colon = strchr(host_buf, ':');
        int port = iduna_port;
        if (colon) { port = atoi(colon + 1); *colon = '\0'; }
        strncpy(iduna_host, host_buf, sizeof(iduna_host) - 1);
        iduna_host[sizeof(iduna_host) - 1] = '\0';
        if (port > 0) iduna_port = port;
    }
    const char *name = getenv("IDUNA_AGENT_NAME");
    const char *secret = getenv("IDUNA_AGENT_SECRET");
    if (name && name[0] && secret && secret[0]) {
        strncpy(iduna_agent_name, name, sizeof(iduna_agent_name) - 1);
        iduna_agent_name[sizeof(iduna_agent_name) - 1] = '\0';
        strncpy(iduna_agent_secret, secret, sizeof(iduna_agent_secret) - 1);
        iduna_agent_secret[sizeof(iduna_agent_secret) - 1] = '\0';
        iduna_agent_configured = 1;
    }
}

static int hex_decode(const char *hex, unsigned char *out, size_t out_len) {
    size_t hexlen = strlen(hex);
    if (hexlen != out_len * 2) return 0;
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return 0;
        out[i] = (unsigned char)byte;
    }
    return 1;
}

// Registered once per process lifetime (see main()), not once per match --
// found live, 2026-07-24: an earlier version called the full register+mint
// flow fresh on every single reconnect, with provider_sub keyed off
// time(NULL), so a "persistent" bot was actually registering a brand-new
// WOTAN identity every match instead of accumulating a stable win/loss
// record. player_game_stats confirmed it live: dozens of one-match player
// rows instead of one bot with a growing record. Fixed by splitting
// registration (once) from ticket-minting (every reconnect, since tickets
// are meant to be short-lived).
static char cached_player_id[64] = "";
static int has_cached_identity = 0;

// agent_login: POST /api/v1/auth/agent -> Bearer token. Called fresh each
// time (register once, but a JWT is only valid ~1hr per IDUNA's AgentAuthHandler,
// and re-logging-in is cheap) rather than cached across the whole process.
static int agent_login(char token[2048]) {
    char resp[4096];
    int status = 0;
    char login_body[512];
    snprintf(login_body, sizeof(login_body),
             "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}",
             iduna_agent_name, iduna_agent_secret);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/auth/agent", NULL,
                        login_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[arena_bot] WOTAN: agent login failed (status=%d)\n", status);
        return 0;
    }
    if (!http_extract_json_string_field(resp, "access_token", token, 2048)) {
        fprintf(stderr, "[arena_bot] WOTAN: agent login response missing access_token\n");
        return 0;
    }
    return 1;
}

// register_wotan_identity_once: registers exactly one real player_id for
// this bot process's whole lifetime, cached in cached_player_id. Safe to
// call repeatedly -- a no-op after the first success (register is itself
// an idempotent upsert on the IDUNA side too, but there's no reason to hit
// the network again once we already have an identity).
static int register_wotan_identity_once(void) {
    if (has_cached_identity) return 1;
    char token[2048];
    if (!agent_login(token)) return 0;

    char resp[4096];
    int status = 0;
    char provider_sub[64];
    snprintf(provider_sub, sizeof(provider_sub), "arenabot-%d-%u",
             (int)getpid(), (unsigned int)time(NULL));
    const char *name = g_bot_name ? g_bot_name : ARENA_BOT_NAMES[(unsigned int)getpid() % ARENA_BOT_NAME_COUNT];
    char register_body[320];
    snprintf(register_body, sizeof(register_body),
             "{\"provider\":\"redgarden_bot\",\"provider_sub\":\"%s\",\"display_name\":\"%s\"}",
             provider_sub, name);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/players/register", token,
                        register_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[arena_bot] WOTAN: player registration failed (status=%d)\n", status);
        return 0;
    }
    if (!http_extract_json_string_field(resp, "player_id", cached_player_id, sizeof(cached_player_id))) {
        fprintf(stderr, "[arena_bot] WOTAN: registration response missing player_id\n");
        return 0;
    }
    has_cached_identity = 1;
    printf("[arena_bot %d] WOTAN: real identity registered -- player_id=%s (kept for this process's lifetime)\n",
           (int)getpid(), cached_player_id);
    return 1;
}

// get_real_wotan_ticket: mints a fresh ticket for the cached identity --
// called once per (re)connect, unlike registration above.
static int get_real_wotan_ticket(unsigned char out[TICKET_TOTAL_LEN]) {
    if (!register_wotan_identity_once()) return 0;

    char token[2048];
    if (!agent_login(token)) return 0;

    char resp[4096];
    int status = 0;
    char ticket_body[128];
    snprintf(ticket_body, sizeof(ticket_body), "{\"player_id\":\"%s\"}", cached_player_id);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/ticket", token,
                        ticket_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "[arena_bot] WOTAN: ticket mint failed (status=%d)\n", status);
        return 0;
    }
    char ticket_hex[128];
    if (!http_extract_json_string_field(resp, "ticket", ticket_hex, sizeof(ticket_hex))) {
        fprintf(stderr, "[arena_bot] WOTAN: ticket response missing ticket field\n");
        return 0;
    }
    if (!hex_decode(ticket_hex, out, TICKET_TOTAL_LEN)) {
        fprintf(stderr, "[arena_bot] WOTAN: ticket field was not valid hex\n");
        return 0;
    }
    return 1;
}

static void mint_ticket_fallback(const char *secret, unsigned char out[TICKET_TOTAL_LEN]) {
    unsigned char payload[TICKET_PAYLOAD_LEN];
    for (int i = 0; i < 16; i++) payload[i] = (unsigned char)(rand() & 0xFF);
    uint32_t expires_at = (uint32_t)time(NULL) + 300;
    payload[16] = (unsigned char)(expires_at & 0xFF);
    payload[17] = (unsigned char)((expires_at >> 8) & 0xFF);
    payload[18] = (unsigned char)((expires_at >> 16) & 0xFF);
    payload[19] = (unsigned char)((expires_at >> 24) & 0xFF);
    unsigned char mac[32];
    hmac_sha256((const unsigned char *)secret, strlen(secret), payload, TICKET_PAYLOAD_LEN, mac);
    memcpy(out, payload, TICKET_PAYLOAD_LEN);
    memcpy(out + TICKET_PAYLOAD_LEN, mac, 16);
}

// ---- matchmaker + server connection ----
static void send_find_match(struct sockaddr_in *mm_addr) {
    NetHeader h = {0};
    h.type = PACKET_FIND_MATCH;
    sendto(sock, (char *)&h, sizeof(NetHeader), 0, (struct sockaddr *)mm_addr, sizeof(*mm_addr));
}

static int wait_for_match(struct sockaddr_in *mm_addr) {
    char buf[64];
    int retry_ticks = 0;
    while (1) {
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)(sizeof(NetHeader) + sizeof(MatchFoundMsg))) {
            NetHeader *h = (NetHeader *)buf;
            if (h->type == PACKET_MATCH_FOUND) {
                MatchFoundMsg *msg = (MatchFoundMsg *)(buf + sizeof(NetHeader));
                return msg->port;
            }
        }
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
        retry_ticks++;
        /* Resend every ~5s (50 ticks), not ~1s -- found live, 2026-07-24: a
           1s retry interval racing a same-box matchmaker's near-instant
           reply meant an already-matched client's own stale retry packet
           could arrive at the matchmaker just after it had already paired
           and dequeued that client, silently re-enqueuing a phantom entry
           nobody would ever come back to claim. That phantom later got
           falsely paired with a genuinely new client, spawning a server
           with one real connection and one that would never arrive --
           found by noticing spawned match-log files with a match_start and
           nothing else, ever, despite zero logged "failed to connect"
           errors on either bot. A same-box matchmaker reply arrives in
           milliseconds; 5s of silence before resending makes this
           collision exceedingly rare without materially slowing down the
           legitimate "packet actually got lost" recovery path. */
        if (retry_ticks % 50 == 0) send_find_match(mm_addr);
        if (retry_ticks > 600) return -1; /* ~60s -- give up and let the caller retry from scratch */
    }
}

static int connect_to_server(const char *ip, int port) {
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    unsigned char ticket[TICKET_TOTAL_LEN];
    int have_ticket = 0;
    if (iduna_agent_configured) have_ticket = get_real_wotan_ticket(ticket);
    if (!have_ticket) {
        const char *secret = getenv("REDGARDEN_TICKET_SECRET");
        if (!secret || !secret[0]) {
            fprintf(stderr, "[arena_bot] no WOTAN identity and no REDGARDEN_TICKET_SECRET -- cannot connect\n");
            return 0;
        }
        fprintf(stderr, "[arena_bot] WOTAN: falling back to self-minted ticket (no real identity)\n");
        mint_ticket_fallback(secret, ticket);
    }

    char buf[sizeof(NetHeader) + TICKET_TOTAL_LEN];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_CONNECT;
    memcpy(buf + sizeof(NetHeader), ticket, TICKET_TOTAL_LEN);
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    for (int tries = 0; tries < 100; tries++) {
        char rbuf[64];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)sizeof(NetHeader)) {
            NetHeader *rh = (NetHeader *)rbuf;
            if (rh->type == PACKET_WELCOME) {
                my_owner = rh->client_id;
                printf("[arena_bot %d] connected -- hero slot %d\n", (int)getpid(), my_owner);
                return 1;
            }
        }
#ifdef _WIN32
        Sleep(50);
#else
        usleep(50000);
#endif
        if (tries % 10 == 0) {
            sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        }
    }
    return 0;
}

static void send_pick(int hero_id) {
    char buf[sizeof(NetHeader) + sizeof(ArenaPickCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_PICK;
    ArenaPickCmd *cmd = (ArenaPickCmd *)(buf + sizeof(NetHeader));
    cmd->hero_id = (uint8_t)hero_id;
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static void send_move(float x, float z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaMoveCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_MOVE;
    ArenaMoveCmd *cmd = (ArenaMoveCmd *)(buf + sizeof(NetHeader));
    cmd->target_x = x;
    cmd->target_z = z;
    /* unit_owner (2026-07-30, Tyler clone-control rework): this bot only ever controls its own
       single body -- no bot AI for independently piloting Tyler's clones exists (a separate,
       much bigger scope than this pass), so always naming my_owner here matches this bot's own
       pre-existing behavior exactly. Left uninitialized, this byte would be whatever garbage sat
       on the stack, which the server's arena_owner_controls check would then reject or
       (worse, coincidentally) accept -- real bug this explicit assignment avoids. */
    cmd->unit_owner = (uint8_t)my_owner;
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static void send_cast(int slot) {
    char buf[sizeof(NetHeader) + sizeof(ArenaCastCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_CAST;
    ArenaCastCmd *cmd = (ArenaCastCmd *)(buf + sizeof(NetHeader));
    cmd->slot = (uint8_t)slot;
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

/* send_attack (S170-162/165, founder: "the bots will need to be updated so
 * they choose their auto attack targets etc in their brain"): the bot's
 * own PACKET_ARENA_ATTACK sender. Sent AFTER send_move each decision tick
 * (not before) -- arena_set_move_target clears attack_target server-side
 * on purpose (NORTHSTAR §17.1), so ordering matters: this needs to be the
 * last word each tick for the lock to actually stick. Melee bots don't
 * strictly need this (their own move-into-range + the existing proximity
 * combat loop already worked before this system existed), but sending it
 * uniformly for every hero is what makes a bot-piloted Gary's ranged
 * homing auto-attack fire at all -- his damage now comes exclusively from
 * arena_tick_attack_targets, which only ever fires at an explicit
 * attack_target lock. */
static void send_attack(int target_owner) {
    char buf[sizeof(NetHeader) + sizeof(ArenaAttackCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_ATTACK;
    ArenaAttackCmd *cmd = (ArenaAttackCmd *)(buf + sizeof(NetHeader));
    cmd->target_owner = (uint8_t)target_owner;
    cmd->commander_unit = (uint8_t)my_owner; /* 2026-07-30: same reasoning as send_move's own unit_owner */
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

/* send_shop_buy (S170-175 Sprint 5, "bot AI shop interaction" -- explicitly deferred at the
 * time, "bots simply won't buy anything yet, flagged not faked"). The bot's own
 * PACKET_ARENA_SHOP_BUY sender, same shape as send_attack above. */
static void send_shop_buy(int item_id) {
    char buf[sizeof(NetHeader) + sizeof(ArenaShopBuyCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_SHOP_BUY;
    ArenaShopBuyCmd *cmd = (ArenaShopBuyCmd *)(buf + sizeof(NetHeader));
    cmd->item_id = (uint8_t)item_id;
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

/* Squad system (S170-202, founder real-time follow-ups to S170-201's node-capture anchor fix:
 * "like the whole team doesnt need to try to cap the node" -> "add like fractal boids so we
 * naturally split more into squads"). S170-201 fixed WHETHER a contested node ever gets a real
 * capper; this fixes the separate, related problem that every idle bot independently computing
 * its own single nearest-uncapped-node naturally converges the WHOLE team onto the same one
 * node whenever the team starts out clustered together (the common case) -- every OTHER
 * uncapped node sits completely uncontested while the whole team dogpiles one spot.
 *
 * "Fractal" reading, applying the exact same Reynolds grouping/spreading instinct S170-160's own
 * flock_offset already uses, recursively, at a coarser scale:
 *   - Individuals still flock together via flock_offset's own alignment/cohesion/separation
 *     math, unchanged -- just scoped to SQUADMATES only now instead of the whole team (see
 *     flock_offset's own updated doc comment below), so tight little clusters emerge instead of
 *     one big blob.
 *   - Squads themselves "separate" from each other by claiming DIFFERENT contested nodes
 *     (hero_squad_target_node below) rather than a second force-based repulsion between squad
 *     centroids -- the same "don't all pile into the same spot" principle expressed as
 *     differentiated GOALS instead of a second physics term, since goal-seeking already
 *     dominates a bot's real movement (flocking is explicitly only ever a perturbation on top of
 *     it, flock_offset's own doc comment) -- a competing squad-repulsion FORCE on top would
 *     fight that existing goal-seeking rather than reinforce it, the same reasoning that made
 *     S170-168 anchor the capper to the node's exact point instead of leaving it purely
 *     force-driven.
 *
 * BotSnapshotView (S170-193): a bot-local combined view standing in for what ArenaSnapshotMsg
 * used to be before the MTU-driven packet split (see ArenaSnapshotHeroesMsg's own doc comment
 * in protocol.h for the full story) -- `world` holds everything the (now heroes-less)
 * ArenaSnapshotMsg still carries, `heroes` is reassembled here from whichever
 * PACKET_ARENA_SNAPSHOT_HEROES chunks have arrived. Purely a convenience for this file's own
 * decision logic below, which was written against one atomic combined struct and has no reason
 * to care that the WIRE format no longer matches that shape -- every read site below just
 * gained a `.world.` prefix on the handful of non-hero fields it touches (`count`/`phase`/
 * `winner`/`nodes`), heroes[] itself is untouched. Not defined in protocol.h: this is a local
 * reassembly convenience, not part of the wire format itself.
 */
typedef struct {
    ArenaSnapshotMsg world;
    ArenaHeroSnapshot heroes[ARENA_SNAPSHOT_MAX_HEROES];
} BotSnapshotView;

/* Squad membership is a simple, stable, no-coordination-needed partition of the team's OWNER
 * SLOTS (my_owner % squad_count) -- every bot computes the same partition independently from
 * data already in the shared snapshot, same idiom the S170-90 approach-angle spread and the
 * S170-201 anchor fix both already rely on. squad_count tracks how many nodes are actually
 * worth splitting toward right now (uncapped), capped by how many living teammates there are
 * (no point in more squads than bots), so the structure adapts as the map state changes instead
 * of using a fixed number regardless of what's actually happening. */
static int hero_squad_count(const BotSnapshotView *cur, int hero_team) {
    int want_owner = hero_team + 1;
    int uncapped_count = 0;
    for (int n = 0; n < ARENA_SNAPSHOT_NODE_COUNT; n++) {
        if (cur->world.nodes[n].owner != want_owner) uncapped_count++;
    }
    int living = 0;
    for (int i = 0; i < cur->world.count; i++) {
        if (!cur->heroes[i].alive) continue;
        int team_i = (i < cur->world.count / 2) ? 0 : 1;
        if (team_i == hero_team) living++;
    }
    int count = uncapped_count < 1 ? 1 : uncapped_count;
    if (count > living) count = living > 0 ? living : 1;
    return count;
}

/* squad_centroid: average live position of hero_team members in squad squad_id (my_owner %
 * squad_count == squad_id). Every bot needs to be able to compute ANY squad's centroid, not
 * just its own, for the greedy squad-to-node assignment below, which has to reason about every
 * squad's position, not just self's. */
static void squad_centroid(const BotSnapshotView *cur, int hero_team, int squad_count, int squad_id,
                            float *cx, float *cz) {
    float sx = 0, sz = 0;
    int n = 0;
    for (int i = 0; i < cur->world.count; i++) {
        if (!cur->heroes[i].alive) continue;
        int team_i = (i < cur->world.count / 2) ? 0 : 1;
        if (team_i != hero_team) continue;
        if ((i % squad_count) != squad_id) continue;
        sx += cur->heroes[i].x;
        sz += cur->heroes[i].z;
        n++;
    }
    *cx = n > 0 ? sx / (float)n : 0.0f;
    *cz = n > 0 ? sz / (float)n : 0.0f;
}

/* ARENA_BOT_DAMAGED_TOWER_PATIENCE_BONUS (2026-08-10, founder real-time: "currently they go
 * right in on the towers... its better to wait for your opponent to clear a tower and then take
 * his base... your team has an advantage because he has to fight your team AND the tower so
 * your team gets the flow advantage"): a real distance bonus (in map units) applied to a node
 * whose tower has already taken damage (hp < max_hp, still alive), making it look this much
 * "closer" in the greedy pick below relative to a full-health, untouched one. This is a
 * PREFERENCE, not a hard block -- if every uncapped node's tower is still full-health, the
 * greedy pick still resolves to the plain nearest one, same as before this change, so there's
 * no deadlock risk (a squad never refuses to cap anything and just stands still forever). Tower
 * damage is a real, wire-visible signal (ArenaTowerSnapshot.hp, protocol.h) that SOMEONE has
 * already spent time fighting that tower -- doesn't distinguish which team caused it (the wire
 * protocol carries no "last attacker" field for towers), but that's fine for this heuristic's
 * own purpose: whoever damaged it already paid a real tempo cost either way, so moving in now
 * to finish the job and contest the node is a real value proposition regardless of who started
 * it. Chosen as roughly a third of this 5-node map's own typical node-to-node spacing (~35-55
 * units between adjacent nodes at the current golden-ratio-scaled layout) -- large enough to
 * genuinely flip a close call toward the damaged node, small enough that a squad still won't
 * cross the entire map chasing one when a much closer full-health node is sitting right there.
 * A heuristic first pass, not the full "wholistic bot-training / commander-squad" answer the
 * founder also asked to fold this into (NORTHSTAR §26) -- see EMILY/BACKLOG.md's own entry for
 * that broader thread. */
#define ARENA_BOT_DAMAGED_TOWER_PATIENCE_BONUS 15.0f

/* commander_posture_multiplier (NORTHSTAR §26.3, "fractal commander/soldier hierarchy" -- first
 * real step, 2026-08-10 founder: "but the wholistic bot training fractal ai commander squad
 * stuff... all the r and d should help" -> fold tower-siege patience into this thread rather
 * than leave it a standalone heuristic). §26.3 specs a full hierarchical-RL commander whose
 * action space is directives to sub-agents -- genuinely bigger scope than this pass attempts
 * (needs a restructured, learned training loop; §26.4 already flags hierarchy depth/branching as
 * unresolved, no founder decision yet). This is a real, honest, SMALLER first step in the same
 * structural direction: a team-wide "Commander" signal (not per-bot local information) that
 * actually changes individual squad decisions -- rule-based, not learned, but a genuine command
 * hierarchy exists and does something, not just a spec. Reads the real resource race (S170-153)
 * already on the wire: a team meaningfully AHEAD plays patient (protects the lead, scales
 * ARENA_BOT_DAMAGED_TOWER_PATIENCE_BONUS up), a team meaningfully BEHIND can't afford to wait
 * passively while falling further behind and needs to force fights instead (scales it down) --
 * real MOBA precedent (protect-the-lead vs. must-force-plays team strategy), not invented. The
 * threshold/multipliers below are a real, documented first pass, not tuned against actual match
 * data -- same "spec the model, commit to real numbers, don't leave them symbolic" discipline
 * arena_game.h's own timers already use throughout. */
#define ARENA_COMMANDER_RESOURCE_LEAD_THRESHOLD 300  /* ~15% of ARENA_RESOURCE_CAP (2000, packages/simulation/arena_game.h) -- a real, not noise-level, lead */
#define ARENA_COMMANDER_PATIENT_MULT 1.5f   /* team meaningfully ahead: extra patient, protect the lead */
#define ARENA_COMMANDER_AGGRESSIVE_MULT 0.3f /* team meaningfully behind: mostly abandon patience, force fights instead of waiting */

static float commander_posture_multiplier(const BotSnapshotView *cur, int hero_team) {
    int delta = (int)cur->world.resources[hero_team] - (int)cur->world.resources[1 - hero_team];
    if (delta >= ARENA_COMMANDER_RESOURCE_LEAD_THRESHOLD) return ARENA_COMMANDER_PATIENT_MULT;
    if (delta <= -ARENA_COMMANDER_RESOURCE_LEAD_THRESHOLD) return ARENA_COMMANDER_AGGRESSIVE_MULT;
    return 1.0f;
}

/* hero_squad_target_node: which uncapped node THIS bot's own squad has claimed, via a
 * deterministic greedy pass every bot computes identically (no communication needed -- same
 * inputs, same algorithm, same answer everywhere). Squads claim in ascending squad-id order,
 * each claiming whichever still-unclaimed uncapped node is nearest to that squad's own
 * centroid -- adjusted by ARENA_BOT_DAMAGED_TOWER_PATIENCE_BONUS's own "prefer a node someone's
 * already been fighting for" preference (scaled by the Commander's own posture read on the
 * real resource race, see commander_posture_multiplier's own doc comment), see that constant's
 * own doc comment. squad_count is sized (hero_squad_count above) so squad_count <= the number of
 * uncapped nodes always holds, which guarantees every squad finds a distinct, never-before-
 * claimed node -- normally a clean 1-squad-per-node split. */
static int hero_squad_target_node(const BotSnapshotView *cur, int hero_team, int squad_count, int my_squad) {
    int want_owner = hero_team + 1;
    int uncapped[ARENA_SNAPSHOT_NODE_COUNT];
    int uncapped_count = 0;
    for (int n = 0; n < ARENA_SNAPSHOT_NODE_COUNT; n++) {
        if (cur->world.nodes[n].owner != want_owner) uncapped[uncapped_count++] = n;
    }
    if (uncapped_count == 0) return -1;

    float posture = commander_posture_multiplier(cur, hero_team);
    int claimed[ARENA_SNAPSHOT_NODE_COUNT] = {0};
    int my_pick = -1;
    for (int s = 0; s < squad_count; s++) {
        float sx, sz;
        squad_centroid(cur, hero_team, squad_count, s, &sx, &sz);
        int pick = -1;
        float pick_d = 0;
        for (int u = 0; u < uncapped_count; u++) {
            int n = uncapped[u];
            if (claimed[n]) continue;
            float dx = cur->world.nodes[n].x - sx, dz = cur->world.nodes[n].z - sz;
            float d = sqrtf(dx * dx + dz * dz);
            const ArenaTowerSnapshot *tower = &cur->world.towers[n];
            if (tower->alive && tower->hp < tower->max_hp) d -= ARENA_BOT_DAMAGED_TOWER_PATIENCE_BONUS * posture;
            if (pick == -1 || d < pick_d) { pick = n; pick_d = d; }
        }
        if (pick != -1) claimed[pick] = 1;
        if (s == my_squad) my_pick = pick;
    }
    return my_pick;
}

/* flock_offset (S170-160), founder: "add boyds to the ai brain[,] check GFD
 * apps2 crystal for a reference if you need it." GoblinFoxDragon's
 * apps2/crystal/main.go has a real, working Reynolds boids implementation
 * (Boid struct, boidForces() blending alignment/cohesion/separation) --
 * used here as the structural reference, ported to this file's own
 * plain-float style (no Vec2 type here) and to hero positions instead of
 * that sim's free-roaming particles.
 *
 * Before this, every bot picked its own move target completely
 * independently -- chase the nearest enemy (with a fixed per-owner
 * approach-angle spread, S170-90) or walk to the nearest un-owned node
 * (S170-155). Correct, but robotic: nothing about how one bot moves is
 * influenced by where its own teammates currently are or are heading.
 * This computes a small steering offset from nearby, living SQUADMATES
 * only (never enemies -- flocking is a squad-cohesion behavior, not a
 * targeting one) within FLOCK_RADIUS:
 *   - alignment: nudge toward the average heading nearby allies just
 *     moved in, derived from the previous tick's snapshot vs this one --
 *     the wire snapshot carries position only, never velocity (same "this
 *     bot only ever sees what any client sees" constraint every other
 *     decision in this file already lives under), so velocity has to be
 *     inferred locally rather than read off the wire.
 *   - cohesion: nudge toward the average position of those same allies.
 *   - separation: push away from any ally close enough to actually be
 *     crowding (a much tighter radius than alignment/cohesion use).
 * Returned as a small (dx, dz) offset meant to be ADDED to whatever
 * objective target the caller already computed (node or enemy-engage
 * point) below -- a perturbation on top of real goal-seeking, not a
 * replacement for it. Weights are deliberately separation-heavy: this
 * codebase already once shipped and had to fix "all of the bots just
 * bunch up on each other" (S170-90); flocking should reinforce that fix,
 * not quietly reintroduce the same clumping through a different code
 * path.
 *
 * S170-202: SQUAD-scoped, not whole-team-scoped, as of this pass -- the "fractal" half of the
 * squad system (see its own doc comment above): individuals flock tightly within a squad via
 * this exact same unchanged math, while squads themselves spread apart by claiming different
 * nodes (hero_squad_target_node), not by a second force here. Whole-team flocking would have
 * pulled every squad back toward each other the instant they got within FLOCK_RADIUS of another
 * squad, fighting the very spread the squad-target-node split is trying to create. */
static void flock_offset(const BotSnapshotView *cur, const BotSnapshotView *prev, int have_prev,
                          int self_owner, int my_team, int squad_count, int my_squad,
                          float *out_dx, float *out_dz) {
    const float FLOCK_RADIUS = 6.0f;
    const float SEPARATION_RADIUS = 2.0f;
    float mx = cur->heroes[self_owner].x, mz = cur->heroes[self_owner].z;
    float align_x = 0.0f, align_z = 0.0f;
    float coh_x = 0.0f, coh_z = 0.0f;
    float sep_x = 0.0f, sep_z = 0.0f;
    int count = 0;

    for (int i = 0; i < cur->world.count; i++) {
        if (i == self_owner || !cur->heroes[i].alive) continue;
        int team = (i < cur->world.count / 2) ? 0 : 1;
        if (team != my_team) continue; /* teammates only -- see doc comment above */
        if ((i % squad_count) != my_squad) continue; /* S170-202: squadmates only, not the whole team */

        float dx = cur->heroes[i].x - mx, dz = cur->heroes[i].z - mz;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist <= 0.0f || dist > FLOCK_RADIUS) continue;

        if (have_prev && prev->heroes[i].alive) {
            align_x += cur->heroes[i].x - prev->heroes[i].x;
            align_z += cur->heroes[i].z - prev->heroes[i].z;
        }
        coh_x += cur->heroes[i].x;
        coh_z += cur->heroes[i].z;
        if (dist < SEPARATION_RADIUS) {
            sep_x -= dx / dist;
            sep_z -= dz / dist;
        }
        count++;
    }

    if (count == 0) {
        *out_dx = 0.0f;
        *out_dz = 0.0f;
        return;
    }

    align_x /= (float)count;
    align_z /= (float)count;
    coh_x = coh_x / (float)count - mx;
    coh_z = coh_z / (float)count - mz;

    *out_dx = align_x * 0.3f + coh_x * 0.15f + sep_x * 1.2f;
    *out_dz = align_z * 0.3f + coh_z * 0.15f + sep_z * 1.2f;
}

/* rl_engage_nudge (2026-07-29, founder: "get all the 19 bots on it" -- the trained RL policy
 * packages/common/rl_policy_weights.h's own rl_policy_forward(), same one arena_game.c's solo
 * local-practice bot already uses, S170-228/promoted 5M-step run same day). Feeds this bot (self)
 * and its current nearest-enemy target (foe) through the trained network using the exact same
 * 18-float observation layout arena_game.c's arena_bot_tick_rl_move() builds (self hp/max_hp/mp/
 * x/z/cooldowns/flow/xp/alive, foe hp/max_hp/x/z/alive, dx/dz -- mirrored the same "always look
 * like the -6-side training agent" way, see that function's own doc comment for the full
 * reasoning), then returns a small bounded step in the suggested direction -- NOT the network's
 * raw output used as a literal world-space target.
 *
 * Two real reasons it's a nudge, not a full replacement of the existing angle-spread approach
 * point below: (1) the network was trained one-on-one, in a small fixed-spawn arena with no
 * nodes/squads/teammates -- it has zero notion of any of that, so it can only ever meaningfully
 * inform the immediate "which way to step towards this one foe" question, not macro positioning;
 * (2) a real, measured coordinate-frame mismatch -- the policy's own action output is clipped to
 * +-RL_POLICY_MOVE_TARGET_RANGE (20.0, scripts/rl_env.py's own MOVE_TARGET_RANGE, tuned for that
 * small training arena) as an ABSOLUTE world-space target, but the live map's real
 * ARENA_HALF_EXTENT is ~51.78 (S170-191's golden-ratio expansion, landed well after this training
 * setup was fixed) -- more than 2x the policy's own reachable range, so during any skirmish that
 * happens away from map center (i.e. most of them, on a 5-node Arathi-sized map) its raw output
 * would be a literal nonsense target, plausibly pulling a bot back toward the origin instead of
 * toward the real nearby enemy. Reinterpreting the output as a DIRECTION (normalized, then
 * stepped a small fixed distance) sidesteps that mismatch by construction -- safe regardless of
 * where on the map the fight is happening -- at the cost of not being a literal port of what the
 * network actually learned. Also deliberately additive on top of the existing S170-90 anti-stack
 * angle spread (not a replacement for it): several bots independently computing this same nudge
 * toward the same foe would otherwise reintroduce the exact "bots pile onto the same point"
 * bug that spread was written to fix. Not independently playtested for feel (no display in this
 * environment) -- automated tests (scripts/test_arena.sh, scripts/test_10_bots.sh) only confirm
 * it compiles, produces bounded output, and doesn't crash a live match; a real read on whether
 * it actually plays better needs the founder's own eyes on it.
 *
 * Confidence-weighted (2026-07-29, founder: "how do we combine heuristics with the ml model so
 * we do a little fuzzy best of both worlds"): the nudge is scaled by rl_engage_confidence()
 * below before being returned -- a clean 1v1 duel gets the full nudge, a chaotic teamfight the
 * model never trained on gets a heavily damped one, rather than either fully trusting or fully
 * ignoring the model based on a hard rule. */
/* rl_engage_confidence (2026-07-29, founder: "how do we combine heuristics with the ml model so
 * we do a little fuzzy best of both worlds"): how much the RL policy's suggestion below should
 * actually be trusted this tick, in (0, 1]. The policy trained strictly 1v1 -- itself and
 * exactly one foe, nobody else anywhere on the map -- so its judgment is only really grounded
 * when the real fight actually looks like that. Counts living combatants (either team, excluding
 * self and the current target) within CONFIDENCE_RADIUS of the self/foe midpoint -- each one
 * nearby halves the confidence. A simple geometric decay, not a fitted curve: there's no logged
 * confidence-vs-outcome data yet to fit a better one against (see this session's own hero win-
 * rate tracking work for the kind of real data that COULD inform this later), so this is an
 * honest first pass, not a tuned final answer. A clean 1v1 (0 nearby others) stays at full
 * confidence (1.0), same as before this function existed.
 *
 * Gaussian falloff (2026-08-10, founder: "we need to introduce a gaussian filter to the
 * heuristic vs [RL-policy] based inputs" -- this is that blend weight): replaces the original
 * discrete confidence *= 0.5 per-nearby-unit halving with a continuous Gaussian function of the
 * same nearby_others count, confidence = exp(-n^2 / (2*sigma^2)). Same qualitative shape (full
 * trust at 0 nearby, decaying toward 0 as the fight gets more crowded) but smooth rather than a
 * step function -- one extra combatant drifting in/out of CONFIDENCE_RADIUS no longer causes a
 * hard 2x jump in how much the policy's nudge is trusted, which is the actual problem a step
 * function creates in a continuously-moving teamfight. GAUSSIAN_SIGMA=1.5 chosen so the curve's
 * shape stays in the same order of magnitude as the old function's own values at small n --
 * n=1: 0.80 (old: 0.5), n=2: 0.41 (old: 0.25), n=3: 0.14 (old: 0.125), n=4: 0.03 (old: 0.0625) --
 * not a fitted curve (no logged confidence-vs-outcome data exists to fit against, same honest-
 * first-pass caveat the original comment above already gives). */
static float rl_engage_confidence(const BotSnapshotView *cur, int self_owner, int foe_owner) {
    const ArenaHeroSnapshot *self_h = &cur->heroes[self_owner];
    const ArenaHeroSnapshot *foe_h = &cur->heroes[foe_owner];
    float mid_x = (self_h->x + foe_h->x) * 0.5f;
    float mid_z = (self_h->z + foe_h->z) * 0.5f;
    /* Order of magnitude between melee range (ARENA_ATTACK_RANGE, 1.6) and the full engage
       range this file's own caller already gates on (15.0) -- "close enough to this fight to
       plausibly join or interrupt it," not just anyone visible on the map. */
    const float CONFIDENCE_RADIUS = 10.0f;
    const float GAUSSIAN_SIGMA = 1.5f;
    int nearby_others = 0;
    for (int i = 0; i < cur->world.count; i++) {
        if (i == self_owner || i == foe_owner || !cur->heroes[i].alive) continue;
        float dx = cur->heroes[i].x - mid_x, dz = cur->heroes[i].z - mid_z;
        if (dx * dx + dz * dz <= CONFIDENCE_RADIUS * CONFIDENCE_RADIUS) nearby_others++;
    }
    float n = (float)nearby_others;
    return expf(-(n * n) / (2.0f * GAUSSIAN_SIGMA * GAUSSIAN_SIGMA));
}

/* arena_rl_fill_hero_onehot (2026-07-29, founder: "not just 2 heroes"): same one-hot encoding
 * apps/arena_training/src/headless.c's own sim_get_obs() writes during training and
 * packages/simulation/arena_game.c's own copy of this exact helper writes for the solo-practice
 * bot -- duplicated here rather than shared across a header, same "pure ctypes/wire-protocol
 * client, no direct C header access to the sim" reasoning this file's own ARENA_HERO_COUNT
 * duplicate already documents for itself. Guarded by RL_POLICY_OBS_SIZE (see arena_game.c's own
 * copy of this exact guard for the full "why" -- an older, narrower-input model genuinely
 * doesn't have these input slots, and writing past the end of an 18-float obs[] stack array
 * would be real out-of-bounds corruption) -- safe to land this source change before a matching
 * wider-input model is trained and promoted. */
#if RL_POLICY_OBS_SIZE >= (18 + 2 * ARENA_HERO_COUNT)
static void arena_rl_fill_hero_onehot(float *obs, int self_hero, int foe_hero) {
    for (int i = 0; i < ARENA_HERO_COUNT; i++) {
        obs[18 + i] = 0.0f;
        obs[18 + ARENA_HERO_COUNT + i] = 0.0f;
    }
    if (self_hero >= 0 && self_hero < ARENA_HERO_COUNT) obs[18 + self_hero] = 1.0f;
    if (foe_hero >= 0 && foe_hero < ARENA_HERO_COUNT) obs[18 + ARENA_HERO_COUNT + foe_hero] = 1.0f;
}
#else
static void arena_rl_fill_hero_onehot(float *obs, int self_hero, int foe_hero) {
    (void)obs; (void)self_hero; (void)foe_hero;
}
#endif

static void rl_engage_nudge(const BotSnapshotView *cur, int self_owner, int foe_owner,
                             float *out_dx, float *out_dz) {
    const ArenaHeroSnapshot *self_h = &cur->heroes[self_owner];
    const ArenaHeroSnapshot *foe_h = &cur->heroes[foe_owner];

    float obs[RL_POLICY_OBS_SIZE];
    obs[0]  = (float)self_h->hp;
    obs[1]  = (float)self_h->max_hp;
    obs[2]  = (float)self_h->mp;
    obs[3]  = -self_h->x;
    obs[4]  = self_h->z;
    obs[5]  = (float)self_h->q_cooldown_ms;
    obs[6]  = (float)self_h->w_cooldown_ms;
    obs[7]  = (float)self_h->r_cooldown_ms;
    obs[8]  = (float)self_h->flow;
    obs[9]  = (float)self_h->xp;
    obs[10] = self_h->alive ? 1.0f : 0.0f;
    obs[11] = (float)foe_h->hp;
    obs[12] = (float)foe_h->max_hp;
    obs[13] = -foe_h->x;
    obs[14] = foe_h->z;
    obs[15] = foe_h->alive ? 1.0f : 0.0f;
    obs[16] = (-foe_h->x) - (-self_h->x);
    obs[17] = foe_h->z - self_h->z;
    arena_rl_fill_hero_onehot(obs, self_h->hero_id, foe_h->hero_id);

    float action[RL_POLICY_ACTION_SIZE];
    rl_policy_forward(obs, action);

    /* action[0]/action[1]: un-mirror x back to this bot's real frame, treat as a direction
       (not an absolute target -- see doc comment above), normalize, step a bounded distance. */
    float dir_x = -action[0];
    float dir_z = action[1];
    float mag = sqrtf(dir_x * dir_x + dir_z * dir_z);
    if (mag < 0.001f) {
        *out_dx = 0.0f;
        *out_dz = 0.0f;
        return;
    }
    const float RL_NUDGE_STEP = 3.0f; /* same order of magnitude as the angle-spread's own approach_radius */
    float confidence = rl_engage_confidence(cur, self_owner, foe_owner);
    *out_dx = (dir_x / mag) * RL_NUDGE_STEP * confidence;
    *out_dz = (dir_z / mag) * RL_NUDGE_STEP * confidence;
}

/* team_rl_engage_nudge (2026-08-11, founder real-time: "ensure the updated model makes it into
 * our frontier bots on the server" -- the first live consumer of a §25.2.1 team-trained checkpoint,
 * the exact gap NORTHSTAR §25.5 flagged as unresolved ("what consumes a team-shaped input
 * vector?"). Same additive-nudge shape as rl_engage_nudge above, but built from the ACTUAL
 * team-mode checkpoint (scripts/rl_train_team.py --team-size 3 --noisy-gestalt, 500K timesteps,
 * 100% eval win rate vs. the fixed heuristic -- Apple #12985), reconstructing
 * apps/arena_training/src/headless.c's own sim_get_obs_team_any() observation layout field-for-
 * field from LIVE wire data (BotSnapshotView) instead of the training-only ArenaHero sim state --
 * this bot only ever sees what any client sees, same constraint every other function in this file
 * already documents.
 *
 * CRITICAL: this specific checkpoint was trained at team_size=3 (a fixed input/output dimension
 * baked into packages/common/rl_policy_weights_team.h at export time), NOT the live 10v10 bot-pool's
 * real team_size=10 -- a real, hard shape mismatch, not a rounding difference. Gated on
 * `cur->world.count / 2 == 3` for exactly that reason: this function must never run against a
 * 10v10 match, where team-relative slot math below would read past real teammates into the OTHER
 * team's own heroes and feed the network structurally wrong input. Live testing for this
 * checkpoint therefore needs a real, separate 3v3 queue (see ops/systemd/redgarden-matchmaker-
 * bots-3v3.service) -- it does NOT activate in the existing 10v10 pool, which keeps running
 * heuristic-only bots unchanged until a team_size=10-trained checkpoint exists.
 *
 * No confidence decay the way rl_engage_confidence dampens the 1v1 model as more combatants
 * crowd in -- that function exists because the 1v1 model is OUT of its training distribution the
 * moment a fight isn't a clean duel. This model's training distribution IS a crowded team fight
 * (rl_env_team.py's own ArenaTeamVecEnv), so there's no equivalent "activate at reduced trust
 * outside training conditions" case to guard against here -- full weight whenever the team-size
 * gate passes. */
static void team_rl_engage_nudge(const BotSnapshotView *cur, int self_owner, int foe_owner,
                                  int my_team, float *out_dx, float *out_dz) {
    *out_dx = 0.0f;
    *out_dz = 0.0f;
    int team_size = cur->world.count / 2;
    if (team_size != 3) return; /* see this function's own doc comment -- the hard shape gate */

    int self_base = my_team == 0 ? 0 : team_size;
    int foe_base = my_team == 0 ? team_size : 0;
    int self_slot = self_owner - self_base;
    if (self_slot < 0 || self_slot >= team_size) return; /* defensive -- shouldn't happen if my_team is derived correctly by the caller */

    const ArenaHeroSnapshot *self_h = &cur->heroes[self_owner];
    const ArenaHeroSnapshot *foe_h = &cur->heroes[foe_owner];

    float obs[TEAM_RL_POLICY_OBS_SIZE];
    obs[0]  = (float)self_h->hp;
    obs[1]  = (float)self_h->max_hp;
    obs[2]  = (float)self_h->mp;
    obs[3]  = self_h->x; /* no mirroring -- sim_get_obs_team_any's own layout doesn't mirror x either, unlike the 1v1 obs above */
    obs[4]  = self_h->z;
    obs[5]  = (float)self_h->q_cooldown_ms;
    obs[6]  = (float)self_h->w_cooldown_ms;
    obs[7]  = (float)self_h->r_cooldown_ms;
    obs[8]  = (float)self_h->flow;
    obs[9]  = (float)self_h->xp;
    obs[10] = self_h->alive ? 1.0f : 0.0f;
    obs[11] = (float)foe_h->hp;
    obs[12] = (float)foe_h->max_hp;
    obs[13] = foe_h->x;
    obs[14] = foe_h->z;
    obs[15] = foe_h->alive ? 1.0f : 0.0f;
    obs[16] = foe_h->x - self_h->x;
    obs[17] = foe_h->z - self_h->z;
    arena_rl_fill_hero_onehot(obs, self_h->hero_id, foe_h->hero_id); /* same offsets [18, 18+2*ARENA_HERO_COUNT) sim_get_obs_team_any's own layout uses -- this helper is shared, not duplicated */

    int base = 18 + 2 * ARENA_HERO_COUNT;
    int slot = 0;
    for (int i = 0; i < team_size; i++) {
        if (i == self_slot) continue;
        const ArenaHeroSnapshot *mate = &cur->heroes[self_base + i];
        float *o = &obs[base + slot * 4];
        o[0] = mate->max_hp > 0 ? (float)mate->hp / (float)mate->max_hp : 0.0f;
        o[1] = mate->x - self_h->x;
        o[2] = mate->z - self_h->z;
        o[3] = mate->alive ? 1.0f : 0.0f;
        slot++;
    }

    int identity_base = base + (team_size - 1) * 4;
    for (int i = 0; i < team_size; i++) {
        obs[identity_base + i] = (i == self_slot) ? 1.0f : 0.0f;
    }

    float action[TEAM_RL_POLICY_ACTION_SIZE];
    team_rl_policy_forward(obs, action);

    /* Unlike rl_engage_nudge's own 1v1 model, this model's action range (TEAM_RL_POLICY_MOVE_
       TARGET_RANGE) was trained against rl_env_team.py's own ARENA_HALF_EXTENT-scaled action
       space, matching the live map's real extent -- no arena-size mismatch to route around the
       way rl_engage_nudge's own doc comment describes. Still applied as a bounded direction
       nudge, not a literal target, for the same "additive on top of, not instead of, the
       existing angle-spread/flocking" reasoning that keeps S170-90's anti-stack guarantee intact
       even with several bots independently consulting the same network. */
    const float TEAM_RL_NUDGE_STEP = 3.0f; /* same order of magnitude as rl_engage_nudge's own RL_NUDGE_STEP */
    float dir_x = action[0];
    float dir_z = action[1];
    float mag = sqrtf(dir_x * dir_x + dir_z * dir_z);
    if (mag < 0.001f) return;
    *out_dx = (dir_x / mag) * TEAM_RL_NUDGE_STEP;
    *out_dz = (dir_z / mag) * TEAM_RL_NUDGE_STEP;
}

// play_one_match runs the draft + live-play loop for a single match against
// the already-connected server. Returns once the match ends (winner != 0)
// or the connection goes quiet for too long.
static void play_one_match(int game_port) {
    /* S170-193: cur_view accumulates whichever of the world/hero-chunk packets have arrived so
       far (see BotSnapshotView's own doc comment) -- last/prev are then swapped exactly ONCE
       per outer loop iteration, after the inner drain loop below has processed every packet
       currently queued, not once per individual packet. This is a real improvement over the
       pre-split behavior, not just a mechanical consequence of it: the old code did `prev =
       last` on every single PACKET_ARENA_SNAPSHOT arrival, so if the bot's own loop ever fell
       behind and drained more than one backlogged snapshot in a single pass, prev/last ended up
       one PACKET apart, not one genuine TICK apart, subtly corrupting flock_offset's velocity
       inference. Swapping once per drain fixes that same-shape-but-worse bug for free while
       fixing the split itself. */
    BotSnapshotView cur_view = {0};
    BotSnapshotView last = {0};
    BotSnapshotView prev = {0}; /* S170-160: previous tick's snapshot, purely so flock_offset can infer ally velocity for alignment -- the wire snapshot itself never carries velocity */
    int have_prev = 0;
    int have_snapshot = 0;
    int picked = 0;
    int ticks_since_pick_send = 0; /* retry, see below -- S170-99 */
    uint32_t last_cast_ms = 0;
    int silent_ticks = 0;
    int shop_next_item_id = 0; /* S170-175 Sprint 5: cycles through ARENA_BOT_ITEM_COSTS in catalog order */
    uint32_t last_shop_buy_ms = 0;
    /* retreating_to_fountain (2026-07-29, founder: "bots should consider healing more than one
       tick at the fountain sometimes" -- see the actual retreat-decision block below for the
       full bug this fixes: single-threshold flapping, no hysteresis). Persists across ticks
       within this one match, same lifetime as picked/silent_ticks above -- the retreat decision
       needs to remember "I'm already mid-heal" from one tick to the next, not just react to
       this tick's HP fraction in isolation. */
    int retreating_to_fountain = 0;
    /* camping_fountain (2026-08-10, founder: "bots need to learn and evolve from fountain
       camping meta" -- heuristic path, see this block's own doc comment below at the actual
       trigger for the real reasoning). Persists across ticks the same way retreating_to_fountain
       does -- camping is a multi-tick commitment, not a per-tick reaction. */
    int camping_fountain = 0;
    uint32_t camping_start_ms = 0;
    /* draft_offset (S170-105, real bug found live, twice): ARENA_HERO_COUNT (21) now exceeds
       ARENA_MAX_HEROES (20) for the first time -- a full 20-player lobby only ever has owner
       slots 0..19, so a bare `owner % hero_count` always maps to hero_ids 0..19 and can NEVER
       reach the last hero in the roster no matter how many matches run; not a rare miss, a
       permanent, deterministic exclusion. First fix attempt used a per-bot random offset, which
       is wrong in a different way: every bot in the same match rolls its OWN independent random
       value, so two different owners can land on the SAME hero_id purely by coincidence --
       trading a permanent exclusion for a probabilistic duplicate-pick risk that didn't exist
       before. Derived from game_port instead: every client connecting to the same match already
       knows the same port (it's how they found this server in the first place), so this is a
       real shared value with zero coordination needed, deterministic per match, and still varies
       match to match as the matchmaker increments the port for each new game. */
    int draft_offset = game_port % ARENA_HERO_COUNT;

    while (1) {
        /* CRITICAL BUG FOUND LIVE (S170-192): this was a fixed char rbuf[2048] -- harmless
           when first written, but every field this session added to ArenaHeroSnapshot/
           ArenaSnapshotMsg (Flow/XP/equipped items, w_active, 7 status-effect fields,
           berserker/regen, powerups) grew the real wire packet past 2048 bytes without
           anyone checking this fixed buffer's own headroom. recvfrom silently truncates a UDP
           datagram larger than the buffer given to it -- every snapshot this whole time was
           truncated, so the size check below (len >= the real packet size) failed on EVERY
           snapshot, `have_snapshot` never went true, and no bot has been able to draft or play
           a real networked match since whichever commit first pushed the struct over 2048.
           Sized dynamically to the actual current packet size instead of a magic-number guess,
           so this can never silently drift out of sync again the same way. */
        /* S170-193: sized for whichever of the two snapshot packet types is larger -- see
           ARENA_SNAPSHOT_RECV_BUF_SIZE's own doc comment in protocol.h. */
        char rbuf[ARENA_SNAPSHOT_RECV_BUF_SIZE];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        int got_one = 0;
        while (len > 0) {
            if (len >= (int)sizeof(NetHeader)) {
                NetHeader *h = (NetHeader *)rbuf;
                if (h->type == PACKET_ARENA_SNAPSHOT && len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotMsg))) {
                    memcpy(&cur_view.world, rbuf + sizeof(NetHeader), sizeof(ArenaSnapshotMsg));
                    got_one = 1;
                } else if (h->type == PACKET_ARENA_SNAPSHOT_HEROES && len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotHeroesMsg))) {
                    ArenaSnapshotHeroesMsg chunk;
                    memcpy(&chunk, rbuf + sizeof(NetHeader), sizeof(chunk));
                    int base = chunk.chunk_index * ARENA_SNAPSHOT_HERO_CHUNK_SIZE;
                    for (int j = 0; j < ARENA_SNAPSHOT_HERO_CHUNK_SIZE && base + j < ARENA_SNAPSHOT_MAX_HEROES; j++) {
                        cur_view.heroes[base + j] = chunk.heroes[j];
                    }
                    got_one = 1;
                }
            }
            len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        }
        if (got_one) {
            prev = last;
            have_prev = have_snapshot;
            last = cur_view;
            have_snapshot = 1;
        }
        silent_ticks = got_one ? 0 : silent_ticks + 1;
        /* Found live 2026-07-29: this loop paces at 100ms/tick (the usleep(100000) at the
           bottom of this same loop), not the ~10ms/tick the old threshold of 1000 assumed --
           1000 * 100ms is really ~100s of hang before a bot notices its server died and
           requeues, not the ~10s this comment originally claimed. Discovered when a dead
           matchmaker-queue entry (a phantom/never-connected client counted toward a spawned
           match's lobby_size) left all 19 real bots connected-but-stuck in draft for a full
           ~100s after their match server's own 60s no-progress timeout killed it -- correct
           self-healing behavior, just far slower than intended. 100 * 100ms = 10s, matching
           what the message below actually says. */
        if (silent_ticks > 100) { /* ~10s of nothing at all -- server's gone */
            fprintf(stderr, "[arena_bot %d] no snapshots for 10s -- giving up on this match\n", (int)getpid());
            return;
        }

        if (have_snapshot) {
            if (last.world.phase == ARENA_PHASE_DRAFT && !picked) {
                /* Simple roster spread: pick based on owner slot so a full
                   lobby doesn't converge on one hero -- real draft strategy
                   is a later, separate concern. */
                int hero_id = (my_owner + draft_offset) % ARENA_HERO_COUNT; /* full roster, rotating exclusion -- see draft_offset's own comment */
                send_pick(hero_id);
                picked = 1;
                ticks_since_pick_send = 0;
                printf("[arena_bot %d] drafted hero_id=%d\n", (int)getpid(), hero_id);
            } else if (last.world.phase == ARENA_PHASE_DRAFT && picked) {
                /* Retry (S170-99, real bug found live against a real human client): a single
                   fire-and-forget send_pick() with no retry meant one dropped UDP packet left
                   the pick never actually received, stalling a full lobby until the server's
                   60s no-progress timeout killed the match. Rock-solid over this bot's own
                   localhost loopback so it never surfaced here, but it's the same latent gap --
                   resend every ~1s (10 ticks @ 100ms) while still stuck in draft. */
                if (++ticks_since_pick_send > 10) {
                    send_pick((my_owner + draft_offset) % ARENA_HERO_COUNT);
                    ticks_since_pick_send = 0;
                }
            } else if (last.world.phase == ARENA_PHASE_LIVE) {
                if (last.world.winner != 0) {
                    printf("[arena_bot %d] match ended, winner=%d\n", (int)getpid(), last.world.winner);
                    return;
                }
                if (my_owner >= 0 && my_owner < last.world.count && last.heroes[my_owner].alive) {
                    /* Nearest enemy from the snapshot -- this bot has no
                       access to the authoritative ArenaState, only what any
                       client sees, same information a human player's client
                       would have. */
                    float mx = last.heroes[my_owner].x, mz = last.heroes[my_owner].z;
                    int my_team = (my_owner < last.world.count / 2) ? 0 : 1;

                    /* S170-173, founder: "add healing fountains to bot awareness
                       brain and heuristics ... bots seek out fountains when super
                       low." Top priority, checked before anything else this tick --
                       a hero below ARENA_BOT_LOW_HP_FRACTION retreats to the nearest
                       fountain and does nothing else (no node-capping, no engaging,
                       no casting) until topped back up, same "go here to top off"
                       real-MOBA instinct the fountain's own heal rate was already
                       tuned for (arena_game.h's ARENA_FOUNTAIN_HEAL_PER_SEC doc
                       comment). Fountain positions are static/deterministic (same
                       "kept in sync by hand" convention this whole file already
                       uses for roster-size constants, see ARENA_HERO_COUNT's own
                       doc comment) -- mirrors arena_fountain_position()'s two fixed
                       points exactly, no wire sync needed since neither ever
                       moves.

                       2026-07-29 bug fix, founder: "bots should consider healing more than
                       one tick at the fountain sometimes." This used to recompute the retreat
                       decision from scratch every single tick off ONLY the current HP
                       fraction (`local var < LOW_HP_FRACTION`, no memory of the previous
                       tick) -- classic single-threshold flapping: a bot could dip to just
                       under 25% HP, take one fountain tick of healing that ticks it to just
                       OVER 25%, and immediately declare itself no longer low and dash back
                       into a fight it had barely started healing for, then dip back under 25%
                       a few ticks later and repeat, arriving at the fountain "sometimes" for
                       one real heal-tick and then leaving. Now a real two-threshold state
                       machine using the outer, per-match-persistent
                       `retreating_to_fountain`: entering retreat still needs HP under the
                       LOW threshold, but once retreating, a bot stays retreating until it's
                       actually topped back up (ARENA_BOT_TOPPED_UP_FRACTION, 90%), not just
                       barely above where it started. */
                    static const float fountains[2][2] = { { -43.78f, -43.78f }, { 43.78f, 43.78f } }; /* S170-191: ARENA_HALF_EXTENT-8 against the golden-ratio-scaled 51.78 (was -24/24 against the old 32) -- kept in sync by hand, same idiom this file's own doc comment already flags for every other duplicated map constant */
                    float hp_frac = last.heroes[my_owner].max_hp > 0
                        ? (float)last.heroes[my_owner].hp / (float)last.heroes[my_owner].max_hp : 0.0f;
                    if (!retreating_to_fountain && hp_frac < ARENA_BOT_LOW_HP_FRACTION) {
                        retreating_to_fountain = 1;
                        camping_fountain = 0; /* self-preservation overrides camping outright */
                    } else if (retreating_to_fountain && hp_frac >= ARENA_BOT_TOPPED_UP_FRACTION) {
                        retreating_to_fountain = 0;
                    }

                    /* Fountain camping (2026-08-10, founder: "bots need to learn and evolve from
                       fountain camping meta" -- heuristic path chosen over a learned one for this
                       first pass: real MOBA fountain-camping is a well-defined, hand-describable
                       tactic (loiter near the enemy's own heal point, punish whoever shows up
                       there), unlike the still-open RL/imitation-learning path (NORTHSTAR
                       §25.2-§25.4), which needs real training compute this box doesn't have
                       headroom for alongside the team-mode training run already in progress.
                       Trigger: a real kill or assist just landed (Flow jumped by at least
                       ARENA_BOT_CAMP_TRIGGER_FLOW between the previous and current snapshot --
                       ARENA_HERO_ASSIST_FLOW is 350, ARENA_HERO_KILL_FLOW is 1000 in
                       packages/simulation/arena_game.h, so this threshold catches both, not just
                       kills) while healthy (hp_frac >= ARENA_BOT_TOPPED_UP_FRACTION, the same 90%
                       bar the retreat state machine already uses for "fully recovered" -- don't
                       send a half-healed bot deep into enemy territory) and not already
                       retreating/camping. Bounded duration (ARENA_BOT_CAMP_DURATION_MS) so a bot
                       doesn't permanently abandon node-capping/shopping if no enemy ever shows up
                       at their own fountain -- exits early on low HP too (a camping bot that gets
                       jumped falls through to the normal retreat/engage logic next tick, same as
                       any other bot). Once camping, this doesn't touch the existing
                       nearest-enemy-engage logic at all (best/best_dist below still finds and
                       fights whoever's nearest, including an enemy who wanders back to their own
                       fountain) -- camping only changes where a bot with NO enemy currently in
                       engage range goes instead of node-capping/shopping, see the best_node
                       fallback below. */
                    const uint32_t ARENA_BOT_CAMP_TRIGGER_FLOW = 350;
                    const uint32_t ARENA_BOT_CAMP_DURATION_MS = 20000;
                    uint32_t now_camp = (uint32_t)time(NULL) * 1000;
                    if (camping_fountain) {
                        if (hp_frac < ARENA_BOT_LOW_HP_FRACTION
                            || now_camp - camping_start_ms > ARENA_BOT_CAMP_DURATION_MS) {
                            camping_fountain = 0;
                        }
                    } else if (!retreating_to_fountain && have_prev
                               && my_owner < last.world.count && my_owner < prev.world.count
                               && hp_frac >= ARENA_BOT_TOPPED_UP_FRACTION) {
                        float flow_delta = (float)last.heroes[my_owner].flow - (float)prev.heroes[my_owner].flow;
                        if (flow_delta >= (float)ARENA_BOT_CAMP_TRIGGER_FLOW) {
                            camping_fountain = 1;
                            camping_start_ms = now_camp;
                        }
                    }

                    if (retreating_to_fountain) {
                        int nearest = 0;
                        float nearest_dist = 0.0f;
                        for (int f = 0; f < 2; f++) {
                            float fdx = fountains[f][0] - mx, fdz = fountains[f][1] - mz;
                            float fdist = fdx * fdx + fdz * fdz;
                            if (f == 0 || fdist < nearest_dist) { nearest = f; nearest_dist = fdist; }
                        }
                        send_move(fountains[nearest][0], fountains[nearest][1]);
                    }

                    if (!retreating_to_fountain) {
                        int best = -1;
                        float best_dist = 0;
                        for (int i = 0; i < last.world.count; i++) {
                            if (!last.heroes[i].alive) continue;
                            int team = (i < last.world.count / 2) ? 0 : 1;
                            if (team == my_team) continue;
                            float dx = last.heroes[i].x - mx, dz = last.heroes[i].z - mz;
                            float dist = dx * dx + dz * dz;
                            if (best == -1 || dist < best_dist) { best = i; best_dist = dist; }
                        }
                        /* S170-175 Sprint 5, founder: "bot AI shop interaction" -- explicitly
                           deferred at the time ("bots simply won't buy anything yet, flagged
                           not faked"). A genuinely simple first pass: when no enemy is nearby
                           (safe, real "recall to shop" instinct) and this bot can afford the
                           next item in catalog order, detour to its own team's shop and buy it
                           -- arena_shop_buy's own server-side validation (proximity,
                           affordability, auto-sell-then-replace on an already-filled slot) does
                           all the real work, this is just deciding WHEN to go and WHICH item to
                           try next, not reasoning about build strategy. Shop positions mirror
                           arena_shop_position() exactly (S170-191: ARENA_HALF_EXTENT is now
                           32*1.618034~=51.78, corner=47.78, +/-5 diagonal offset -- was
                           ARENA_HALF_EXTENT=32/corner=28 before the golden-ratio expansion) --
                           same "kept in sync by hand" idiom as the fountain positions above,
                           this file deliberately doesn't link packages/simulation/arena_game.c. */
                        static const float shops[2][2] = { { -52.78f, 52.78f }, { 52.78f, -52.78f } };
                        float shop_safe_dist_sq = 20.0f * 20.0f;
                        int shopping = !camping_fountain && shop_next_item_id < ARENA_BOT_ITEM_COUNT
                            && (best == -1 || best_dist > shop_safe_dist_sq)
                            && (float)last.heroes[my_owner].flow >= (float)ARENA_BOT_ITEM_COSTS[shop_next_item_id];
                        if (shopping) {
                            float sx = shops[my_team][0], sz = shops[my_team][1];
                            float sdx = sx - mx, sdz = sz - mz;
                            float sdist_sq = sdx * sdx + sdz * sdz;
                            send_move(sx, sz);
                            uint32_t now_shop = (uint32_t)time(NULL) * 1000;
                            /* ARENA_SHOP_RADIUS is 3.0f (packages/simulation/arena_game.h) --
                               hardcoded here rather than duplicated as its own named constant
                               since it's used in exactly this one place. A 2s cooldown between
                               buy attempts, same shape as last_cast_ms above, so a bot parked
                               at its own shop doesn't spam repurchase the same slot every tick
                               (arena_shop_buy auto-sells then rebuys on a repeat call, which
                               would just bleed Flow to the 50%-refund loss over and over). */
                            if (sdist_sq <= 3.0f * 3.0f && now_shop - last_shop_buy_ms > 2000) {
                                send_shop_buy(shop_next_item_id);
                                shop_next_item_id++;
                                last_shop_buy_ms = now_shop;
                            }
                        }
                        if (!shopping) {
                        /* S170-155, founder: "add resource management (node capping) to the
                           bot AI heuristic and brain ... first pass." Before this, bots did
                           nothing but chase whichever enemy hero was nearest, anywhere on the
                           map -- with the resource race (S170-153) now the actual win
                           condition, a bot that never once stands on a node can't meaningfully
                           contribute to winning. First-pass split: engage a nearby enemy if one
                           is actually close enough to be a real threat/opportunity (unchanged
                           feel for real skirmishes); otherwise, walk to and hold the nearest
                           node this bot's team doesn't already own, same "keep capturing
                           ground when nothing's fighting you" behavior a real Arathi Basin
                           player falls back to. Only chases a distant enemy again once the
                           team already owns every node (nothing left to capture). */
                        float engage_range_sq = 15.0f * 15.0f;
                        /* S170-202: squad membership/target computed unconditionally (cheap --
                           see hero_squad_count's own doc comment for the sizing reasoning), used
                           by both the node-capping branch below and flock_offset's own now
                           squad-scoped flocking, whichever branch ends up mattering this tick. */
                        int squad_count = hero_squad_count(&last, my_team);
                        int my_squad = my_owner % squad_count;
                        int best_node = -1;
                        if (best == -1 || best_dist > engage_range_sq) {
                            if (camping_fountain) {
                                /* No enemy in engage range while camping -- head for the enemy
                                   fountain instead of capping a node (see this block's own doc
                                   comment above at the trigger for the full reasoning). best_node
                                   stays -1 (skips the node-anchor branch below) and best is
                                   already -1/out-of-range (skips the engage branch too), so this
                                   send_move is the only movement command this tick -- exactly the
                                   desired "go camp, do nothing else" behavior. The moment an enemy
                                   DOES wander into engage range, best != -1 takes over next tick
                                   via the unchanged engage branch below, same as any other engage. */
                                int enemy_fountain = 1 - my_team;
                                send_move(fountains[enemy_fountain][0], fountains[enemy_fountain][1]);
                            } else {
                                /* Powerup awareness (2026-08-10, founder: "heuristically make
                                   bots aware of berserk and regen powerups" -- "currently they
                                   only pick them up if they happen to run over em"). Both
                                   powerups are already synced over the wire
                                   (last.world.powerups[], S170-190) but nothing ever pathed
                                   toward one -- a bot only ever grabbed one by coincidence, if
                                   node-capping/engaging happened to walk it through the exact
                                   pickup point. Detour to the nearest ACTIVE powerup within
                                   ARENA_BOT_POWERUP_SEEK_RADIUS instead of node-capping this
                                   tick, same "worth a detour, not worth abandoning the whole
                                   game plan for" priority the shopping detour above already
                                   uses -- only considered here (no enemy in engage range this
                                   tick), so grabbing a powerup never pulls a bot out of a fight
                                   it's already in. */
                                const float ARENA_BOT_POWERUP_SEEK_RADIUS = 25.0f;
                                int nearest_powerup = -1;
                                float nearest_powerup_dist = 0.0f;
                                for (int p = 0; p < (int)(sizeof(last.world.powerups) / sizeof(last.world.powerups[0])); p++) {
                                    if (!last.world.powerups[p].active) continue;
                                    float pdx = last.world.powerups[p].x - mx, pdz = last.world.powerups[p].z - mz;
                                    float pdist = pdx * pdx + pdz * pdz;
                                    if (pdist > ARENA_BOT_POWERUP_SEEK_RADIUS * ARENA_BOT_POWERUP_SEEK_RADIUS) continue;
                                    if (nearest_powerup == -1 || pdist < nearest_powerup_dist) {
                                        nearest_powerup = p;
                                        nearest_powerup_dist = pdist;
                                    }
                                }
                                if (nearest_powerup != -1) {
                                    send_move(last.world.powerups[nearest_powerup].x, last.world.powerups[nearest_powerup].z);
                                } else {
                                    best_node = hero_squad_target_node(&last, my_team, squad_count, my_squad);
                                }
                            }
                        }
                        /* S170-160/S170-202: squad flocking (alignment/cohesion/separation among
                           living SQUADMATES, see flock_offset's own doc comment) is a small
                           perturbation added on top of whichever objective target gets picked
                           below -- real goal-seeking (capture the node, engage the enemy)
                           always still drives the bot, flocking just makes the squad's motion
                           toward that goal feel organic instead of every bot pathing as an
                           island. */
                        float flock_dx, flock_dz;
                        flock_offset(&last, &prev, have_prev, my_owner, my_team, squad_count, my_squad, &flock_dx, &flock_dz);

                        if (best_node != -1) {
                            /* S170-168's original fix: separation force is strongest exactly
                               when allies are close together, which is unavoidably true the
                               moment several bots converge on the same node -- flocking kept
                               perturbing everyone off the node's exact point forever, never
                               letting anyone actually settle there. Its "anchor" rule (whichever
                               bot's owner index mod ARENA_SNAPSHOT_NODE_COUNT matched this
                               node's own index ignored the flock and pathed straight to the
                               node) worked, but only by coincidence: a bot's OWNER SLOT is
                               permanent and has nothing to do with which node it's actually
                               heading to on any given tick. If neither of a node's two
                               coincidental slot-owners happened to be heading there right now
                               (dead, engaged with an enemy instead, already anchoring a
                               different node), nobody ever anchored it -- every other bot
                               flocked around it forever, and flock_offset's own separation term
                               alone can push a crowded bot's real move target outside
                               ARENA_NODE_CAPTURE_RADIUS, so the crowd never actually registered
                               as team_present[] there. That node stayed silently uncappable for
                               as long as its two coincidental slot-owners stayed unavailable,
                               while every OTHER node capped fine -- S170-201, founder: "some
                               issue with flocking my team having a lot of trouble capping a
                               node" (singular: exactly this per-node failure mode, not every
                               node at once). S170-202 then found the OTHER half of the same
                               complaint ("the whole team doesnt need to try to cap the node"):
                               every idle bot picking its own single nearest node also meant the
                               WHOLE team piled onto the same one, leaving every other node
                               uncontested -- fixed by hero_squad_target_node above splitting
                               the team into squads that each claim a distinct node.

                               With squads now doing that claiming, every LIVING member of MY
                               OWN squad has, by construction, the exact same best_node -- so the
                               anchor question collapses to "am I my own squad's lowest owner
                               index," no per-candidate node lookup needed anymore. */
                            int am_anchor = 1;
                            for (int i = 0; i < last.world.count; i++) {
                                if (i == my_owner || !last.heroes[i].alive) continue;
                                int team_i = (i < last.world.count / 2) ? 0 : 1;
                                if (team_i != my_team || (i % squad_count) != my_squad) continue;
                                if (i < my_owner) { am_anchor = 0; break; }
                            }
                            if (am_anchor) {
                                send_move(last.world.nodes[best_node].x, last.world.nodes[best_node].z);
                            } else {
                                send_move(last.world.nodes[best_node].x + flock_dx, last.world.nodes[best_node].z + flock_dz);
                            }
                        } else if (best != -1) {
                            /* S170-90 fix, real bug found live: "all of the bots just bunch up on
                               eachother." Root cause -- every bot sent its move target as the
                               nearest enemy's *exact* (x,z). Whenever several bots shared the same
                               nearest enemy (common once a team clusters up), they'd all converge on
                               the literal same point and stack. Spread each bot to its own approach
                               angle around the target instead, derived from its stable owner index
                               (no coordination needed between bots, no shared state) -- a real
                               surround formation rather than a single pile. Radius is just outside
                               ARENA_ATTACK_RANGE (1.6f) so bots still end up in melee range of the
                               target, just not literally on top of each other or it. */
                            float approach_angle = (float)(my_owner % 8) * (2.0f * 3.14159265f / 8.0f);
                            float approach_radius = 2.0f;
                            /* rl_engage_nudge (see its own doc comment): the trained RL policy's
                               suggested step, added on top of -- not instead of -- the angle
                               spread above, so S170-90's anti-stack guarantee still holds even
                               with several bots independently consulting the same network. */
                            float rl_dx, rl_dz;
                            rl_engage_nudge(&last, my_owner, best, &rl_dx, &rl_dz);
                            /* team_rl_engage_nudge (see its own doc comment): the team-trained
                               checkpoint's suggestion, additive on top of everything else here --
                               a no-op (0,0) whenever this isn't a real 3v3 match (the team_size
                               gate inside), so this line is a safe no-op in the live 10v10 pool. */
                            float team_rl_dx, team_rl_dz;
                            team_rl_engage_nudge(&last, my_owner, best, my_team, &team_rl_dx, &team_rl_dz);
                            float tx = last.heroes[best].x + cosf(approach_angle) * approach_radius + flock_dx + rl_dx + team_rl_dx;
                            float tz = last.heroes[best].z + sinf(approach_angle) * approach_radius + flock_dz + rl_dz + team_rl_dz;
                            send_move(tx, tz);
                            /* S170-162/165: sent AFTER send_move on purpose, see
                               send_attack's own doc comment -- this is what
                               actually makes a bot-piloted ranged hero (Gary)
                               deal any damage at all, and gives every other bot
                               the real chase-a-fleeing-target behavior for free
                               too, on top of the approach-angle move above. */
                            send_attack(best);
                            uint32_t now = (uint32_t)time(NULL) * 1000;
                            if (now - last_cast_ms > 2000) {
                                send_cast(0); /* Q -- server no-ops it if actually on cooldown */
                                last_cast_ms = now;
                            }
                        }
                        } /* !shopping */
                    } /* !retreating_to_fountain */
                }
            }
        }
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000); /* 10 decisions/sec -- plenty for a first AI pass */
#endif
    }
}

int main(int argc, char *argv[]) {
    const char *ip = "127.0.0.1";
    int direct_port = 0; /* 0 = go through the arena matchmaker */
    int name_index = -1;
    /* --matchmaker-port (2026-08-10, founder: "we need full duplicate backend surfaces
       including matchmaking and bot pools" -- REDGARDEN is now the R&D deployment,
       GoblinFoxDragon's Battlegrounds gets its own separate, stable matchmaker instance on a
       different port): ARENA_MATCHMAKER_PORT was a hardcoded #define with no runtime override,
       which meant this binary could only ever point at one matchmaker regardless of which
       deployment (R&D vs. stable) built or ran it -- the two deployments need the SAME source,
       different runtime config, not a source fork. Defaults to ARENA_MATCHMAKER_PORT so every
       existing invocation (scripts/run_bot_pool.sh, ops/systemd/redgarden-bot-pool.service, any
       manual run) is unaffected. */
    int matchmaker_port = ARENA_MATCHMAKER_PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) ip = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) direct_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--index") == 0 && i + 1 < argc) name_index = atoi(argv[++i]);
        else if (strcmp(argv[i], "--matchmaker-port") == 0 && i + 1 < argc) matchmaker_port = atoi(argv[++i]);
    }
    /* --index (from scripts/launch_arena_pools.sh's spawn loop): a
       deterministic, stable name per pool slot, so restarting the pool
       doesn't reshuffle who's who on the leaderboard. Falls back to a
       pid-derived pick for anyone running the binary directly. */
    g_bot_name = ARENA_BOT_NAMES[(name_index >= 0 ? (unsigned int)name_index : (unsigned int)getpid()) % ARENA_BOT_NAME_COUNT];

    setbuf(stdout, NULL);
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    load_iduna_agent_config();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    while (1) {
        int game_port = direct_port;
        if (game_port == 0) {
            struct sockaddr_in mm_addr;
            mm_addr.sin_family = AF_INET;
            mm_addr.sin_port = htons((uint16_t)matchmaker_port);
            mm_addr.sin_addr.s_addr = inet_addr(ip);
            printf("[arena_bot %d] finding match via arena matchmaker %s:%d...\n", (int)getpid(), ip, matchmaker_port);
            send_find_match(&mm_addr);
            game_port = wait_for_match(&mm_addr);
            if (game_port < 0) {
                fprintf(stderr, "[arena_bot %d] matchmaker timeout, retrying\n", (int)getpid());
                continue;
            }
            printf("[arena_bot %d] matched -> game server port %d\n", (int)getpid(), game_port);
        }

        my_owner = -1;
        if (connect_to_server(ip, game_port)) {
            play_one_match(game_port);
        } else {
            fprintf(stderr, "[arena_bot %d] failed to connect to server on port %d\n", (int)getpid(), game_port);
        }

        if (direct_port != 0) break; /* direct-connect mode plays exactly one match, matching apps/arena's own --connect */
#ifdef _WIN32
        Sleep(1000);
#else
        usleep(1000000); /* brief pause before requeuing -- persistent, not a tight crash loop */
#endif
    }
    return 0;
}
