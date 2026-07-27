/* RED GARDEN — single-hero click-to-move arena demo.
 *
 * New, additive client: does not touch apps/lobby or the existing card-RTS.
 * Modern-GL (shader) rendering on purpose -- this sidesteps the GL/glu.h
 * dependency that blocks apps/lobby on this box (no libglu1-mesa-dev
 * installed): a shader pipeline only needs GL/gl.h + SDL_GL_GetProcAddress
 * function loading, no GLU, no GLEW/GLAD.
 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "../../../packages/common/mat4.h"
#include "../../../packages/common/protocol.h"
#include "../../../packages/common/hmac_sha256.h"
#include "../../../packages/common/http_client.h"
#include "../../../packages/simulation/arena_game.h"
#include "../../../packages/simulation/arena_ai_bridge.h"
#include "../../../packages/simulation/arena_replay.h"

/* ---------------- networked PvP (2026-07-24 pivot, NORTHSTAR §13) ----------------
 * Local-only mode (no --connect flag) is unchanged: my_owner stays 0,
 * arena_update() runs fully client-side against the built-in bot. In
 * network mode, apps/arena_server is authoritative -- this client only
 * sends move/cast commands and applies incoming snapshots, never calls
 * arena_update() itself. */
static int net_mode = 0;
static int my_owner = 0; /* which arena_state.heroes[] slot is "me" -- 0 in local mode always */

/* Toggleable APM overlay (S170-71): off by default, F11 flips it. Ring buffer of action
 * timestamps (moves + Q/W/E casts) so the on-screen number is a real trailing-60s rate, not a
 * running-average-since-launch. */
#define APM_RING_CAP 512
static int show_apm = 0;
static uint32_t apm_ring[APM_RING_CAP];
static int apm_ring_head = 0;
static int apm_ring_count = 0;

static void apm_record_action(uint32_t now_ms) {
    apm_ring[apm_ring_head] = now_ms;
    apm_ring_head = (apm_ring_head + 1) % APM_RING_CAP;
    if (apm_ring_count < APM_RING_CAP) apm_ring_count++;
}

static int apm_compute(uint32_t now_ms) {
    int count = 0;
    for (int i = 0; i < apm_ring_count; i++) {
        int idx = (apm_ring_head - 1 - i + APM_RING_CAP) % APM_RING_CAP;
        if (now_ms - apm_ring[idx] > 60000) break; /* ring is time-ordered -- stop at the first stale entry */
        count++;
    }
    return count;
}

/* This whole networking section (through the matching closing comment
 * below) used to be #ifndef _WIN32-only, with main() stubbing out
 * --connect/--queue entirely on Windows as a result. Now that the
 * platform-specific internals (winsock includes, ioctlsocket/fcntl,
 * closesocket/close, GetCurrentProcessId/getpid, mkdir) are each guarded
 * individually where they actually differ, this compiles and works on
 * both -- S170-54, found by actually watching the Windows cross-compile
 * fail rather than assuming the workflow alone would catch it. */
#define ARENA_TICKET_PAYLOAD_LEN 20
#define ARENA_TICKET_TOTAL_LEN (ARENA_TICKET_PAYLOAD_LEN + 16)

static int net_sock = -1;
static struct sockaddr_in net_server_addr;

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

/* Same register+mint round trip as apps/client/bot_main.c's
 * get_real_wotan_ticket -- ported here rather than shared via a header,
 * since this codebase duplicates per-binary orchestration logic (see
 * apps/server vs apps/arena_server) rather than linking .c files across
 * build targets. */
static int get_real_wotan_ticket(unsigned char out[ARENA_TICKET_TOTAL_LEN]) {
    char resp[4096];
    int status = 0;

    char login_body[512];
    snprintf(login_body, sizeof(login_body),
             "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}",
             iduna_agent_name, iduna_agent_secret);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/auth/agent", NULL,
                        login_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "WOTAN: agent login failed (status=%d)\n", status);
        return 0;
    }
    char token[2048];
    if (!http_extract_json_string_field(resp, "access_token", token, sizeof(token))) {
        fprintf(stderr, "WOTAN: agent login response missing access_token\n");
        return 0;
    }

    char provider_sub[64];
#ifdef _WIN32
    snprintf(provider_sub, sizeof(provider_sub), "player-%lu-%u",
             (unsigned long)GetCurrentProcessId(), (unsigned int)time(NULL));
#else
    snprintf(provider_sub, sizeof(provider_sub), "player-%d-%u",
             (int)getpid(), (unsigned int)time(NULL));
#endif
    char register_body[256];
    snprintf(register_body, sizeof(register_body),
             "{\"provider\":\"redgarden_bot\",\"provider_sub\":\"%s\"}", provider_sub);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/players/register", token,
                        register_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "WOTAN: player registration failed (status=%d)\n", status);
        return 0;
    }
    char player_id[64];
    if (!http_extract_json_string_field(resp, "player_id", player_id, sizeof(player_id))) {
        fprintf(stderr, "WOTAN: registration response missing player_id\n");
        return 0;
    }

    char ticket_body[128];
    snprintf(ticket_body, sizeof(ticket_body), "{\"player_id\":\"%s\"}", player_id);
    if (http_post_json(iduna_host, iduna_port, "/api/v1/redgarden/ticket", token,
                        ticket_body, resp, sizeof(resp), &status) != 0 || status != 200) {
        fprintf(stderr, "WOTAN: ticket mint failed (status=%d)\n", status);
        return 0;
    }
    char ticket_hex[128];
    if (!http_extract_json_string_field(resp, "ticket", ticket_hex, sizeof(ticket_hex))) {
        fprintf(stderr, "WOTAN: ticket response missing ticket field\n");
        return 0;
    }
    if (!hex_decode(ticket_hex, out, ARENA_TICKET_TOTAL_LEN)) {
        fprintf(stderr, "WOTAN: ticket field was not valid %d-byte hex\n", ARENA_TICKET_TOTAL_LEN);
        return 0;
    }
    printf("WOTAN: real identity registered -- player_id=%s\n", player_id);
    return 1;
}

/* Self-mint fallback, same scheme as bot_main.c's mint_ticket -- used only
 * if IDUNA isn't configured/reachable, so local network-mode testing
 * without a running IDUNA doesn't hard-fail. */
static void mint_ticket_fallback(const char *secret, unsigned char out[ARENA_TICKET_TOTAL_LEN]) {
    unsigned char payload[ARENA_TICKET_PAYLOAD_LEN];
    for (int i = 0; i < 16; i++) payload[i] = (unsigned char)(rand() & 0xFF);
    uint32_t expires_at = (uint32_t)time(NULL) + 300;
    payload[16] = (unsigned char)(expires_at & 0xFF);
    payload[17] = (unsigned char)((expires_at >> 8) & 0xFF);
    payload[18] = (unsigned char)((expires_at >> 16) & 0xFF);
    payload[19] = (unsigned char)((expires_at >> 24) & 0xFF);
    unsigned char mac[32];
    hmac_sha256((const unsigned char *)secret, strlen(secret), payload, ARENA_TICKET_PAYLOAD_LEN, mac);
    memcpy(out, payload, ARENA_TICKET_PAYLOAD_LEN);
    memcpy(out + ARENA_TICKET_PAYLOAD_LEN, mac, 16);
}

static int net_connect(const char *host, int port) {
    net_sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(net_sock, FIONBIO, &mode);
#else
    int flags = fcntl(net_sock, F_GETFL, 0);
    fcntl(net_sock, F_SETFL, flags | O_NONBLOCK);
#endif

    net_server_addr.sin_family = AF_INET;
    net_server_addr.sin_port = htons((uint16_t)port);
    net_server_addr.sin_addr.s_addr = inet_addr(host);

    unsigned char ticket[ARENA_TICKET_TOTAL_LEN];
    int have_ticket = 0;
    if (iduna_agent_configured) {
        have_ticket = get_real_wotan_ticket(ticket);
    }
    if (!have_ticket) {
        const char *secret = getenv("REDGARDEN_TICKET_SECRET");
        if (!secret || !secret[0]) {
            fprintf(stderr, "No WOTAN identity and no REDGARDEN_TICKET_SECRET -- cannot connect.\n");
            return 0;
        }
        fprintf(stderr, "WOTAN: falling back to self-minted ticket (no real identity)\n");
        mint_ticket_fallback(secret, ticket);
    }

    char buf[sizeof(NetHeader) + ARENA_TICKET_TOTAL_LEN];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_CONNECT;
    memcpy(buf + sizeof(NetHeader), ticket, ARENA_TICKET_TOTAL_LEN);
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));

    /* Wait (briefly, blocking with retries) for PACKET_WELCOME so we know
       our own hero slot before the render loop starts. */
    for (int tries = 0; tries < 100; tries++) {
        char rbuf[64];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)sizeof(NetHeader)) {
            NetHeader *rh = (NetHeader *)rbuf;
            if (rh->type == PACKET_WELCOME) {
                my_owner = rh->client_id;
                printf("Connected -- assigned hero slot %d\n", my_owner);
                return 1;
            }
        }
        SDL_Delay(50);
        if (tries % 10 == 0) {
            sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
        }
    }
    fprintf(stderr, "Timed out waiting for server welcome.\n");
    return 0;
}

/* net_find_and_connect -- queue into the matchmaker's pool (the same one
 * apps/arena_bot's persistent bots queue into) instead of connecting to an
 * already-known server:port. Reuses net_connect's ticket-minting/PACKET_CONNECT
 * handshake for the actual game connection once a match is assigned; only the
 * "how do I find a port" step differs from --connect. Lets a real human join
 * whatever match the bot pool is currently matchmaking into (NORTHSTAR §13,
 * "the human will join the bot games to validate, bot-first feedback loop"). */
