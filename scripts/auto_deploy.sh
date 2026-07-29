#!/usr/bin/env bash
# scripts/auto_deploy.sh — keeps the live arena systemd units running the
# latest GREEN (completed + successful) CI build on main, polled via a
# systemd timer (see ops/systemd/redgarden-auto-deploy.{service,timer}).
#
# S170-100, founder: "ensure the server version always stays current with
# the currently latest passing build."
#
# Deliberately operates on a SEPARATE checkout (/home/fatbaby/redgarden-
# deploy), never the interactive dev checkout this script itself lives in
# (/home/fatbaby/REDGARDEN) -- an automated `git checkout <sha>` running
# against the same directory active development happens in would risk
# clobbering in-progress uncommitted work or racing a build.
#
# Guards against the exact CI-hang class of failure named in the backlog
# item (S170-84, a run sat "in_progress" indefinitely): only ever
# considers runs with status=completed AND conclusion=success, never just
# "the latest run."
#
# Also re-verifies locally before touching anything live -- CI's own
# green checkmark is a strong signal, not blind trust; this repo's own
# "tests before commit" discipline applies here too.
set -euo pipefail

REPO_SSH="git@github.com:emilyspringerton/REDGARDEN.git"
REPO_API="emilyspringerton/REDGARDEN"
DEPLOY_DIR="/home/fatbaby/redgarden-deploy"
LIVE_DIR="/home/fatbaby/REDGARDEN"
STATE_DIR="${DEPLOY_DIR}/var/auto_deploy"
STATE_FILE="${STATE_DIR}/.deployed_sha"
LOG_FILE="${LIVE_DIR}/var/logs/auto-deploy.log"

log() { printf '%s %s\n' "$(date -u '+%Y-%m-%d %H:%M:%S UTC')" "$1" | tee -a "${LOG_FILE}"; }

mkdir -p "$(dirname "${LOG_FILE}")"

if [ ! -d "${DEPLOY_DIR}/.git" ]; then
    log "bootstrapping fresh deploy checkout at ${DEPLOY_DIR}"
    git clone --quiet "${REPO_SSH}" "${DEPLOY_DIR}"
fi

cd "${DEPLOY_DIR}"
git fetch --quiet origin main

LATEST_GREEN_SHA=$(curl -s "https://api.github.com/repos/${REPO_API}/actions/workflows/ci.yml/runs?branch=main&status=success&per_page=1" \
    | jq -r '.workflow_runs[0].head_sha // empty')

if [ -z "${LATEST_GREEN_SHA}" ]; then
    log "no completed+successful CI run found on main -- nothing to do"
    exit 0
fi

mkdir -p "${STATE_DIR}"
CURRENT_SHA=""
[ -f "${STATE_FILE}" ] && CURRENT_SHA=$(cat "${STATE_FILE}")

if [ "${LATEST_GREEN_SHA}" = "${CURRENT_SHA}" ]; then
    log "already deployed ${LATEST_GREEN_SHA} -- up to date"
    exit 0
fi

log "new green build available: ${LATEST_GREEN_SHA} (currently deployed: ${CURRENT_SHA:-none}) -- deploying"

git checkout --quiet "${LATEST_GREEN_SHA}"

if ! bash scripts/build.sh >> "${LOG_FILE}" 2>&1; then
    log "build failed for ${LATEST_GREEN_SHA} -- aborting, leaving ${CURRENT_SHA:-none} live"
    exit 1
fi

if ! bash scripts/test_arena.sh >> "${LOG_FILE}" 2>&1; then
    log "local test_arena.sh failed for ${LATEST_GREEN_SHA} (despite CI green) -- aborting, leaving ${CURRENT_SHA:-none} live"
    exit 1
fi

if ! bash scripts/test_10_bots.sh >> "${LOG_FILE}" 2>&1; then
    log "local test_10_bots.sh failed for ${LATEST_GREEN_SHA} (despite CI green) -- aborting, leaving ${CURRENT_SHA:-none} live"
    exit 1
fi

log "local verification passed -- publishing binaries to ${LIVE_DIR}/build/"
mkdir -p "${LIVE_DIR}/build"
# copy-then-rename, not a direct cp: red_garden_arena_bot in particular is a
# long-running process (the 19-bot pool) with the binary mapped the whole
# time it runs -- a direct overwrite hits ETXTBSY (found live, first real
# run of this script). rename() just repoints the directory entry to a new
# inode; already-running processes keep the old inode mapped until they're
# actually restarted below, exactly the atomic-swap pattern package
# managers use for the same reason.
for bin in red_garden_arena_server red_garden_arena_bot red_garden_matchmaker red_garden_arena; do
    if [ -f "build/${bin}" ]; then
        cp "build/${bin}" "${LIVE_DIR}/build/${bin}.new"
        mv "${LIVE_DIR}/build/${bin}.new" "${LIVE_DIR}/build/${bin}"
    fi
done

export XDG_RUNTIME_DIR="/run/user/$(id -u)"

# Match-aware restart guard (2026-07-29, EMILY/BACKLOG.md follow-up to Apple #11297): restarting
# the matchmaker/bot-pool units used to kill any currently-live match along with them -- spawned
# `red_garden_arena_server` processes are forked children of the matchmaker, not their own
# systemd units, so systemd's own control-group kill on restart took them out too. Real incident:
# the founder got through a draft, the timer fired ~2 minutes later, and their just-started match
# server died mid-game (Apple #11297) -- the timer was stopped afterward specifically because of
# this, not re-enabled since. A spawned server process only exists between "lobby just filled"
# and "match ended/timed out" (spawn_game_server/`running = 0`, apps/arena_server/src/main.c) --
# checking for ANY of them is a simple, sufficient proxy for "is a real match plausibly in
# progress right now," no new status endpoint needed. Binaries are already published above
# regardless (harmless either way -- the matchmaker execs `server_bin` fresh per spawn, so a
# future match picks up the new server binary without this restart at all); only the
# bot-pool/matchmaker restart itself, and marking this SHA as deployed, are deferred. Not writing
# STATE_FILE here means the next timer tick (5 min later) retries this whole check from scratch
# until it lands in a genuinely idle window, rather than silently giving up after one skip.
if pgrep -f "build/red_garden_arena_server --port" > /dev/null 2>&1; then
    log "deferring restart for ${LATEST_GREEN_SHA} -- a match server is currently running (binaries published, will retry next cycle)"
    exit 0
fi

systemctl --user restart redgarden-matchmaker-bots.service redgarden-matchmaker-players.service redgarden-bot-pool.service

echo "${LATEST_GREEN_SHA}" > "${STATE_FILE}"
log "deployed ${LATEST_GREEN_SHA}, restarted live systemd units"
