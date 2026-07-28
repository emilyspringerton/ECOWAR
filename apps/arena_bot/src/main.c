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

#define TICKET_PAYLOAD_LEN 20
#define TICKET_MAC_LEN 16
#define TICKET_TOTAL_LEN (TICKET_PAYLOAD_LEN + TICKET_MAC_LEN)
#define ARENA_MATCHMAKER_PORT 7778 /* separate queue from the card-RTS matchmaker's 7777 */
/* Mirrors packages/simulation/arena_game.h's ARENA_HERO_COUNT -- this file is a pure network
   client and deliberately doesn't include the sim header (no direct ArenaState access, wire
   protocol only), so the roster size has to be kept in sync by hand here. Bump this alongside
   ARENA_HERO_COUNT whenever a new hero is added. */
#define ARENA_HERO_COUNT 26

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
    sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
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
 * This computes a small steering offset from nearby, living TEAMMATES
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
 * path. */
static void flock_offset(const ArenaSnapshotMsg *cur, const ArenaSnapshotMsg *prev, int have_prev,
                          int self_owner, int my_team, float *out_dx, float *out_dz) {
    const float FLOCK_RADIUS = 6.0f;
    const float SEPARATION_RADIUS = 2.0f;
    float mx = cur->heroes[self_owner].x, mz = cur->heroes[self_owner].z;
    float align_x = 0.0f, align_z = 0.0f;
    float coh_x = 0.0f, coh_z = 0.0f;
    float sep_x = 0.0f, sep_z = 0.0f;
    int count = 0;

    for (int i = 0; i < cur->count; i++) {
        if (i == self_owner || !cur->heroes[i].alive) continue;
        int team = (i < cur->count / 2) ? 0 : 1;
        if (team != my_team) continue; /* teammates only -- see doc comment above */

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

// play_one_match runs the draft + live-play loop for a single match against
// the already-connected server. Returns once the match ends (winner != 0)
// or the connection goes quiet for too long.
static void play_one_match(int game_port) {
    ArenaSnapshotMsg last = {0};
    ArenaSnapshotMsg prev = {0}; /* S170-160: previous tick's snapshot, purely so flock_offset can infer ally velocity for alignment -- the wire snapshot itself never carries velocity */
    int have_prev = 0;
    int have_snapshot = 0;
    int picked = 0;
    int ticks_since_pick_send = 0; /* retry, see below -- S170-99 */
    uint32_t last_cast_ms = 0;
    int silent_ticks = 0;
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
        char rbuf[2048];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        int got_one = 0;
        while (len > 0) {
            if (len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotMsg))) {
                NetHeader *h = (NetHeader *)rbuf;
                if (h->type == PACKET_ARENA_SNAPSHOT) {
                    prev = last;
                    have_prev = have_snapshot;
                    memcpy(&last, rbuf + sizeof(NetHeader), sizeof(ArenaSnapshotMsg));
                    have_snapshot = 1;
                    got_one = 1;
                }
            }
            len = recvfrom(sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        }
        silent_ticks = got_one ? 0 : silent_ticks + 1;
        if (silent_ticks > 1000) { /* ~10s of nothing at all -- server's gone */
            fprintf(stderr, "[arena_bot %d] no snapshots for 10s -- giving up on this match\n", (int)getpid());
            return;
        }

        if (have_snapshot) {
            if (last.phase == ARENA_PHASE_DRAFT && !picked) {
                /* Simple roster spread: pick based on owner slot so a full
                   lobby doesn't converge on one hero -- real draft strategy
                   is a later, separate concern. */
                int hero_id = (my_owner + draft_offset) % ARENA_HERO_COUNT; /* full roster, rotating exclusion -- see draft_offset's own comment */
                send_pick(hero_id);
                picked = 1;
                ticks_since_pick_send = 0;
                printf("[arena_bot %d] drafted hero_id=%d\n", (int)getpid(), hero_id);
            } else if (last.phase == ARENA_PHASE_DRAFT && picked) {
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
            } else if (last.phase == ARENA_PHASE_LIVE) {
                if (last.winner != 0) {
                    printf("[arena_bot %d] match ended, winner=%d\n", (int)getpid(), last.winner);
                    return;
                }
                if (my_owner >= 0 && my_owner < last.count && last.heroes[my_owner].alive) {
                    /* Nearest enemy from the snapshot -- this bot has no
                       access to the authoritative ArenaState, only what any
                       client sees, same information a human player's client
                       would have. */
                    float mx = last.heroes[my_owner].x, mz = last.heroes[my_owner].z;
                    int my_team = (my_owner < last.count / 2) ? 0 : 1;
                    int best = -1;
                    float best_dist = 0;
                    for (int i = 0; i < last.count; i++) {
                        if (!last.heroes[i].alive) continue;
                        int team = (i < last.count / 2) ? 0 : 1;
                        if (team == my_team) continue;
                        float dx = last.heroes[i].x - mx, dz = last.heroes[i].z - mz;
                        float dist = dx * dx + dz * dz;
                        if (best == -1 || dist < best_dist) { best = i; best_dist = dist; }
                    }
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
                    int want_owner = my_team + 1; /* ArenaNodeSnapshot.owner: 1=team0, 2=team1 */
                    int best_node = -1;
                    float best_node_dist = 0;
                    if (best == -1 || best_dist > engage_range_sq) {
                        for (int n = 0; n < ARENA_SNAPSHOT_NODE_COUNT; n++) {
                            if (last.nodes[n].owner == want_owner) continue; /* already ours */
                            float dx = last.nodes[n].x - mx, dz = last.nodes[n].z - mz;
                            float dist = dx * dx + dz * dz;
                            if (best_node == -1 || dist < best_node_dist) { best_node = n; best_node_dist = dist; }
                        }
                    }
                    /* S170-160: squad flocking (alignment/cohesion/separation among
                       living teammates, see flock_offset's own doc comment) is a small
                       perturbation added on top of whichever objective target gets picked
                       below -- real goal-seeking (capture the node, engage the enemy)
                       always still drives the bot, flocking just makes the group's motion
                       toward that goal feel organic instead of every bot pathing as an
                       island. */
                    float flock_dx, flock_dz;
                    flock_offset(&last, &prev, have_prev, my_owner, my_team, &flock_dx, &flock_dz);

                    if (best_node != -1) {
                        /* S170-168 fix, real bug found live: "the boyds stuff makes the
                           team do a weird cluster dance around the objective ... they are
                           doing the boids dance around the objective not sitting right on
                           it" -> "at least one of them should sit right on it and ignore
                           the flock." Root cause: separation force is strongest exactly
                           when allies are close together, which is unavoidably true the
                           moment several bots converge on the same node -- flocking kept
                           perturbing everyone off the node's exact point forever, never
                           letting anyone actually settle there. Fix: a stateless, no-
                           coordination-needed "anchor" rule -- whichever bots' owner index
                           happens to land on this node's own index mod ARENA_SNAPSHOT_NODE_COUNT
                           ignore the flock entirely and path straight to the node's exact
                           (x,z), guaranteeing real capture progress; every other bot still
                           flocks around it as a loose escort, which is the actual organic
                           "fan out and hold the ground around the objective" look this was
                           always meant to produce. */
                        int am_anchor = (my_owner % ARENA_SNAPSHOT_NODE_COUNT) == best_node;
                        if (am_anchor) {
                            send_move(last.nodes[best_node].x, last.nodes[best_node].z);
                        } else {
                            send_move(last.nodes[best_node].x + flock_dx, last.nodes[best_node].z + flock_dz);
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
                        float tx = last.heroes[best].x + cosf(approach_angle) * approach_radius + flock_dx;
                        float tz = last.heroes[best].z + sinf(approach_angle) * approach_radius + flock_dz;
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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) ip = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) direct_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--index") == 0 && i + 1 < argc) name_index = atoi(argv[++i]);
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
            mm_addr.sin_port = htons(ARENA_MATCHMAKER_PORT);
            mm_addr.sin_addr.s_addr = inet_addr(ip);
            printf("[arena_bot %d] finding match via arena matchmaker %s:%d...\n", (int)getpid(), ip, ARENA_MATCHMAKER_PORT);
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