static int net_find_and_connect(const char *mm_host, int mm_port) {
    net_sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(net_sock, FIONBIO, &mode);
#else
    int flags = fcntl(net_sock, F_GETFL, 0);
    fcntl(net_sock, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in mm_addr = {0};
    mm_addr.sin_family = AF_INET;
    mm_addr.sin_port = htons((uint16_t)mm_port);
    mm_addr.sin_addr.s_addr = inet_addr(mm_host);

    NetHeader find = {0};
    find.type = PACKET_FIND_MATCH;
    sendto(net_sock, (const char *)&find, sizeof(find), 0, (struct sockaddr *)&mm_addr, sizeof(mm_addr));

    printf("Queuing for a match at %s:%d ...\n", mm_host, mm_port);
    int game_port = -1;
    for (int retry_ticks = 0; retry_ticks < 1200; retry_ticks++) {
        char buf[64];
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        int len = recvfrom(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&sender, &slen);
        if (len >= (int)(sizeof(NetHeader) + sizeof(MatchFoundMsg))) {
            NetHeader *h = (NetHeader *)buf;
            if (h->type == PACKET_MATCH_FOUND) {
                MatchFoundMsg *msg = (MatchFoundMsg *)(buf + sizeof(NetHeader));
                game_port = msg->port;
                break;
            }
        }
        SDL_Delay(100);
        /* Resend every ~5s, not every tick -- same same-box retry-race
           reasoning as apps/arena_bot's wait_for_match (found live, S170-43):
           resending too eagerly can race the matchmaker's own near-instant
           reply and re-enqueue a phantom entry. */
        if (retry_ticks % 50 == 0 && retry_ticks > 0) {
            sendto(net_sock, (const char *)&find, sizeof(find), 0, (struct sockaddr *)&mm_addr, sizeof(mm_addr));
        }
    }
    if (game_port < 0) {
        fprintf(stderr, "Timed out waiting for a match (60s). Is the matchmaker/bot pool running?\n");
        return 0;
    }
    printf("Match found on port %d -- connecting...\n", game_port);
    /* net_connect opens its own fresh socket; close the queue socket first. */
#ifdef _WIN32
    closesocket(net_sock);
#else
    close(net_sock);
#endif
    net_sock = -1;
    return net_connect(mm_host, game_port);
}

static void net_send_move(float x, float z) {
    char buf[sizeof(NetHeader) + sizeof(ArenaMoveCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_MOVE;
    ArenaMoveCmd *cmd = (ArenaMoveCmd *)(buf + sizeof(NetHeader));
    cmd->target_x = x;
    cmd->target_z = z;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static void net_send_cast(int slot) {
    char buf[sizeof(NetHeader) + sizeof(ArenaCastCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_CAST;
    ArenaCastCmd *cmd = (ArenaCastCmd *)(buf + sizeof(NetHeader));
    cmd->slot = (uint8_t)slot;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static void net_send_pick(int hero_id) {
    char buf[sizeof(NetHeader) + sizeof(ArenaPickCmd)];
    NetHeader *h = (NetHeader *)buf;
    memset(h, 0, sizeof(NetHeader));
    h->type = PACKET_ARENA_PICK;
    ArenaPickCmd *cmd = (ArenaPickCmd *)(buf + sizeof(NetHeader));
    cmd->hero_id = (uint8_t)hero_id;
    sendto(net_sock, buf, sizeof(buf), 0, (struct sockaddr *)&net_server_addr, sizeof(net_server_addr));
}

static int net_lobby_size = 2; /* set from the server's own msg->count once a snapshot arrives */
static uint8_t net_phase = ARENA_PHASE_WAITING;
static int net_picked = 0; /* have we sent our PACKET_ARENA_PICK for the current draft yet */
static uint32_t net_last_pick_send_ms = 0; /* for retry -- see net_poll_snapshots' resend logic */
/* net_draft_offset (S170-105, real bug found live): ARENA_HERO_COUNT (21) now exceeds
   ARENA_MAX_HEROES (20) -- a bare `owner % hero_count` in a full 20-player lobby can never reach
   the last hero in the roster, a permanent exclusion, not a rare miss (same finding as
   apps/arena_bot's own fix). Re-rolled every time net_picked resets to 0 (a fresh draft is about
   to start), so which hero ends up excluded varies match to match instead of always being the
   same one hero nobody can ever pick. */
static int net_draft_offset = 0;

/* Defined further down alongside the other particle-effect state
   (spawn_ring/AttackFlash) -- forward-declared here so net_poll_snapshots
   can consume the wire's cast_flash_slot the instant a snapshot arrives. */
static void spawn_spell_flash(float x, float z, int slot);
static void play_tone(float freq_hz, float duration_ms, float volume);
static void play_cast_tone(int slot);
static void trigger_squish(int owner);
#define ARENA_AUDIO_HEARING_RADIUS 15.0f /* how far from the local player's own hero a cast/hit sound is still audible */

static void net_poll_snapshots(uint32_t now_ms) {
    char rbuf[2048];
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);
    int len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
    while (len > 0) {
        if (len >= (int)(sizeof(NetHeader) + sizeof(ArenaSnapshotMsg))) {
            NetHeader *h = (NetHeader *)rbuf;
            if (h->type == PACKET_ARENA_SNAPSHOT) {
                ArenaSnapshotMsg *msg = (ArenaSnapshotMsg *)(rbuf + sizeof(NetHeader));
                net_lobby_size = msg->count;
                net_phase = msg->phase;
                if (net_phase == ARENA_PHASE_DRAFT && !net_picked) {
                    /* Auto-draft (S170-66/68): the client has no pick UI yet, and the
                       founder confirmed auto-draft is fine for now -- same roster-spread
                       rule apps/arena_bot already uses, so the human doesn't get stuck
                       in ARENA_PHASE_DRAFT forever waiting on input that never comes. */
                    /* Derived from the connected server's own port, not rand() -- every client in
                       this match (bots included, see apps/arena_bot's identical fix) already
                       knows this same port, so it's a real shared value with zero coordination
                       needed, not an independent per-client random roll that could collide with
                       someone else's pick on the same hero. See net_draft_offset's own doc
                       comment for the full "why not rand()" reasoning. */
                    net_draft_offset = ntohs(net_server_addr.sin_port) % ARENA_HERO_COUNT;
                    int hero_id = (my_owner + net_draft_offset) % ARENA_HERO_COUNT;
                    net_send_pick(hero_id);
                    net_picked = 1;
                    net_last_pick_send_ms = now_ms;
                    printf("[arena client] auto-drafted hero_id=%d for slot %d\n", hero_id, my_owner);
                    fflush(stdout);
                } else if (net_phase != ARENA_PHASE_DRAFT) {
                    net_picked = 0; /* reset so the next draft (after a requeue) picks again */
                }
                for (int i = 0; i < msg->count && i < ARENA_SNAPSHOT_MAX_HEROES; i++) {
                    ArenaHero *dst = &arena_state.heroes[i];
                    dst->x = msg->heroes[i].x;
                    dst->z = msg->heroes[i].z;
                    dst->hp = msg->heroes[i].hp;
                    dst->max_hp = msg->heroes[i].max_hp;
                    dst->alive = msg->heroes[i].alive;
                    dst->active = 1;
                    dst->team = (i < msg->count / 2) ? 0 : 1;
                    dst->hero_id = (ArenaHeroID)msg->heroes[i].hero_id;
                    /* S170-137: ability-tile readiness needs real cooldown/mana state, not the
                       zeroed default net_mode left them at forever (see the field's own doc
                       comment in protocol.h). */
                    dst->q_cooldown_ms = msg->heroes[i].q_cooldown_ms;
                    dst->w_cooldown_ms = msg->heroes[i].w_cooldown_ms;
                    dst->r_cooldown_ms = msg->heroes[i].r_cooldown_ms;
                    dst->mp = msg->heroes[i].mp;
                    if (msg->heroes[i].cast_flash_slot > 0) {
                        spawn_spell_flash(dst->x, dst->z, msg->heroes[i].cast_flash_slot);
                        trigger_squish(i);
                        /* Hearing range (S170-92): a real 20-hero match can have several
                           casts landing every second across the whole map -- unfiltered,
                           that's noise, not legibility. Only sound cues for casts within a
                           reasonable radius of the local player's own hero, same "you can
                           hear nearby fights, not the whole battlefield" scoping real games
                           use for audio falloff. */
                        float adx = dst->x - arena_state.heroes[my_owner].x;
                        float adz = dst->z - arena_state.heroes[my_owner].z;
                        if (adx * adx + adz * adz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                            play_cast_tone(msg->heroes[i].cast_flash_slot);
                        }
                    }
                }
                arena_state.winner = msg->winner;
                for (int i = 0; i < ARENA_SNAPSHOT_NODE_COUNT && i < ARENA_NODE_COUNT; i++) {
                    ArenaNode *dst = &arena_state.nodes[i];
                    dst->x = msg->nodes[i].x;
                    dst->z = msg->nodes[i].z;
                    dst->owner = msg->nodes[i].owner;
                    dst->capturing_team = msg->nodes[i].capturing_team;
                    dst->capture_progress_ms = msg->nodes[i].capture_progress_ms;
                }
                /* S170-136: projectiles are sparse (only some slots active),
                   so mirror the wire message's own "active count" directly
                   rather than reusing arena_state.projectiles[]' own active
                   flags -- the render loop below just walks 0..count. */
                {
                    int pcount = msg->projectile_count;
                    if (pcount > ARENA_SNAPSHOT_MAX_PROJECTILES) pcount = ARENA_SNAPSHOT_MAX_PROJECTILES;
                    if (pcount > ARENA_MAX_PROJECTILES) pcount = ARENA_MAX_PROJECTILES;
                    for (int i = 0; i < pcount; i++) {
                        ArenaProjectile *dst = &arena_state.projectiles[i];
                        dst->active = 1;
                        dst->x = msg->projectiles[i].x;
                        dst->z = msg->projectiles[i].z;
                        dst->owner = msg->projectiles[i].owner;
                        dst->hero_id = (ArenaHeroID)msg->projectiles[i].hero_id;
                    }
                    for (int i = pcount; i < ARENA_MAX_PROJECTILES; i++) {
                        arena_state.projectiles[i].active = 0;
                    }
                }
            }
        }
        len = recvfrom(net_sock, rbuf, sizeof(rbuf), 0, (struct sockaddr *)&sender, &slen);
    }
    /* Pick retry (S170-99, real bug found live: a genuinely full 20/20 lobby stalled in
     * ARENA_PHASE_DRAFT and died on the server's own 60s no-progress timeout). Root cause:
     * net_send_pick(), unlike net_connect()/net_find_and_connect(), was a single fire-and-
     * forget UDP send with no retry at all -- rock-solid over localhost loopback (bots, which
     * is all this path was ever tested against), but a real external connection can drop that
     * one unacknowledged packet, and net_picked latching to 1 on send (not on confirmation)
     * meant it would never be resent. Resend every 1s while still in draft and not yet live --
     * harmless if the original arrived (server's own PACKET_ARENA_PICK handling just re-records
     * the same hero_id), the actual fix if it didn't. */
    if (net_phase == ARENA_PHASE_DRAFT && net_picked && now_ms - net_last_pick_send_ms > 1000) {
        int hero_id = (my_owner + net_draft_offset) % ARENA_HERO_COUNT; /* same offset as the original pick -- resend must match, not re-roll */
        net_send_pick(hero_id);
        net_last_pick_send_ms = now_ms;
    }
}
/* end of the S170-54 cross-platform networking section */

/* Match event log — MOBA half of NORTHSTAR §12 Phase B (EMILY/BACKLOG.md
 * S170-29), extending apps/server's S170-28 pattern to this demo. Same
 * "minimum hook, not a replay system" philosophy: one JSON line per event
 * to var/matches/arena-<timestamp>.jsonl. Unlike apps/server, this client
 * has no networking or connect-ticket auth at all, so there's no real WOTAN
 * player_id to attach -- "local_player"/"local_bot" are honest placeholders,
 * not a guess at an identity that doesn't exist yet. Real identity
 * attribution for arena replays is blocked on arena getting connect-ticket
 * auth in the first place, which is out of scope here. */
static FILE *arena_log_fp = NULL;
static uint32_t arena_log_elapsed_ms = 0;
static uint32_t arena_log_since_snapshot_ms = 0;
#define ARENA_LOG_SNAPSHOT_INTERVAL_MS 500

static void arena_log_open(void) {
#ifdef _WIN32
    mkdir("var");
    mkdir("var/matches");
#else
    mkdir("var", 0755);
    mkdir("var/matches", 0755);
#endif
    char path[256];
    snprintf(path, sizeof(path), "var/matches/arena-%ld.jsonl", (long)time(NULL));
    if (arena_log_fp) fclose(arena_log_fp);
    arena_log_fp = fopen(path, "a");
    if (!arena_log_fp) {
        fprintf(stderr, "WARNING: could not open arena match log %s -- match will not be logged (S170-29)\n", path);
        return;
    }
    arena_log_elapsed_ms = 0;
    arena_log_since_snapshot_ms = 0;
    fprintf(arena_log_fp, "{\"event\":\"match_start\",\"ts_ms\":0}\n");
    fflush(arena_log_fp);
    printf("Arena match event log: %s\n", path);
}

static void arena_log_snapshot(void) {
    if (!arena_log_fp) return;
    ArenaHero *p = &arena_state.heroes[0];
    ArenaHero *b = &arena_state.heroes[1];
    fprintf(arena_log_fp,
            "{\"event\":\"snapshot\",\"ts_ms\":%u,"
            "\"player\":{\"id\":\"local_player\",\"x\":%.2f,\"z\":%.2f,\"hp\":%d},"
            "\"bot\":{\"id\":\"local_bot\",\"x\":%.2f,\"z\":%.2f,\"hp\":%d}}\n",
            arena_log_elapsed_ms, p->x, p->z, p->hp, b->x, b->z, b->hp);
    fflush(arena_log_fp);
}

static void arena_log_ability(const char *ability) {
    if (!arena_log_fp) return;
    fprintf(arena_log_fp, "{\"event\":\"ability_cast\",\"player_id\":\"local_player\",\"ability\":\"%s\",\"ts_ms\":%u}\n",
            ability, arena_log_elapsed_ms);
    fflush(arena_log_fp);
}

static void arena_log_win(int winner) {
    if (!arena_log_fp) return;
    const char *winner_id = (winner == 1) ? "local_player" : "local_bot";
    fprintf(arena_log_fp, "{\"event\":\"match_end\",\"winner\":\"%s\",\"ts_ms\":%u}\n", winner_id, arena_log_elapsed_ms);
    fflush(arena_log_fp);
}

/* ---------------- manually-loaded GL 3.x function pointers ---------------- */
static PFNGLCREATESHADERPROC glCreateShader_;
static PFNGLSHADERSOURCEPROC glShaderSource_;
static PFNGLCOMPILESHADERPROC glCompileShader_;
static PFNGLGETSHADERIVPROC glGetShaderiv_;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_;
static PFNGLCREATEPROGRAMPROC glCreateProgram_;
static PFNGLATTACHSHADERPROC glAttachShader_;
static PFNGLLINKPROGRAMPROC glLinkProgram_;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_;
static PFNGLUSEPROGRAMPROC glUseProgram_;
static PFNGLDELETESHADERPROC glDeleteShader_;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray_;
static PFNGLGENBUFFERSPROC glGenBuffers_;
static PFNGLBINDBUFFERPROC glBindBuffer_;
static PFNGLBUFFERDATAPROC glBufferData_;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_;
static PFNGLUNIFORM4FPROC glUniform4f_;
static PFNGLUNIFORM3FPROC glUniform3f_;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_;

#define LOAD(name, type) name##_ = (type)SDL_GL_GetProcAddress(#name)

static int load_gl_functions(void) {
    LOAD(glCreateShader, PFNGLCREATESHADERPROC);
    LOAD(glShaderSource, PFNGLSHADERSOURCEPROC);
    LOAD(glCompileShader, PFNGLCOMPILESHADERPROC);
    LOAD(glGetShaderiv, PFNGLGETSHADERIVPROC);
    LOAD(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
    LOAD(glCreateProgram, PFNGLCREATEPROGRAMPROC);
    LOAD(glAttachShader, PFNGLATTACHSHADERPROC);
    LOAD(glLinkProgram, PFNGLLINKPROGRAMPROC);
    LOAD(glGetProgramiv, PFNGLGETPROGRAMIVPROC);
    LOAD(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
    LOAD(glUseProgram, PFNGLUSEPROGRAMPROC);
    LOAD(glDeleteShader, PFNGLDELETESHADERPROC);
    LOAD(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    LOAD(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    LOAD(glGenBuffers, PFNGLGENBUFFERSPROC);
    LOAD(glBindBuffer, PFNGLBINDBUFFERPROC);
    LOAD(glBufferData, PFNGLBUFFERDATAPROC);
    LOAD(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    LOAD(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
    LOAD(glUniform4f, PFNGLUNIFORM4FPROC);
    LOAD(glUniform3f, PFNGLUNIFORM3FPROC);
    LOAD(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC);
    return glCreateShader_ && glShaderSource_ && glCompileShader_ && glLinkProgram_ &&
           glUseProgram_ && glGenVertexArrays_ && glBindVertexArray_ && glGenBuffers_ &&
           glBufferData_ && glVertexAttribPointer_ && glUniformMatrix4fv_;
}

/* ---------------- shader source ---------------- */
static const char *VS_SRC =
    "#version 150\n"
    "in vec3 aPos;\n"
    "in vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "out vec3 vNormal;\n"
    "void main() {\n"
    "    vNormal = mat3(uModel) * aNormal;\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "}\n";

static const char *FS_SRC =
    "#version 150\n"
    "in vec3 vNormal;\n"
    "uniform vec4 uColor;\n"
    "uniform vec3 uLightDir;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float diff = max(dot(normalize(vNormal), normalize(uLightDir)), 0.2);\n"
    "    fragColor = vec4(uColor.rgb * diff, uColor.a);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0;
    glGetShaderiv_(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog_(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error: %s\n", log);
    }
    return s;
}

static GLuint link_program(const char *vs_src, const char *fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram_();
    glAttachShader_(prog, vs);
    glAttachShader_(prog, fs);
    glBindAttribLocation_(prog, 0, "aPos");
    glBindAttribLocation_(prog, 1, "aNormal");
    glLinkProgram_(prog);
    GLint ok = 0;
    glGetProgramiv_(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog_(prog, sizeof(log), NULL, log);
        fprintf(stderr, "program link error: %s\n", log);
    }
    glDeleteShader_(vs);
    glDeleteShader_(fs);
    return prog;
}

/* ---------------- meshes ---------------- */
/* Unit cube, -0.5..0.5, position+normal interleaved, 36 verts. */
static const float CUBE_VERTS[] = {
    /* +X */  0.5f,-0.5f,-0.5f, 1,0,0,   0.5f, 0.5f,-0.5f, 1,0,0,   0.5f, 0.5f, 0.5f, 1,0,0,
              0.5f,-0.5f,-0.5f, 1,0,0,   0.5f, 0.5f, 0.5f, 1,0,0,   0.5f,-0.5f, 0.5f, 1,0,0,
    /* -X */ -0.5f,-0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f,-0.5f,-1,0,0,
             -0.5f,-0.5f, 0.5f,-1,0,0,  -0.5f, 0.5f,-0.5f,-1,0,0,  -0.5f,-0.5f,-0.5f,-1,0,0,
    /* +Y */ -0.5f, 0.5f,-0.5f, 0,1,0,  -0.5f, 0.5f, 0.5f, 0,1,0,   0.5f, 0.5f, 0.5f, 0,1,0,
             -0.5f, 0.5f,-0.5f, 0,1,0,   0.5f, 0.5f, 0.5f, 0,1,0,   0.5f, 0.5f,-0.5f, 0,1,0,
    /* -Y */ -0.5f,-0.5f, 0.5f, 0,-1,0, -0.5f,-0.5f,-0.5f, 0,-1,0,  0.5f,-0.5f,-0.5f, 0,-1,0,
             -0.5f,-0.5f, 0.5f, 0,-1,0,  0.5f,-0.5f,-0.5f, 0,-1,0,  0.5f,-0.5f, 0.5f, 0,-1,0,
    /* +Z */ -0.5f,-0.5f, 0.5f, 0,0,1,   0.5f,-0.5f, 0.5f, 0,0,1,   0.5f, 0.5f, 0.5f, 0,0,1,
             -0.5f,-0.5f, 0.5f, 0,0,1,   0.5f, 0.5f, 0.5f, 0,0,1,  -0.5f, 0.5f, 0.5f, 0,0,1,
    /* -Z */  0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f, 0.5f,-0.5f, 0,0,-1,
              0.5f,-0.5f,-0.5f, 0,0,-1, -0.5f, 0.5f,-0.5f, 0,0,-1,  0.5f, 0.5f,-0.5f, 0,0,-1,
};
#define CUBE_VERT_COUNT 36

/* Flat 1x1 ground quad in the XZ plane, normal up. */
static const float PLANE_VERTS[] = {
    -0.5f, 0, -0.5f, 0,1,0,   0.5f, 0, -0.5f, 0,1,0,   0.5f, 0, 0.5f, 0,1,0,
    -0.5f, 0, -0.5f, 0,1,0,   0.5f, 0,  0.5f, 0,1,0,  -0.5f, 0, 0.5f, 0,1,0,
};
#define PLANE_VERT_COUNT 6

#define RING_SEGMENTS 24
#define RING_VERT_COUNT (RING_SEGMENTS * 6)
static float RING_VERTS[RING_VERT_COUNT * 6]; /* filled at startup: pos.xyz + normal.xyz per vertex */

static void build_ring_mesh(float inner_r, float outer_r) {
    int vi = 0;
    for (int i = 0; i < RING_SEGMENTS; i++) {
        float a0 = (float)i / RING_SEGMENTS * 2.0f * (float)M_PI;
        float a1 = (float)(i + 1) / RING_SEGMENTS * 2.0f * (float)M_PI;
        float ix0 = cosf(a0) * inner_r, iz0 = sinf(a0) * inner_r;
        float ox0 = cosf(a0) * outer_r, oz0 = sinf(a0) * outer_r;
        float ix1 = cosf(a1) * inner_r, iz1 = sinf(a1) * inner_r;
        float ox1 = cosf(a1) * outer_r, oz1 = sinf(a1) * outer_r;
        float quad[6][3] = {
            {ix0, 0, iz0}, {ox0, 0, oz0}, {ox1, 0, oz1},
            {ix0, 0, iz0}, {ox1, 0, oz1}, {ix1, 0, iz1},
        };
        for (int v = 0; v < 6; v++) {
            RING_VERTS[vi++] = quad[v][0];
            RING_VERTS[vi++] = quad[v][1];
            RING_VERTS[vi++] = quad[v][2];
            RING_VERTS[vi++] = 0; RING_VERTS[vi++] = 1; RING_VERTS[vi++] = 0;
        }
    }
}

typedef struct { GLuint vao, vbo; int count; } Mesh;

static Mesh upload_mesh(const float *verts, int vert_count) {
    Mesh m; m.count = vert_count;
    glGenVertexArrays_(1, &m.vao);
    glBindVertexArray_(m.vao);
    glGenBuffers_(1, &m.vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, m.vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(float) * 6 * vert_count, verts, GL_STATIC_DRAW);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray_(1);
    glBindVertexArray_(0);
    return m;
}

static void draw_mesh(const Mesh *m) {
    glBindVertexArray_(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
    glBindVertexArray_(0);
}

/* one box of a hero model, in hero-local space (dx/dy/dz offset from the hero's
   footprint, sx/sy/sz box scale) -- dy is measured from the ground, not from the
   hero's own translate, since hero translate is already y=0.5 (see caller) */
/* squish (S170-128, "add charming squish animations" -> "for movement also spell casts"):
 * 1.0 = neutral. <1.0 = squashed (short and wide, feet still on the ground). >1.0 = stretched
 * (tall and thin). Applied uniformly to every box in a hero's silhouette so the whole model
 * squishes together, not one accent piece independently of the body. The Y scale AND Y offset
 * both get multiplied by squish (not just scale) so a squashed hero's boxes compress toward
 * the ground plane instead of scaling around each box's own center and clipping into the
 * floor or floating above it. X/Z get the inverse relationship (a squashed hero reads wider,
 * a stretched one reads thinner) for a cheap volume-preserving cartoon feel, not physically
 * exact but the "charming" part of squash-and-stretch was never about being exact. */
static void draw_hero_box(float hero_x, float hero_z, float dx, float dy, float dz,
                           float sx, float sy, float sz, float squish, const Mat4 *vp,
                           GLint loc_mvp, GLint loc_model, const Mesh *cube_mesh) {
    float squish_xz = 2.0f - squish;
    if (squish_xz < 0.4f) squish_xz = 0.4f;
    Mat4 t = mat4_translate(hero_x + dx * squish_xz, dy * squish, hero_z + dz * squish_xz);
    Mat4 s = mat4_scale(sx * squish_xz, sy * squish, sz * squish_xz);
    Mat4 model = mat4_multiply(&t, &s);
    Mat4 mvp = mat4_multiply(vp, &model);
    glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
    draw_mesh(cube_mesh);
}

/* S170-118 -- real per-hero silhouette instead of one generic cube for all 18.
   Every box shares the caller's relationship color (self/team/enemy, see the
   call site) so team/self legibility -- already solved by S170-89/96 -- is
   never overridden by per-hero identity; only SHAPE encodes which hero this is.
   Reuses the silhouette concepts already designed for the 7 SHANKPIT skins
   (apps/lobby/src/main.c draw_player_skin_*) where a hero overlaps one, expressed
   here as axis-aligned draw_mesh() boxes since this renderer has no mat4_rotate
   and SHANKPIT's immediate-mode glPushMatrix/glRotatef code can't port verbatim. */
static void draw_hero_model(ArenaHeroID hero_id, float hero_x, float hero_z, float squish, const Mat4 *vp,
                             GLint loc_mvp, GLint loc_model, const Mesh *cube_mesh) {
#define BOX(dx, dy, dz, sx, sy, sz) \
    draw_hero_box(hero_x, hero_z, dx, dy, dz, sx, sy, sz, squish, vp, loc_mvp, loc_model, cube_mesh)
    switch (hero_id) {
        case ARENA_HERO_UNICORN: /* SHANKPIT SKIN_UNICORN: body + tapered horn */
            BOX(0.0f, 0.55f, 0.0f, 0.85f, 1.1f, 0.85f);
            BOX(0.0f, 1.25f, 0.35f, 0.14f, 0.4f, 0.14f);
            break;
        case ARENA_HERO_DUCK: /* SHANKPIT SKIN_DUCK: squat wide body + forward bill */
            BOX(0.0f, 0.35f, 0.0f, 1.0f, 0.7f, 1.0f);
            BOX(0.0f, 0.35f, 0.55f, 0.3f, 0.16f, 0.35f);
            break;
        case ARENA_HERO_GHOST: /* SHANKPIT SKIN_GHOST: tall tapered legless body */
            BOX(0.0f, 0.8f, 0.0f, 0.55f, 1.6f, 0.55f);
            break;
        case ARENA_HERO_FROG: /* SHANKPIT SKIN_FROG: wide flat body + bulging eyes */
            BOX(0.0f, 0.3f, 0.0f, 1.1f, 0.55f, 1.05f);
            BOX(-0.25f, 0.68f, 0.3f, 0.2f, 0.2f, 0.2f);
            BOX(0.25f, 0.68f, 0.3f, 0.2f, 0.2f, 0.2f);
            break;
        case ARENA_HERO_DOC_WHEEL: /* wide flat base "wheel" + upright body */
            BOX(0.0f, 0.55f, 0.0f, 0.65f, 1.0f, 0.65f);
            BOX(0.0f, 0.12f, 0.0f, 1.15f, 0.16f, 1.15f);
            break;
        case ARENA_HERO_TREE: /* SHANKPIT SKIN_TREE: narrow trunk + wide canopy */
            BOX(0.0f, 0.5f, 0.0f, 0.4f, 1.6f, 0.4f);
            BOX(0.0f, 1.25f, 0.0f, 1.05f, 0.55f, 1.05f);
            break;
        case ARENA_HERO_PIZZA: /* SHANKPIT SKIN_PIZZA: flat wide wedge */
            BOX(0.0f, 0.18f, 0.0f, 1.3f, 0.3f, 1.3f);
            break;
        case ARENA_HERO_FLAMEL: /* alchemist -- body + a small flame-accent box */
            BOX(0.0f, 0.6f, 0.0f, 0.8f, 1.2f, 0.8f);
            BOX(0.3f, 1.35f, 0.0f, 0.2f, 0.3f, 0.2f);
            break;
        case ARENA_HERO_MORRIGAN: /* raven-goddess -- body + two side wing slabs */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            BOX(-0.55f, 0.9f, 0.0f, 0.35f, 0.55f, 0.15f);
            BOX(0.55f, 0.9f, 0.0f, 0.35f, 0.55f, 0.15f);
            break;
        case ARENA_HERO_DAGDA: /* bruiser king -- one big bulky box */
            BOX(0.0f, 0.65f, 0.0f, 1.2f, 1.3f, 1.2f);
            break;
        case ARENA_HERO_COURIER: /* Ratatoskr -- thin tall messenger + tail-flick accent */
            BOX(0.0f, 0.7f, 0.0f, 0.65f, 1.4f, 0.65f);
            BOX(0.0f, 1.1f, -0.45f, 0.18f, 0.5f, 0.18f);
            break;
        case ARENA_HERO_LOKI: /* duality -- main body + a smaller offset "double" */
            BOX(0.0f, 0.6f, 0.0f, 0.8f, 1.2f, 0.8f);
            BOX(0.5f, 0.4f, 0.35f, 0.4f, 0.8f, 0.4f);
            break;
        case ARENA_HERO_GARY: /* off-duty security, marksman -- boxy body + a long rifle/scope
                                  bar held out to the side, not a chest-mounted slab (S170-131:
                                  was near-identical to Abraham's grimoire silhouette) */
            BOX(0.0f, 0.65f, 0.0f, 0.8f, 1.3f, 0.8f);
            BOX(0.55f, 0.55f, 0.15f, 0.55f, 0.08f, 0.08f);
            break;
        case ARENA_HERO_FLUTE_DEBT: /* thin tall body + horizontal flute accent */
            BOX(0.0f, 0.7f, 0.0f, 0.65f, 1.4f, 0.65f);
            BOX(0.45f, 0.95f, 0.0f, 0.55f, 0.1f, 0.1f);
            break;
        case ARENA_HERO_BACON_PUCK: /* two merged heroes -- two half-width bodies side by side */
            BOX(-0.32f, 0.6f, 0.0f, 0.55f, 1.2f, 0.75f);
            BOX(0.32f, 0.5f, 0.0f, 0.55f, 1.0f, 0.75f);
            break;
        case ARENA_HERO_ABRAHAM: /* mage -- body + a flat "grimoire" accent + a small floating
                                     arcane orb above it (S170-131: the book alone read almost
                                     identically to Gary's old clipboard slab at the same spot) */
            BOX(0.0f, 0.65f, 0.0f, 0.8f, 1.3f, 0.8f);
            BOX(0.0f, 0.65f, 0.45f, 0.3f, 0.4f, 0.08f);
            BOX(0.0f, 1.05f, 0.4f, 0.14f, 0.14f, 0.14f);
            break;
        case ARENA_HERO_ADA: /* mech pilot -- boxy, oversized mech-like frame */
            BOX(0.0f, 0.7f, 0.0f, 1.0f, 1.4f, 1.0f);
            BOX(0.0f, 1.55f, 0.0f, 0.4f, 0.3f, 0.4f);
            break;
        case ARENA_HERO_TYLER: /* deliberately unremarkable plain humanoid, per character */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            break;
        case ARENA_HERO_PAIMON: /* Court Voice -- robed commander body + a raised scepter accent */
            BOX(0.0f, 0.65f, 0.0f, 0.85f, 1.3f, 0.85f);
            BOX(0.35f, 1.3f, 0.0f, 0.12f, 0.5f, 0.12f);
            break;
        case ARENA_HERO_NOOR1: /* the snowman form (S170-104) -- three stacked boxes, decreasing size */
            BOX(0.0f, 0.40f, 0.0f, 0.55f, 0.40f, 0.55f);
            BOX(0.0f, 0.95f, 0.0f, 0.40f, 0.35f, 0.40f);
            BOX(0.0f, 1.40f, 0.0f, 0.28f, 0.28f, 0.28f);
            break;
        case ARENA_HERO_CAIN: /* weathered wanderer body + the mark itself, front and center on
                                  the forehead -- Genesis's own imagery, not an incidental
                                  shoulder detail (S170-105, enlarged+repositioned S170-131: at
                                  the old size/spot it was nearly lost against Tyler's
                                  deliberately bare identical-base body) */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            BOX(0.0f, 1.32f, 0.36f, 0.22f, 0.2f, 0.06f);
            break;
        case ARENA_HERO_GUNNR: /* shieldmaiden -- body + a flat shield accent (S170-93) */
            BOX(0.0f, 0.65f, 0.0f, 0.75f, 1.3f, 0.75f);
            BOX(-0.65f, 0.65f, 0.0f, 0.10f, 0.55f, 0.45f);
            break;
        case ARENA_HERO_VASSAGO: /* soft foresight -- slender cloaked body + a small floating orb (S170-93) */
            BOX(0.0f, 0.60f, 0.0f, 0.55f, 1.2f, 0.55f);
            BOX(0.0f, 1.55f, 0.0f, 0.16f, 0.16f, 0.16f);
            break;
        case ARENA_HERO_HE_XIANGU: /* immortal ascetic -- slender robed body + a small crescent accent (S170-93) */
            BOX(0.0f, 0.62f, 0.0f, 0.5f, 1.25f, 0.5f);
            BOX(0.0f, 1.5f, 0.35f, 0.2f, 0.06f, 0.06f);
            break;
        case ARENA_HERO_BELETH: /* the Detonation -- body + three angled shard accents radiating
                                    outward, a burst pattern no other silhouette on the roster
                                    uses (S170-93) */
            BOX(0.0f, 0.6f, 0.0f, 0.7f, 1.25f, 0.7f);
            BOX(0.4f, 0.9f, 0.25f, 0.12f, 0.12f, 0.35f);
            BOX(-0.4f, 0.9f, 0.25f, 0.12f, 0.12f, 0.35f);
            BOX(0.0f, 1.15f, -0.35f, 0.12f, 0.12f, 0.35f);
            break;
        case ARENA_HERO_MNM: /* the Shapeshifting Crab -- wide low shell + two forward claw
                                 accents, low center of gravity distinct from every other
                                 silhouette on the roster (S170-134) */
            BOX(0.0f, 0.35f, 0.0f, 1.15f, 0.6f, 1.0f);
            BOX(0.75f, 0.35f, 0.35f, 0.25f, 0.25f, 0.4f);
            BOX(-0.75f, 0.35f, 0.35f, 0.25f, 0.25f, 0.4f);
            break;
        default:
            BOX(0.0f, 0.5f, 0.0f, 0.9f, 1.0f, 0.9f);
            break;
    }
#undef BOX
}

/* ---------------- tiny immediate-mode HUD text (ported from apps/lobby) ---------------- */
static void draw_char(char c, float x, float y, float s) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); /* fold lowercase -- one glyph set, not two */
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    if (c >= '0' && c <= '9') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
    } else if (c == 'W') {
        glVertex2f(x, y + s); glVertex2f(x + s * 0.25f, y);
        glVertex2f(x + s * 0.25f, y); glVertex2f(x + s * 0.5f, y + s * 0.6f);
        glVertex2f(x + s * 0.5f, y + s * 0.6f); glVertex2f(x + s * 0.75f, y);
        glVertex2f(x + s * 0.75f, y); glVertex2f(x + s, y + s);
    } else if (c == 'I') {
        glVertex2f(x + s / 2, y); glVertex2f(x + s / 2, y + s);
    } else if (c == 'N') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
    } else if (c == 'L') {
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
    } else if (c == 'O') {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    } else if (c == 'S') {
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y + s / 2);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x, y);
    } else if (c == 'E') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.8f, y + s / 2);
    } else if (c == 'U') {
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
    } else if (c == 'Y') {
        glVertex2f(x, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s / 2, y + s / 2); glVertex2f(x + s / 2, y);
    } else if (c == 'H') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
    } else if (c == 'P') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x, y + s / 2);
    } else if (c == ' ') {
    /* The rest of the alphabet + a handful of punctuation marks (S170's font-glyph gap, found
       live: tonight's new hero names -- Gary, Bacon+Puck, Abraham, Ada -- use letters this font
       never covered, falling through to the generic missing-glyph box below for most of their
       own names). Same simple GL_LINES stroke style as the letters above, not a real font. */
    } else if (c == 'A') {
        glVertex2f(x, y); glVertex2f(x + s / 2, y + s);
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s, y);
        glVertex2f(x + s * 0.25f, y + s * 0.4f); glVertex2f(x + s * 0.75f, y + s * 0.4f);
    } else if (c == 'B') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s * 0.7f, y + s);
        glVertex2f(x + s * 0.7f, y + s); glVertex2f(x + s * 0.7f, y + s / 2);
        glVertex2f(x + s * 0.7f, y + s / 2); glVertex2f(x, y + s / 2);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.7f, y + s / 2);
        glVertex2f(x + s * 0.7f, y + s / 2); glVertex2f(x + s * 0.7f, y);
        glVertex2f(x + s * 0.7f, y); glVertex2f(x, y);
    } else if (c == 'C') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
    } else if (c == 'D') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s * 0.6f, y + s);
        glVertex2f(x + s * 0.6f, y + s); glVertex2f(x + s, y + s * 0.7f);
        glVertex2f(x + s, y + s * 0.7f); glVertex2f(x + s, y + s * 0.3f);
        glVertex2f(x + s, y + s * 0.3f); glVertex2f(x + s * 0.6f, y);
        glVertex2f(x + s * 0.6f, y); glVertex2f(x, y);
    } else if (c == 'F') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s * 0.8f, y + s / 2);
    } else if (c == 'G') {
        glVertex2f(x + s, y); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s * 0.5f);
        glVertex2f(x + s * 0.5f, y + s * 0.5f); glVertex2f(x + s, y + s * 0.5f);
    } else if (c == 'J') {
        glVertex2f(x + s * 0.7f, y + s); glVertex2f(x + s * 0.7f, y + s * 0.2f);
        glVertex2f(x + s * 0.7f, y + s * 0.2f); glVertex2f(x + s * 0.3f, y);
        glVertex2f(x + s * 0.3f, y); glVertex2f(x, y + s * 0.2f);
    } else if (c == 'K') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y);
    } else if (c == 'M') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s / 2, y + s / 2);
        glVertex2f(x + s / 2, y + s / 2); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y);
    } else if (c == 'Q') {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
        glVertex2f(x + s * 0.55f, y + s * 0.35f); glVertex2f(x + s, y);
    } else if (c == 'R') {
        glVertex2f(x, y); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s, y + s / 2); glVertex2f(x, y + s / 2);
        glVertex2f(x + s / 2, y + s / 2); glVertex2f(x + s, y);
    } else if (c == 'T') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s / 2, y);
    } else if (c == 'V') {
        glVertex2f(x, y + s); glVertex2f(x + s / 2, y);
        glVertex2f(x + s / 2, y); glVertex2f(x + s, y + s);
    } else if (c == 'X') {
        glVertex2f(x, y); glVertex2f(x + s, y + s);
        glVertex2f(x, y + s); glVertex2f(x + s, y);
    } else if (c == 'Z') {
        glVertex2f(x, y + s); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y);
        glVertex2f(x, y); glVertex2f(x + s, y);
    } else if (c == '-') {
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
    } else if (c == '+') {
        glVertex2f(x, y + s / 2); glVertex2f(x + s, y + s / 2);
        glVertex2f(x + s / 2, y); glVertex2f(x + s / 2, y + s);
    } else if (c == '\'' || c == '"') {
        glVertex2f(x + s * 0.5f, y + s * 0.75f); glVertex2f(x + s * 0.5f, y + s);
    } else if (c == '.') {
        glVertex2f(x + s * 0.4f, y); glVertex2f(x + s * 0.6f, y);
    } else if (c == ',') {
        glVertex2f(x + s * 0.5f, y); glVertex2f(x + s * 0.3f, y - s * 0.25f);
    } else if (c == ':') {
        glVertex2f(x + s * 0.4f, y + s * 0.7f); glVertex2f(x + s * 0.6f, y + s * 0.7f);
        glVertex2f(x + s * 0.4f, y + s * 0.25f); glVertex2f(x + s * 0.6f, y + s * 0.25f);
    } else if (c == '!') {
        glVertex2f(x + s / 2, y + s); glVertex2f(x + s / 2, y + s * 0.3f);
        glVertex2f(x + s * 0.4f, y); glVertex2f(x + s * 0.6f, y);
    } else if (c == '(') {
        glVertex2f(x + s * 0.7f, y + s); glVertex2f(x + s * 0.3f, y + s * 0.5f);
        glVertex2f(x + s * 0.3f, y + s * 0.5f); glVertex2f(x + s * 0.7f, y);
    } else if (c == ')') {
        glVertex2f(x + s * 0.3f, y + s); glVertex2f(x + s * 0.7f, y + s * 0.5f);
        glVertex2f(x + s * 0.7f, y + s * 0.5f); glVertex2f(x + s * 0.3f, y);
    } else {
        glVertex2f(x, y); glVertex2f(x + s, y);
        glVertex2f(x + s, y); glVertex2f(x + s, y + s);
        glVertex2f(x + s, y + s); glVertex2f(x, y + s);
        glVertex2f(x, y + s); glVertex2f(x, y);
    }
    glEnd();
}

static void draw_string(const char *str, float x, float y, float size) {
    while (*str) {
        draw_char(*str, x, y, size);
        x += size * 1.2f;
        str++;
    }
}

/* hero_status_label (S170-133, founder: "text label above health bar above hero shows status
 * effects like stun silence root slow etc"): composes a short space-separated tag string from
 * whichever generic status-effect fields are currently active on this hero. "Stun" and "slow"
 * aren't modeled as their own generic fields yet (only silenced_ms/rooted_ms/intangible_ms/
 * burning_ms/survive_floor_ms exist on ArenaHero today) -- this surfaces what the sim actually
 * tracks rather than inventing new effect types as a UI-only side effect; adding a real stun/slow
 * mechanic is separate kit work, not a HUD task. Returns 1 if buf has anything to draw. */
static int hero_status_label(const ArenaHero *h, char *buf, size_t bufsize) {
    buf[0] = '\0';
    size_t used = 0;
#define APPEND_TAG(tag) do { \
        int n = snprintf(buf + used, bufsize - used, "%s%s", used > 0 ? " " : "", tag); \
        if (n > 0 && (size_t)n < bufsize - used) used += (size_t)n; \
    } while (0)
    if (h->silenced_ms > 0) APPEND_TAG("SILENCED");
    if (h->rooted_ms > 0) APPEND_TAG("ROOTED");
    if (h->intangible_ms > 0) APPEND_TAG("INTANGIBLE");
    if (h->burning_ms > 0) APPEND_TAG("BURNING");
    if (h->survive_floor_ms > 0) APPEND_TAG("UNKILLABLE");
#undef APPEND_TAG
    return used > 0;
}

/* draw_queuing_screen (S170-115, real bug found live): net_find_and_connect()/net_connect() both
 * block the whole event loop for up to 60s -- with no frame rendered during that whole wait, the
 * window shows whatever was on screen before the click and never updates, which is genuinely
 * indistinguishable from a hang. The matchmaker log confirmed it: 13+ distinct source ports from
 * the same external IP in a few minutes, consistent with the founder force-quitting an apparently
 * frozen window and relaunching, over and over, each relaunch a fresh queue attempt that
 * abandoned the previous one mid-match. This renders one real "please wait" frame and presents
 * it (SDL_GL_SwapWindow) *before* the blocking call starts, so the last thing on screen is an
 * honest status, not a stale frame. Doesn't make the wait non-blocking -- that's a bigger
 * rearchitecture -- but makes the wait visibly a wait, not a crash. */
static void draw_queuing_screen(SDL_Window *win, int win_w, int win_h) {
    glClearColor(0.03f, 0.05f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, 0, win_h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(0.6f, 1.0f, 0.7f);
    draw_string("QUEUING FOR MATCH", win_w / 2.0f - 190, win_h / 2.0f + 20, 20);
    glColor3f(0.7f, 0.8f, 0.75f);
    draw_string("PLEASE WAIT - THIS CAN TAKE UP TO 60 SECONDS", win_w / 2.0f - 300, win_h / 2.0f - 20, 12);
    draw_string("THE WINDOW WILL NOT RESPOND UNTIL A MATCH IS FOUND", win_w / 2.0f - 330, win_h / 2.0f - 44, 12);
    SDL_GL_SwapWindow(win);
}

/* ---------------- placement rings ---------------- */
#define MAX_RINGS 6
#define RING_LIFETIME_MS 500.0f
typedef struct { float x, z, age_ms; int active; } Ring;
static Ring rings[MAX_RINGS];

static void spawn_ring(float x, float z) {
    for (int i = 0; i < MAX_RINGS; i++) {
        if (!rings[i].active) {
            rings[i].active = 1;
            rings[i].x = x;
            rings[i].z = z;
            rings[i].age_ms = 0;
            return;
        }
    }
}

/* ---------------- attack flashes (S170-122, "add basic animations for auto
 * attacks") ---------------- */
/* Neither the wire snapshot (ArenaHeroSnapshot, deliberately minimal --
 * position/HP/alive/hero_id only) nor the local sim's per-hero state expose
 * a clean "an auto-attack just landed" signal that's available uniformly in
 * every render mode (local demo, net_mode, and replay/observe). What IS
 * available everywhere is HP itself -- so a frame-to-frame HP decrease on
 * any hero is treated as "something hit them" and gets a brief flash at
 * their position. This also catches ability damage, not just melee autos,
 * but for a first basic pass that's an honest, correctly-scoped simplification
 * rather than a wire-protocol change to carry real attack events. */
#define MAX_ATTACK_FLASHES ARENA_MAX_HEROES
#define ATTACK_FLASH_LIFETIME_MS 180.0f
typedef struct { float x, z, age_ms; int active; } AttackFlash;
static AttackFlash attack_flashes[MAX_ATTACK_FLASHES];
static int prev_hero_hp[ARENA_MAX_HEROES];
static int prev_hero_hp_valid[ARENA_MAX_HEROES];

static void spawn_attack_flash(float x, float z) {
    for (int i = 0; i < MAX_ATTACK_FLASHES; i++) {
        if (!attack_flashes[i].active) {
            attack_flashes[i].active = 1;
            attack_flashes[i].x = x;
            attack_flashes[i].z = z;
            attack_flashes[i].age_ms = 0;
            return;
        }
    }
}

/* ---------------- squish (S170-128, "add charming squish animations" ->
 * "for movement also spell casts") ---------------- */
/* One timer per hero slot, not a pooled particle array like the flashes above --
 * squish is a continuous property of the hero's own model, not a spawned object
 * at a fixed world position, so it's simplest to key it directly by owner index.
 * A large/negative age means "not currently animating," read by compute_squish
 * as neutral (1.0, no visual change at all) without needing a separate active flag. */
#define SQUISH_ANIM_MS 260.0f
static float squish_age_ms[ARENA_MAX_HEROES];
static int prev_hero_moving[ARENA_MAX_HEROES];
static int prev_hero_moving_valid[ARENA_MAX_HEROES];

static void trigger_squish(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return;
    squish_age_ms[owner] = 0.0f;
}

/* compute_squish: a decaying cosine -- starts squashed (short, wide), bounces past
 * neutral into a slight stretch, settles back to 1.0. Classic squash-and-stretch
 * bounce-back, cheap to compute, no physics simulation needed for something this
 * short-lived and purely cosmetic. */
static float compute_squish(int owner) {
    if (owner < 0 || owner >= ARENA_MAX_HEROES) return 1.0f;
    float t = squish_age_ms[owner];
    if (t < 0.0f || t >= SQUISH_ANIM_MS) return 1.0f;
    float amplitude = 0.32f;
    float decay = expf(-t / (SQUISH_ANIM_MS * 0.35f));
    float wobble = cosf(t / SQUISH_ANIM_MS * 3.14159265f * 2.4f);
    return 1.0f - amplitude * decay * wobble;
}

/* ---------------- spell flashes (S170-124, "add particle effects to
 * spells") ---------------- */
/* Unlike auto-attacks (S170-122, HP-delta is a decent-enough proxy), a real
 * "a spell was just cast" signal doesn't exist in HP alone -- several kits
 * have no damage component on some slots (Frog's Q rewinds position/HP with
 * no damage at all; Unicorn's W is a pure toggle). Carried over the wire for
 * real instead: ArenaHeroSnapshot.cast_flash_slot (0/1/2/3 = none/Q/W/R),
 * a one-tick signal the server sets the instant a cast clears its gate and
 * clears again right after broadcasting it. Slot gets its own color/size so
 * Q/W/R read as visually distinct tiers, same convention as any real MOBA
 * (bigger, brighter effect for the ultimate). */
#define MAX_SPELL_FLASHES (ARENA_MAX_HEROES * 2)
#define SPELL_FLASH_LIFETIME_MS 260.0f
typedef struct { float x, z, age_ms; int slot; int active; } SpellFlash;
static SpellFlash spell_flashes[MAX_SPELL_FLASHES];

static void spawn_spell_flash(float x, float z, int slot) {
    for (int i = 0; i < MAX_SPELL_FLASHES; i++) {
        if (!spell_flashes[i].active) {
            spell_flashes[i].active = 1;
            spell_flashes[i].x = x;
            spell_flashes[i].z = z;
            spell_flashes[i].slot = slot;
            spell_flashes[i].age_ms = 0;
            return;
        }
    }
}

/* ---------------- ability recast tiles (S170-127, "add the ability frame
 * cooldown timer tiles from shankpit og engine as recast time affordances"
 * -> "make it like overwatch recast frames for q w e") ---------------- */
/* Peak-cooldown tracking for the local player's own Q/W/E, one float each --
 * see the call site's own comment for why this exists (no per-hero max-
 * cooldown table to compute a wipe fraction against otherwise). */
static float q_cooldown_peak_ms = 0.0f;
static float w_cooldown_peak_ms = 0.0f;
static float r_cooldown_peak_ms = 0.0f;

/* draw_ability_tile: one Overwatch-style square ability icon -- bordered
 * tile, a radial dark wedge (GL_TRIANGLE_FAN from the tile's center)
 * sweeping clockwise from 12 o'clock that shrinks as cooldown counts down
 * (SHANKPIT's draw_ability_one_tile() only ever showed a flat color swap +
 * a number, no progress wipe -- REDGARDEN's 19-hero, 3-slot roster spans
 * cooldowns from ~2s to 26s+, where "how much is left" matters more than
 * SHANKPIT's single fixed-cooldown blade dash), a big centered countdown
 * number while on cooldown, and a keybind label below. `active` lights the
 * tile a bright toggle-green regardless of cooldown state, matching the
 * existing "W is ON" HUD convention this replaces. `peak_ms` is the
 * caller's own persistent float -- updated here, not reset by this
 * function, so it survives across frames.
 *
 * S170-137: `mana_blocked` (mp below this slot's flat cost) is a second,
 * independent way a ready-looking (cooldown_ms == 0) ability can still be
 * uncastable -- the mana layer (S170-132) already lets a cast whiff for
 * lack of mp with the cooldown untouched, so a tile that only ever read
 * cooldown_ms would keep telling the player an ability is ready right up
 * until they try it and nothing happens. Shares the same dimmed
 * background/border treatment as on_cooldown (one "not actually castable"
 * visual language), but skips the radial wipe and countdown number --
 * there's no fixed timer to animate, just "wait for regen" -- printing
 * "MP" in their place instead so the reason reads differently from a real
 * cooldown. */
static void draw_ability_tile(float x, float y, float size, int cooldown_ms, float *peak_ms,
                               int active, int mana_blocked, const char *keybind, const char *ability_name,
                               float base_r, float base_g, float base_b) {
    if (cooldown_ms > 0) {
        if ((float)cooldown_ms > *peak_ms) *peak_ms = (float)cooldown_ms;
    } else {
        *peak_ms = 0.0f;
    }
    int on_cooldown = cooldown_ms > 0;
    int not_ready = on_cooldown || mana_blocked;
    float frac_remaining = (on_cooldown && *peak_ms > 0.0f) ? (float)cooldown_ms / *peak_ms : 0.0f;
    if (frac_remaining > 1.0f) frac_remaining = 1.0f;

    float bg_r = active ? 0.15f : (not_ready ? 0.10f : 0.08f);
    float bg_g = active ? 0.45f : (not_ready ? 0.10f : 0.08f);
    float bg_b = active ? 0.20f : (not_ready ? 0.12f : 0.10f);
    glColor4f(bg_r, bg_g, bg_b, 0.85f);
    glRectf(x, y, x + size, y + size);

    /* Border: the ability's own base color at full brightness when ready
       or active, dimmed to near-gray while on cooldown or mana-blocked --
       same "ready pops, cooldown recedes" legibility Overwatch's own icon
       border uses. */
    float border_scale = (not_ready && !active) ? 0.35f : 1.0f;
    glColor4f(base_r * border_scale + (1.0f - border_scale) * 0.3f,
              base_g * border_scale + (1.0f - border_scale) * 0.3f,
              base_b * border_scale + (1.0f - border_scale) * 0.3f, 0.95f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x + size, y);
    glVertex2f(x + size, y + size); glVertex2f(x, y + size);
    glEnd();
    glLineWidth(1.0f);

    /* Radial cooldown wipe: a dark wedge from the tile's center, starting
       at 12 o'clock, sweeping clockwise for frac_remaining * 360 degrees --
       shrinks toward nothing as the ability approaches ready, exactly the
       "watch the pie empty" affordance real ability HUDs use. */
    if (on_cooldown && frac_remaining > 0.0f) {
        float cx = x + size / 2.0f, cy = y + size / 2.0f;
        float radius = size * 0.75f; /* overshoots the tile corners so the wedge always fully covers it */
        int segments = 24;
        int sweep_segments = (int)(segments * frac_remaining);
        if (sweep_segments < 1) sweep_segments = 1;
        glColor4f(0.0f, 0.0f, 0.0f, 0.72f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int s = 0; s <= sweep_segments; s++) {
            float t = (float)s / (float)segments;
            float angle = -3.14159265f / 2.0f + t * 2.0f * 3.14159265f; /* start at 12 o'clock, sweep clockwise */
            glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
        }
        glEnd();
    }

    if (on_cooldown) {
        char buf[8];
        int seconds = (int)ceilf((float)cooldown_ms / 1000.0f);
        if (seconds < 1) seconds = 1;
        snprintf(buf, sizeof(buf), "%d", seconds);
        float text_size = size * 0.05f;
        float approx_w = (float)strlen(buf) * text_size * 3.8f;
        glColor3f(1.0f, 0.95f, 0.95f);
        draw_string(buf, x + (size - approx_w) / 2.0f, y + size * 0.4f, text_size);
    } else if (mana_blocked) {
        float text_size = size * 0.05f;
        float approx_w = 2.0f * text_size * 3.8f; /* "MP" is always 2 chars */
        glColor3f(0.55f, 0.75f, 1.0f);
        draw_string("MP", x + (size - approx_w) / 2.0f, y + size * 0.4f, text_size);
    }

    glColor3f(0.92f, 0.96f, 1.0f);
    draw_string(keybind, x + size / 2.0f - 3.0f, y - 12.0f, 8.0f);
    glColor3f(0.75f, 0.8f, 0.85f);
    draw_string(ability_name, x, y - 24.0f, 6.0f);
}

/* ---------------- camera ---------------- */
static float cam_yaw = 45.0f, cam_pitch = 40.0f, cam_dist = 16.0f;

static void camera_basis(float focus_x, float focus_z,
                          float *eye_x, float *eye_y, float *eye_z,
                          float *fwd_x, float *fwd_y, float *fwd_z,
                          float *right_x, float *right_y, float *right_z,
                          float *up_x, float *up_y, float *up_z) {
    float yaw = cam_yaw * (float)M_PI / 180.0f;
    float pitch = cam_pitch * (float)M_PI / 180.0f;
    *eye_x = focus_x + cam_dist * cosf(pitch) * sinf(yaw);
    *eye_y = cam_dist * sinf(pitch);
    *eye_z = focus_z + cam_dist * cosf(pitch) * cosf(yaw);
    float fx = focus_x - *eye_x, fy = -*eye_y, fz = focus_z - *eye_z;
    float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    *fwd_x = fx / flen; *fwd_y = fy / flen; *fwd_z = fz / flen;
    float upx = 0, upy = 1, upz = 0;
    float rx = *fwd_y * upz - *fwd_z * upy;
    float ry = *fwd_z * upx - *fwd_x * upz;
    float rz = *fwd_x * upy - *fwd_y * upx;
    float rlen = sqrtf(rx * rx + ry * ry + rz * rz);
    *right_x = rx / rlen; *right_y = ry / rlen; *right_z = rz / rlen;
    *up_x = *right_y * *fwd_z - *right_z * *fwd_y;
    *up_y = *right_z * *fwd_x - *right_x * *fwd_z;
    *up_z = *right_x * *fwd_y - *right_y * *fwd_x;
}

/* Intersects the mouse ray with the y=0 ground plane. Returns 1 on hit. */
static int screen_to_ground(int mx, int my, int w, int h, float fov_deg,
                             float focus_x, float focus_z, float *out_x, float *out_z) {
    float eye_x, eye_y, eye_z, fx, fy, fz, rx, ry, rz, ux, uy, uz;
    camera_basis(focus_x, focus_z, &eye_x, &eye_y, &eye_z, &fx, &fy, &fz, &rx, &ry, &rz, &ux, &uy, &uz);
    float ndc_x = (2.0f * mx / w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / h);
    float aspect = (float)w / (float)h;
    float tan_fov = tanf(fov_deg * 0.5f * (float)M_PI / 180.0f);
    float dx = fx + ndc_x * tan_fov * aspect * rx + ndc_y * tan_fov * ux;
    float dy = fy + ndc_x * tan_fov * aspect * ry + ndc_y * tan_fov * uy;
    float dz = fz + ndc_x * tan_fov * aspect * rz + ndc_y * tan_fov * uz;
    if (fabsf(dy) < 1e-5f) return 0;
    float t = -eye_y / dy;
    if (t <= 0) return 0;
    *out_x = eye_x + t * dx;
    *out_z = eye_z + t * dz;
    return 1;
}

/* world_to_screen: inverse of screen_to_ground's job -- projects a 3D world point through
 * the same view-projection matrix the 3D pass draws with, into the 2D HUD's bottom-up pixel
 * space (S170-89, per-hero floating health bars). Mat4 is column-major (mat4.h's own
 * mat4_multiply indexes m[col*4+row]), so the manual point transform below follows the same
 * convention. Returns 0 if the point is behind the camera (w <= 0), meaningless to project. */
static int world_to_screen(const Mat4 *vp, float wx, float wy, float wz, int win_w, int win_h,
                            float *sx, float *sy) {
    float px[4] = {wx, wy, wz, 1.0f};
    float clip[4];
    for (int row = 0; row < 4; row++) {
        float sum = 0.0f;
        for (int col = 0; col < 4; col++) sum += vp->m[col * 4 + row] * px[col];
        clip[row] = sum;
    }
    if (clip[3] <= 0.01f) return 0;
    float ndc_x = clip[0] / clip[3];
    float ndc_y = clip[1] / clip[3];
    *sx = (ndc_x * 0.5f + 0.5f) * win_w;
    *sy = (ndc_y * 0.5f + 0.5f) * win_h;
    return 1;
}

/* ---------------- audio (S170-92, "add little musical sound effects... for
 * legibility via midi") ---------------- */
/* Real scope call, not guessed: raw SDL2 core audio (SDL_OpenAudioDevice +
 * SDL_QueueAudio), no SDL2_mixer. The backlog item's own open questions --
 * whether a new mixer dependency is acceptable, what the Windows-bundle
 * story is for a second DLL alongside SDL2.dll -- both dissolve if nothing
 * new gets linked at all: SDL2 core already has an audio subsystem, already
 * ships in every build (Linux and the mingw cross-compile alike), so short
 * procedurally-synthesized tones need zero new toolchain/CI/bundling work.
 * "Via midi" read as "short, distinct musical notes per event," not literal
 * .mid file playback -- a simple sine tone per cue is the honest match for
 * that intent at this scope ("little," per the founder's own word).
 * Graceful degradation: if no audio device is available (this box is
 * headless; a real player's box might also have no sound hardware, or it's
 * muted), audio_dev stays 0 and every play_tone() call is a silent no-op --
 * never a crash. */
static SDL_AudioDeviceID audio_dev = 0;

static void audio_init(void) {
    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "[arena client] no audio device available (%s) -- sound effects disabled\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(audio_dev, 0);
}

/* play_tone: synthesizes duration_ms of a sine wave at freq_hz and queues it
 * for immediate playback. Linear fade-out over the last ~15ms avoids the
 * audible click a hard-cut sine wave would otherwise produce. */
static void play_tone(float freq_hz, float duration_ms, float volume) {
    if (audio_dev == 0) return;
    int sample_rate = 44100;
    int n = (int)(sample_rate * duration_ms / 1000.0f);
    if (n <= 0) return;
    int16_t *buf = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    if (!buf) return;
    int fade_samples = sample_rate * 15 / 1000;
    if (fade_samples > n) fade_samples = n;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)sample_rate;
        float env = 1.0f;
        if (i > n - fade_samples) env = (float)(n - i) / (float)fade_samples;
        float sample = sinf(2.0f * 3.14159265f * freq_hz * t) * volume * env;
        buf[i] = (int16_t)(sample * 32000.0f);
    }
    SDL_QueueAudio(audio_dev, buf, (Uint32)n * sizeof(int16_t));
    free(buf);
}

/* play_cast_tone: one distinct note per ability slot -- an ascending triad
   (Q/W/R -> A4/C#5/E5), same "which slot just fired" legibility the spell
   flash's cyan/violet/gold color tiers already give visually, mirrored in
   sound so it reads even without looking at the cast location. */
static void play_cast_tone(int slot) {
    switch (slot) {
        case 1: play_tone(440.0f, 90.0f, 0.3f); break;  /* Q: A4 */
        case 2: play_tone(554.0f, 110.0f, 0.3f); break; /* W: C#5 */
        default: play_tone(659.0f, 140.0f, 0.32f); break; /* R: E5, longest and loudest -- the ultimate */
    }
}

int main(int argc, char *argv[]) {
    /* No srand() call existed anywhere in this file before -- mint_ticket_fallback's own
       rand()-based nonce (used only when IDUNA isn't reachable) was silently using the default
       seed=1 sequence, identical every single launch, a real if minor pre-existing weakness found
       while adding this call for an unrelated reason (net_draft_offset originally used rand()
       too, before being switched to a deterministic port-derived value instead -- see its own
       doc comment). Left in: real randomness for the ticket nonce is still worth having even
       though the offset itself no longer needs it. */
    srand((unsigned int)time(NULL));
    /* squish_age_ms[] zero-initializes with the rest of static storage, but 0.0f reads as
       "animation just started" (compute_squish's own neutral sentinel is anything >=
       SQUISH_ANIM_MS) -- without this every hero would appear squashed for one frame the instant
       the game launches, before any real trigger fires. Push every slot past the animation
       window so compute_squish() reads neutral (1.0f) until trigger_squish() actually resets it. */
    for (int squish_init_i = 0; squish_init_i < ARENA_MAX_HEROES; squish_init_i++) {
        squish_age_ms[squish_init_i] = SQUISH_ANIM_MS + 1.0f;
    }
    /* Observer mode (NORTHSTAR §12 Phase C, EMILY/BACKLOG.md S170-30):
     * `red_garden_arena --observe var/matches/arena-<ts>.jsonl` plays back
     * a logged match through this exact same renderer instead of driving
     * ArenaState from live input/bot AI -- "same draw code, no second
     * rendering path" per the founder's requirement. */
    ArenaReplay replay;
    int observing = 0;
    uint32_t observe_elapsed_ms = 0;
    const char *connect_host = NULL;
    int connect_port = 7200;
    const char *queue_host = NULL;
    int queue_port = 7778; /* apps/matchmaker's documented arena listen-port */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--observe") == 0 && i + 1 < argc) {
            if (!arena_replay_load(argv[i + 1], &replay)) {
                fprintf(stderr, "--observe: could not open %s\n", argv[i + 1]);
                return 1;
            }
            observing = 1;
            printf("OBSERVER MODE: replaying %s (%d snapshots)\n", argv[i + 1], replay.count);
        } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            /* Real networked PvP (NORTHSTAR §13): connect to a real
               apps/arena_server instead of running the local sim. */
            connect_host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            connect_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--queue") == 0 && i + 1 < argc) {
            /* Join whatever match the persistent bot pool is currently
               matchmaking into, instead of connecting to an already-known
               server (S170-44: "moba player can join bot pool games"). */
            queue_host = argv[++i];
        } else if (strcmp(argv[i], "--matchmaker-port") == 0 && i + 1 < argc) {
            queue_port = atoi(argv[++i]);
        }
    }
#ifdef _WIN32
    /* Sockets need WSAStartup before any socket() call on Windows -- only
       needed if this run actually uses the network (--connect/--queue),
       same "only pay for what you use" reasoning as everywhere else in
       this file. Harmless to call unconditionally, but scoped here to
       keep it next to what actually needs it. */
    if (connect_host || queue_host) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
#endif
    if (connect_host) {
        net_mode = 1;
        load_iduna_agent_config();
        if (!net_connect(connect_host, connect_port)) {
            fprintf(stderr, "Failed to connect to arena server at %s:%d\n", connect_host, connect_port);
            return 1;
        }
    } else if (queue_host) {
        net_mode = 1;
        load_iduna_agent_config();
        if (!net_find_and_connect(queue_host, queue_port)) {
            fprintf(stderr, "Failed to join a match via matchmaker at %s:%d\n", queue_host, queue_port);
            return 1;
        }
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    audio_init();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int win_w = 1280, win_h = 720;
    SDL_Window *win = SDL_CreateWindow(
        observing ? "KNIGHTS OF THE VOID — OBSERVER MODE" :
        (net_mode ? "KNIGHTS OF THE VOID (networked PvP)" : "KNIGHTS OF THE VOID (local)"),
        100, 100, win_w, win_h, SDL_WINDOW_OPENGL);
    if (!win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError()); return 1; }

    if (!load_gl_functions()) {
        fprintf(stderr, "Failed to load required GL 3.x functions via SDL_GL_GetProcAddress\n");
        return 1;
    }

    GLuint prog = link_program(VS_SRC, FS_SRC);
    GLint loc_mvp = glGetUniformLocation_(prog, "uMVP");
    GLint loc_model = glGetUniformLocation_(prog, "uModel");
    GLint loc_color = glGetUniformLocation_(prog, "uColor");
    GLint loc_light = glGetUniformLocation_(prog, "uLightDir");

    build_ring_mesh(0.8f, 1.0f);
    Mesh cube_mesh = upload_mesh(CUBE_VERTS, CUBE_VERT_COUNT);
    Mesh plane_mesh = upload_mesh(PLANE_VERTS, PLANE_VERT_COUNT);
    Mesh ring_mesh = upload_mesh(RING_VERTS, RING_VERT_COUNT);

    glEnable(GL_DEPTH_TEST);

    arena_init();
    /* In net_mode, apps/arena_server is authoritative and writes its own
       match log -- a local log here would be redundant and would wrongly
       claim "local_player"/"local_bot" identities for a real match. */
    if (!observing && !net_mode) arena_log_open();

    int dragging_cam = 0;
    int last_mx = 0, last_my = 0;
    int running = 1;
    int win_logged = 0;
    uint32_t last_tick = SDL_GetTicks();

    while (running) {
        uint32_t now = SDL_GetTicks();
        uint32_t dt = now - last_tick;
        last_tick = now;
        if (observing) {
            observe_elapsed_ms += dt;
        } else {
            arena_log_elapsed_ms += dt;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = e.window.data1; win_h = e.window.data2;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                dragging_cam = 1; last_mx = e.button.x; last_my = e.button.y;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                dragging_cam = 0;
            }
            if (e.type == SDL_MOUSEMOTION && dragging_cam) {
                int dx = e.motion.x - last_mx, dy = e.motion.y - last_my;
                last_mx = e.motion.x; last_my = e.motion.y;
                cam_yaw += dx * 0.3f;
                cam_pitch += dy * 0.3f;
                if (cam_pitch < 10.0f) cam_pitch = 10.0f;
                if (cam_pitch > 80.0f) cam_pitch = 80.0f;
            }
            if (e.type == SDL_MOUSEWHEEL) {
                cam_dist -= e.wheel.y * 1.0f;
                if (cam_dist < 4.0f) cam_dist = 4.0f;
                if (cam_dist > 30.0f) cam_dist = 30.0f;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                show_apm = !show_apm; /* S170-71: works in any mode, not gated on net_mode/observing */
            }
            /* Everything below drives a live match (movement clicks, kit
             * casts, restart-into-a-new-match) -- none of it applies while
             * observing a logged one. Camera control above still works, so
             * an observer can look around freely. */
            if (!observing && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                arena_state.winner == 0) {
                float gx, gz;
                float focus_x = arena_state.heroes[my_owner].x, focus_z = arena_state.heroes[my_owner].z;
                if (screen_to_ground(e.button.x, e.button.y, win_w, win_h, 60.0f,
                                     focus_x, focus_z, &gx, &gz)) {
                    if (net_mode) net_send_move(gx, gz);
                    else arena_set_move_target(my_owner, gx, gz);
                    spawn_ring(gx, gz);
                    apm_record_action(now);
                }
            }
            /* Requeue-after-win OK button (S170-66/68: "we need to requeue after
             * a game after an ok button"). Only meaningful in net_mode -- local
             * practice mode already has its own R-to-restart below. Click box
             * matches the one drawn under the YOU WIN/YOU LOSE text further down;
             * SDL mouse y is top-down, the ortho HUD draw space is bottom-up, so
             * flip before hit-testing against those same screen-space bounds. */
            if (net_mode && !observing && e.type == SDL_MOUSEBUTTONDOWN &&
                e.button.button == SDL_BUTTON_LEFT && arena_state.winner != 0) {
                float bx = e.button.x, by = win_h - e.button.y;
                float ok_left = win_w / 2.0f - 90, ok_right = win_w / 2.0f + 90;
                float ok_bottom = win_h / 2.0f - 70, ok_top = win_h / 2.0f - 30;
                if (bx >= ok_left && bx <= ok_right && by >= ok_bottom && by <= ok_top) {
                    printf("[arena client] requeuing for another match...\n");
                    fflush(stdout);
#ifdef _WIN32
                    if (net_sock >= 0) closesocket(net_sock);
#else
                    if (net_sock >= 0) close(net_sock);
#endif
                    net_sock = -1;
                    memset(&arena_state, 0, sizeof(arena_state));
                    memset(rings, 0, sizeof(rings));
                    win_logged = 0;
                    net_picked = 0;
                    net_phase = ARENA_PHASE_WAITING;
                    draw_queuing_screen(win, win_w, win_h);
                    int reconnected = queue_host ? net_find_and_connect(queue_host, queue_port)
                                                  : net_connect(connect_host, connect_port);
                    if (!reconnected) {
                        fprintf(stderr, "[arena client] requeue failed -- matchmaker/bot pool may be down\n");
                    } else {
                        printf("[arena client] requeue connected -- hero slot %d\n", my_owner);
                    }
                    fflush(stdout);
                }
            }
            if (!net_mode && e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r) {
                if (observing) {
                    observe_elapsed_ms = 0; /* restart playback from the beginning */
                    arena_state.winner = 0;
                } else {
                    arena_init();
                    memset(rings, 0, sizeof(rings));
                    arena_log_open(); /* fresh match -> fresh log file, S170-29 */
                    win_logged = 0;
                }
            }
            /* The Unicorn's kit (docs/HEROES_VS0.md) — the local player's own
             * hero (my_owner) only, S170-18. R is already bound to "restart
             * match" in local mode, so the ultimate goes on E. In net_mode,
             * casts are sent to the server, which owns cooldowns/effects. */
            if (!observing && e.type == SDL_KEYDOWN && arena_state.winner == 0) {
                if (e.key.keysym.sym == SDLK_q || e.key.keysym.sym == SDLK_w || e.key.keysym.sym == SDLK_e) {
                    apm_record_action(now);
                }
                if (net_mode) {
                    if (e.key.keysym.sym == SDLK_q) net_send_cast(0);
                    if (e.key.keysym.sym == SDLK_w) net_send_cast(1);
                    if (e.key.keysym.sym == SDLK_e) net_send_cast(2);
                } else {
                    if (e.key.keysym.sym == SDLK_q) { arena_cast_q(my_owner); arena_log_ability("Q"); }
                    if (e.key.keysym.sym == SDLK_w) { arena_toggle_w(my_owner); arena_log_ability("W"); }
                    if (e.key.keysym.sym == SDLK_e) { arena_cast_r(my_owner); arena_log_ability("R"); }
                }
            }
        }

        if (observing) {
            /* Drive the exact same ArenaState the live path draws from --
             * "same draw code, no second rendering path" (S170-30). */
            arena_replay_apply_at(&replay, observe_elapsed_ms, &arena_state);
        }
        else if (net_mode) {
            /* apps/arena_server is authoritative -- apply its snapshots
               rather than running arena_update() locally (that would
               double-simulate and diverge from the server's own state). */
            net_poll_snapshots(now);
        }
        else if (arena_state.winner == 0) {
            arena_update(dt);
            arena_log_since_snapshot_ms += dt;
            if (arena_log_since_snapshot_ms >= ARENA_LOG_SNAPSHOT_INTERVAL_MS) {
                arena_log_snapshot();
                arena_log_since_snapshot_ms = 0;
            }
        } else if (!win_logged && !net_mode) {
            arena_log_win(arena_state.winner);
            win_logged = 1;
        }
        for (int i = 0; i < MAX_RINGS; i++) {
            if (!rings[i].active) continue;
            rings[i].age_ms += dt;
            if (rings[i].age_ms >= RING_LIFETIME_MS) rings[i].active = 0;
        }
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->active || !h->alive) {
                prev_hero_hp_valid[i] = 0;
                prev_hero_moving_valid[i] = 0;
                continue;
            }
            /* Movement-start squish (S170-128, "for movement also spell casts"):
               same transition-detection idiom as the HP-delta check just below,
               fired once per departure, not every frame spent moving. */
            if (prev_hero_moving_valid[i] && !prev_hero_moving[i] && h->moving) {
                trigger_squish(i);
            }
            prev_hero_moving[i] = h->moving;
            prev_hero_moving_valid[i] = 1;
            if (prev_hero_hp_valid[i] && h->hp < prev_hero_hp[i]) {
                spawn_attack_flash(h->x, h->z);
                trigger_squish(i);
                float hdx = h->x - arena_state.heroes[my_owner].x;
                float hdz = h->z - arena_state.heroes[my_owner].z;
                if (hdx * hdx + hdz * hdz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                    play_tone(220.0f, 60.0f, 0.35f); /* short low thud, distinct from the higher/longer cast tones */
                }
            }
            prev_hero_hp[i] = h->hp;
            prev_hero_hp_valid[i] = 1;
        }
        for (int i = 0; i < MAX_ATTACK_FLASHES; i++) {
            if (!attack_flashes[i].active) continue;
            attack_flashes[i].age_ms += dt;
            if (attack_flashes[i].age_ms >= ATTACK_FLASH_LIFETIME_MS) attack_flashes[i].active = 0;
        }
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            if (squish_age_ms[i] >= 0.0f && squish_age_ms[i] < SQUISH_ANIM_MS) {
                squish_age_ms[i] += dt;
            }
        }
        /* Local-mode cast_flash_slot drain (S170-124): net_mode already spawns spell
           flashes directly off the wire snapshot inside net_poll_snapshots and never
           writes this field locally, so this loop is a no-op there -- it only ever
           fires for the local 1v1 demo, where arena_cast_q/toggle_w/cast_r are called
           directly (both the human's own key presses and the internal bot AI), with no
           server-side broadcast/clear step to do this job instead. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (h->cast_flash_slot > 0) {
                spawn_spell_flash(h->x, h->z, h->cast_flash_slot);
                trigger_squish(i);
                float sdx = h->x - arena_state.heroes[my_owner].x;
                float sdz = h->z - arena_state.heroes[my_owner].z;
                if (sdx * sdx + sdz * sdz <= ARENA_AUDIO_HEARING_RADIUS * ARENA_AUDIO_HEARING_RADIUS) {
                    play_cast_tone(h->cast_flash_slot);
                }
                h->cast_flash_slot = 0;
            }
        }
        for (int i = 0; i < MAX_SPELL_FLASHES; i++) {
            if (!spell_flashes[i].active) continue;
            spell_flashes[i].age_ms += dt;
            if (spell_flashes[i].age_ms >= SPELL_FLASH_LIFETIME_MS) spell_flashes[i].active = 0;
        }

        glViewport(0, 0, win_w, win_h);
        glClearColor(0.03f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float focus_x = arena_state.heroes[my_owner].x, focus_z = arena_state.heroes[my_owner].z;
        Mat4 view = mat4_orbit_view(focus_x, 0, focus_z, cam_yaw, cam_pitch, cam_dist);
        Mat4 proj = mat4_perspective(60.0f, (float)win_w / (float)win_h, 0.1f, 100.0f);
        Mat4 vp = mat4_multiply(&proj, &view);

        glUseProgram_(prog);
        glUniform3f_(loc_light, 0.4f, 0.8f, 0.3f);

        /* ground */
        {
            Mat4 model = mat4_scale(ARENA_HALF_EXTENT * 2.2f, 1.0f, ARENA_HALF_EXTENT * 2.2f);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.08f, 0.18f, 0.10f, 1.0f);
            draw_mesh(&plane_mesh);
        }

        /* nodes -- colored by owner (S170-87 cont'd, now that ownership
           actually reaches the client at all) so the capture redesign's
           whole point -- who controls this ground right now -- is visible,
           not just the node's existence/position. Same team-color
           convention as the hero cubes below (blue = team 0, red =
           team 1), gold for neutral/contested, matching the territory
           system's own owner encoding (0/1/2). */
        for (int i = 0; i < ARENA_NODE_COUNT; i++) {
            Mat4 t = mat4_translate(arena_state.nodes[i].x, 0.15f, arena_state.nodes[i].z);
            Mat4 s = mat4_scale(1.2f, 0.3f, 1.2f);
            Mat4 model = mat4_multiply(&t, &s);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            switch (arena_state.nodes[i].owner) {
                case 1: glUniform4f_(loc_color, 0.15f, 0.35f, 0.95f, 1.0f); break; /* team 0's */
                case 2: glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, 1.0f); break; /* team 1's */
                default: glUniform4f_(loc_color, 0.85f, 0.7f, 0.1f, 1.0f); break;  /* neutral/contested */
            }
            draw_mesh(&cube_mesh);
        }

        /* jungle obstacles (S170-138, "add rocks and trees so we naturally
           start to create some lanes"): boxes only, same "boxes for now"
           silhouette approach as the hero models below -- trunk+canopy for a
           tree (mirrors ARENA_HERO_TREE's own two-box shape), one squat box
           for a rock. Purely a draw of where the sim's own obstacles[] array
           already is (packages/simulation/arena_game.c's
           arena_obstacles_reset_layout) -- the collision that actually
           carves the map into lanes happens sim-side in
           resolve_hero_obstacle_collision, this is just rendering it. */
        for (int i = 0; i < ARENA_OBSTACLE_COUNT; i++) {
            const ArenaObstacle *o = &arena_state.obstacles[i];
            if (o->kind == ARENA_OBSTACLE_TREE) {
                glUniform4f_(loc_color, 0.32f, 0.22f, 0.12f, 1.0f); /* trunk: brown */
                draw_hero_box(o->x, o->z, 0.0f, o->radius * 0.7f, 0.0f,
                              o->radius * 0.35f, o->radius * 1.4f, o->radius * 0.35f,
                              1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
                glUniform4f_(loc_color, 0.15f, 0.45f, 0.18f, 1.0f); /* canopy: green */
                draw_hero_box(o->x, o->z, 0.0f, o->radius * 1.7f, 0.0f,
                              o->radius, o->radius * 0.9f, o->radius,
                              1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
            } else {
                glUniform4f_(loc_color, 0.45f, 0.44f, 0.42f, 1.0f); /* rock: grey */
                draw_hero_box(o->x, o->z, 0.0f, o->radius * 0.55f, 0.0f,
                              o->radius, o->radius * 0.55f, o->radius * 0.9f,
                              1.0f, &vp, loc_mvp, loc_model, &cube_mesh);
            }
        }

        /* heroes -- ARENA_MAX_HEROES so team-mode matches (up to 10v10)
           render every real hero; local/1v1 heroes[2..] are simply never
           alive, so this loop is a no-op regression risk for that mode. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->alive) continue;
            /* intangible_ms (Ghost's Not a Ghost, Frog's R vanish, Bacon Puck's Q, etc. --
               any kit that grants the shared can't-be-hit status) reads as the skinmodel
               going see-through for its duration, same "can't touch this" read a real MOBA
               gives untargetable heroes, on top of the INTANGIBLE text tag already above
               the health bar. Alpha blending needs GL_BLEND on and depth writes off for
               this hero's boxes only -- everyone else stays fully opaque with normal
               depth writes, same convention as the ring/flash effects below. */
            int is_intangible = h->intangible_ms > 0;
            float alpha = is_intangible ? 0.35f : 1.0f;
            if (i == my_owner) {
                glUniform4f_(loc_color, 0.1f, 0.8f, 0.95f, alpha); /* my hero: bright cyan */
            } else if (h->team == arena_state.heroes[my_owner].team) {
                glUniform4f_(loc_color, 0.15f, 0.35f, 0.95f, alpha); /* teammate: blue */
            } else {
                glUniform4f_(loc_color, 0.95f, 0.25f, 0.15f, alpha); /* enemy: red */
            }
            if (is_intangible) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            /* S170-118: per-hero_id silhouette (multi-box), not one generic cube --
               relationship color above still wins for self/team/enemy legibility. */
            draw_hero_model(h->hero_id, h->x, h->z, compute_squish(i), &vp, loc_mvp, loc_model, &cube_mesh);
            if (is_intangible) {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
        }

        /* projectiles (S170-136): the first travelling skill-shot in this
           arena. Small, bright, and shape-distinct from every hero
           silhouette on purpose -- this needs to read as "an incoming shot"
           at a glance, not blend into the hero-model system above. Same
           self/team/enemy color convention as heroes so a player can tell
           at a glance whether an in-flight shot is a threat (enemy, red)
           before it arrives -- the actual dodge affordance this ability was
           built for. */
        for (int i = 0; i < ARENA_MAX_PROJECTILES; i++) {
            ArenaProjectile *p = &arena_state.projectiles[i];
            if (!p->active) continue;
            /* p->team isn't synced over the wire (owner is enough -- the
               firer's team is already known client-side via the heroes
               array, no need for a second field carrying the same fact). */
            if (p->owner == my_owner) {
                glUniform4f_(loc_color, 0.1f, 0.95f, 1.0f, 1.0f); /* my own shot: bright cyan-white */
            } else if (arena_state.heroes[p->owner].team == arena_state.heroes[my_owner].team) {
                glUniform4f_(loc_color, 0.4f, 0.6f, 1.0f, 1.0f); /* ally's shot: light blue */
            } else {
                glUniform4f_(loc_color, 1.0f, 0.85f, 0.15f, 1.0f); /* enemy shot: hot yellow -- the thing you need to dodge */
            }
            Mat4 pt = mat4_translate(p->x, 0.8f, p->z);
            Mat4 ps = mat4_scale(0.35f, 0.35f, 0.35f);
            Mat4 pmodel = mat4_multiply(&pt, &ps);
            Mat4 pmvp = mat4_multiply(&vp, &pmodel);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, pmvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, pmodel.m);
            draw_mesh(&cube_mesh);
        }

        /* placement rings */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (int i = 0; i < MAX_RINGS; i++) {
            if (!rings[i].active) continue;
            float t01 = rings[i].age_ms / RING_LIFETIME_MS;
            float scale = 0.3f + t01 * 1.5f;
            float alpha = 1.0f - t01;
            Mat4 tr = mat4_translate(rings[i].x, 0.03f, rings[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 0.2f, 1.0f, 0.5f, alpha);
            draw_mesh(&ring_mesh);
        }
        /* attack flashes (S170-122): quick, small, orange-white burst right
           on the hit hero -- visually distinct from the slower green
           placement ring above (move-click feedback) so the two don't read
           as the same thing. */
        for (int i = 0; i < MAX_ATTACK_FLASHES; i++) {
            if (!attack_flashes[i].active) continue;
            float t01 = attack_flashes[i].age_ms / ATTACK_FLASH_LIFETIME_MS;
            float scale = 0.5f + t01 * 0.4f;
            float alpha = 1.0f - t01;
            Mat4 tr = mat4_translate(attack_flashes[i].x, 0.05f, attack_flashes[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, 1.0f, 0.75f, 0.15f, alpha);
            draw_mesh(&ring_mesh);
        }
        /* spell flashes (S170-124): one look per ability tier, same
           low-basic to high-ultimate color/size ramp any real MOBA uses --
           Q small and cyan-arcane, W a bit bigger and violet, R the
           biggest and gold, so which slot someone just cast reads at a
           glance even in a crowded team fight. */
        for (int i = 0; i < MAX_SPELL_FLASHES; i++) {
            if (!spell_flashes[i].active) continue;
            float t01 = spell_flashes[i].age_ms / SPELL_FLASH_LIFETIME_MS;
            float alpha = 1.0f - t01;
            float base_scale, rr, gg, bb;
            switch (spell_flashes[i].slot) {
                case 1: base_scale = 0.6f; rr = 0.3f; gg = 0.7f; bb = 1.0f; break;  /* Q: cyan-arcane */
                case 2: base_scale = 0.8f; rr = 0.7f; gg = 0.3f; bb = 1.0f; break;  /* W: violet */
                default: base_scale = 1.1f; rr = 1.0f; gg = 0.85f; bb = 0.2f; break; /* R: gold, biggest */
            }
            float scale = base_scale + t01 * 0.6f;
            Mat4 tr = mat4_translate(spell_flashes[i].x, 0.08f, spell_flashes[i].z);
            Mat4 sc = mat4_scale(scale, 1.0f, scale);
            Mat4 model = mat4_multiply(&tr, &sc);
            Mat4 mvp = mat4_multiply(&vp, &model);
            glUniformMatrix4fv_(loc_mvp, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv_(loc_model, 1, GL_FALSE, model.m);
            glUniform4f_(loc_color, rr, gg, bb, alpha);
            draw_mesh(&ring_mesh);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        /* ---- 2D HUD pass (legacy immediate mode, compatibility profile) ---- */
        glUseProgram_(0);
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, win_w, 0, win_h, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        /* Enhanced cursor hover state (S170-69 revisited): which hero, if any, the mouse is
           currently over, and its screen-space bar position -- found in the same pass as the
           health bars below (cheapest place to do it, world_to_screen already runs there for
           every hero) and consumed just after the loop to draw a highlight + tooltip on top of
           everything. SDL mouse Y is top-down; world_to_screen's sy is bottom-up (matches this
           HUD's own glOrtho), same flip the OK-button hit test already uses. */
        int raw_mx, raw_my;
        SDL_GetMouseState(&raw_mx, &raw_my);
        float mouse_hx = (float)raw_mx, mouse_hy = (float)(win_h - raw_my);
        int hovered_i = -1;
        float hovered_sx = 0, hovered_sy = 0;
        float hovered_best_dist_sq = 30.0f * 30.0f; /* hover radius */

        /* Per-hero floating health bars (S170-89: "health bar hovers over hero") -- every
           alive hero, not just YOU/nearest-enemy's fixed HUD bars, so a 20-hero team match
           actually shows damage landing on whoever's in view. Reuses the same vp matrix the
           3D pass just drew with, projected into this 2D HUD's pixel space. */
        for (int i = 0; i < ARENA_MAX_HEROES; i++) {
            ArenaHero *h = &arena_state.heroes[i];
            if (!h->alive) continue;
            float sx, sy;
            if (!world_to_screen(&vp, h->x, 1.6f, h->z, win_w, win_h, &sx, &sy)) continue;
            if (sx < -40 || sx > win_w + 40 || sy < -20 || sy > win_h + 20) continue;
            float frac = h->max_hp > 0 ? (float)h->hp / h->max_hp : 0.0f;
            float bw = 40.0f, bh = 5.0f;
            glColor3f(0.1f, 0.1f, 0.1f);
            glBegin(GL_QUADS);
            glVertex2f(sx - bw / 2, sy); glVertex2f(sx + bw / 2, sy);
            glVertex2f(sx + bw / 2, sy + bh); glVertex2f(sx - bw / 2, sy + bh);
            glEnd();
            if (i == my_owner) glColor3f(0.1f, 0.8f, 0.95f);
            else if (h->team == arena_state.heroes[my_owner].team) glColor3f(0.15f, 0.55f, 0.95f);
            else glColor3f(0.9f, 0.25f, 0.15f);
            glBegin(GL_QUADS);
            glVertex2f(sx - bw / 2, sy); glVertex2f(sx - bw / 2 + bw * frac, sy);
            glVertex2f(sx - bw / 2 + bw * frac, sy + bh); glVertex2f(sx - bw / 2, sy + bh);
            glEnd();
            /* S170-96: name label above the bar -- with 17+ heroes in the
               roster now, a colored bar alone doesn't say who's who at a
               glance. arena_hero_name() is the same token vocabulary the
               Game AI bridge already uses (lowercase, e.g. "morrigan"),
               reused here rather than inventing a separate display-name
               table. draw_string's own size param is roughly the glyph
               height in pixels; centered by eye against the bar width,
               not measured -- good enough for a short lowercase token. */
            draw_string(arena_hero_name(h->hero_id), sx - bw / 2, sy + bh + 2.0f, 10);

            /* Status-effect label (S170-133): a further line above the name, only drawn when
               something's actually active -- most heroes most ticks have nothing to show, and an
               always-present empty line would just be clutter. */
            char status_buf[64];
            if (hero_status_label(h, status_buf, sizeof(status_buf))) {
                glColor3f(0.95f, 0.75f, 0.15f);
                draw_string(status_buf, sx - bw / 2, sy + bh + 14.0f, 9);
                if (i == my_owner) glColor3f(0.1f, 0.8f, 0.95f);
                else if (h->team == arena_state.heroes[my_owner].team) glColor3f(0.15f, 0.55f, 0.95f);
                else glColor3f(0.9f, 0.25f, 0.15f);
            }

            float hdx = mouse_hx - sx, hdy = mouse_hy - (sy + bh / 2);
            float hdist_sq = hdx * hdx + hdy * hdy;
            if (hdist_sq < hovered_best_dist_sq) {
                hovered_best_dist_sq = hdist_sq;
                hovered_i = i;
                hovered_sx = sx;
                hovered_sy = sy;
            }
        }
        if (hovered_i >= 0) {
            ArenaHero *hh = &arena_state.heroes[hovered_i];
            float bw = 40.0f, bh = 5.0f;
            /* Relationship color, same convention as the bar fill above --
               self/ally/enemy read identically everywhere in this HUD. */
            float rr, gg, bb;
            const char *relation;
            if (hovered_i == my_owner) { rr = 0.1f; gg = 0.8f; bb = 0.95f; relation = "YOU"; }
            else if (hh->team == arena_state.heroes[my_owner].team) { rr = 0.15f; gg = 0.55f; bb = 0.95f; relation = "ALLY"; }
            else { rr = 0.95f; gg = 0.25f; bb = 0.15f; relation = "ENEMY"; }

            /* Bracket outline around the bar -- distinct from the bar's own
               border (which is always drawn, hover or not): a wider,
               brighter box just outside it. */
            glColor3f(rr, gg, bb);
            glLineWidth(2.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(hovered_sx - bw / 2 - 3, hovered_sy - 3);
            glVertex2f(hovered_sx + bw / 2 + 3, hovered_sy - 3);
            glVertex2f(hovered_sx + bw / 2 + 3, hovered_sy + bh + 3);
            glVertex2f(hovered_sx - bw / 2 - 3, hovered_sy + bh + 3);
            glEnd();

            /* Tooltip near the cursor: relationship + name + real HP numbers,
               not just the bar's fractional fill. */
            char tip[64];
            snprintf(tip, sizeof(tip), "%s - %s (%d/%d)", relation, arena_hero_name(hh->hero_id), hh->hp, hh->max_hp);
            glColor3f(rr, gg, bb);
            draw_string(tip, mouse_hx + 14.0f, mouse_hy + 6.0f, 11);
        }

        glColor3f(0.1f, 0.8f, 0.95f);
        draw_string("YOU", 20, win_h - 40.0f, 14);
        glColor3f(1.0f, 1.0f, 1.0f);
        {
            ArenaHero *h = &arena_state.heroes[my_owner];
            float frac = (float)h->hp / h->max_hp;
            glColor3f(0.2f, 0.2f, 0.2f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 38.0f); glVertex2f(290, win_h - 38.0f);
            glVertex2f(290, win_h - 20.0f); glVertex2f(90, win_h - 20.0f);
            glEnd();
            glColor3f(0.1f, 0.9f, 0.3f);
            glBegin(GL_QUADS);
            glVertex2f(90, win_h - 38.0f); glVertex2f(90 + 200 * frac, win_h - 38.0f);
            glVertex2f(90 + 200 * frac, win_h - 20.0f); glVertex2f(90, win_h - 20.0f);
            glEnd();
        }
        glColor3f(0.95f, 0.25f, 0.15f);
        draw_string(net_mode ? "NEAREST ENEMY" : "BOT", 20, win_h - 70.0f, 14);
        {
            /* heroes[1 - my_owner] only ever made sense for exactly 2 heroes (1v1) -- in
               team mode (S170-79 finding, real bug, not cosmetic) it either mislabels a
               teammate as ENEMY (heroes[1] is always team 0 same as heroes[0] for
               my_owner==0) or reads a negative out-of-bounds index for any my_owner > 1.
               arena_nearest_enemy() is the real team-aware lookup already used server-side. */
            ArenaHero *h = net_mode ? arena_nearest_enemy(my_owner) : &arena_state.heroes[1 - my_owner];
            if (h) {
                float frac = (float)h->hp / h->max_hp;
                glColor3f(0.2f, 0.2f, 0.2f);
                glBegin(GL_QUADS);
                glVertex2f(90, win_h - 68.0f); glVertex2f(290, win_h - 68.0f);
                glVertex2f(290, win_h - 50.0f); glVertex2f(90, win_h - 50.0f);
                glEnd();
                glColor3f(0.9f, 0.3f, 0.1f);
                glBegin(GL_QUADS);
                glVertex2f(90, win_h - 68.0f); glVertex2f(90 + 200 * frac, win_h - 68.0f);
                glVertex2f(90 + 200 * frac, win_h - 50.0f); glVertex2f(90, win_h - 50.0f);
                glEnd();
            }
        }

        {
            /* Own hero's kit status -- real Overwatch-style recast-time tiles (S170-127,
               "add the ability frame cooldown timer tiles from shankpit og engine as recast
               time affordances" -> "make it like overwatch recast frames for q w e"). Ported
               the tile visual language from SHANKPIT's apps/lobby/src/main.c
               draw_ability_one_tile() (bordered square, background/border color swap on
               cooldown, big centered countdown number, keybind label) and added a real radial
               wipe on top -- REDGARDEN has 3 slots with very different cooldown lengths across
               19 heroes, not SHANKPIT's single fixed-cooldown ability, so a flat color tint
               alone doesn't show *how much* cooldown is left the way Overwatch's ability icons
               do. No per-hero max-cooldown table exists client-side to compute that fraction
               against, so it's tracked locally instead: remember the highest cooldown_ms value
               seen since it last hit 0 (arms the instant a cast starts it counting down from
               its real peak) and wipe the fraction of that peak still remaining -- self-
               correcting per-hero-per-slot with no new wire data needed.

               S170-137: readiness is no longer cooldown-only. `mp` reaches the client now
               (net_poll_snapshots, protocol.h's ArenaHeroSnapshot) instead of sitting zeroed
               forever in net_mode, so each tile can flag "off cooldown but can't actually
               afford it" against this slot's own flat ARENA_MP_COST_*. */
            ArenaHero *h = &arena_state.heroes[my_owner];
            draw_ability_tile(20.0f, win_h - 168.0f, 56.0f, h->q_cooldown_ms, &q_cooldown_peak_ms,
                               0, h->mp < ARENA_MP_COST_Q, "Q", arena_ability_name(h->hero_id, 0), 0.3f, 0.7f, 1.0f);
            draw_ability_tile(86.0f, win_h - 168.0f, 56.0f, h->w_cooldown_ms, &w_cooldown_peak_ms,
                               h->w_active, (!h->w_active && h->mp < ARENA_MP_COST_W), "W", arena_ability_name(h->hero_id, 1), 0.7f, 0.3f, 1.0f);
            draw_ability_tile(152.0f, win_h - 168.0f, 56.0f, h->r_cooldown_ms, &r_cooldown_peak_ms,
                               h->r_active_ms > 0, h->mp < ARENA_MP_COST_R, "E", arena_ability_name(h->hero_id, 2), 1.0f, 0.85f, 0.2f);
        }

        if (show_apm) {
            char apmbuf[24];
            snprintf(apmbuf, sizeof(apmbuf), "APM %d", apm_compute(now));
            glColor3f(0.9f, 0.9f, 0.3f);
            draw_string(apmbuf, win_w - 140.0f, win_h - 30.0f, 14);
        }

        if (arena_state.winner != 0) {
            if (arena_state.winner == my_owner + 1) {
                glColor3f(0.2f, 1.0f, 0.4f);
                draw_string("YOU WIN", win_w / 2.0f - 150, win_h / 2.0f, 24);
            } else {
                glColor3f(1.0f, 0.2f, 0.2f);
                draw_string("YOU LOSE", win_w / 2.0f - 160, win_h / 2.0f, 24);
            }
            if (net_mode) {
                /* Requeue OK button -- bounds must match the click hit-test above. */
                glColor3f(0.15f, 0.35f, 0.2f);
                glBegin(GL_QUADS);
                glVertex2f(win_w / 2.0f - 90, win_h / 2.0f - 70);
                glVertex2f(win_w / 2.0f + 90, win_h / 2.0f - 70);
                glVertex2f(win_w / 2.0f + 90, win_h / 2.0f - 30);
                glVertex2f(win_w / 2.0f - 90, win_h / 2.0f - 30);
                glEnd();
                glColor3f(0.6f, 1.0f, 0.7f);
                draw_string("OK - REQUEUE", win_w / 2.0f - 78, win_h / 2.0f - 55, 14);
            }
        }
        glEnable(GL_DEPTH_TEST);

        SDL_GL_SwapWindow(win);
        SDL_Delay(16);
    }

    if (audio_dev != 0) SDL_CloseAudioDevice(audio_dev);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
