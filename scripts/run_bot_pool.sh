#!/usr/bin/env bash
# run_bot_pool.sh — launches N persistent apps/arena_bot processes against
# the bot-pool matchmaker and stays in the foreground (via `wait`), so a
# systemd unit supervising this script can actually track liveness instead
# of the launch-and-detach pattern scripts/launch_arena_pools.sh used
# (EMILY/BACKLOG.md S170-65 -- see ops/systemd/redgarden-bot-pool.service).
#
# Usage: ./scripts/run_bot_pool.sh [n_bots]   # default 19 -- lobby-size is 20 (S170-183:
#                                              # reverted back to 10v10 after briefly being
#                                              # 7v7/14 under S170-178) and one slot must
#                                              # stay open or a human can never queue in
#                                              # (S170-66: pool used to launch all N and the
#                                              # lobby was permanently full of bots). See
#                                              # ops/systemd/redgarden-bot-pool.service's own
#                                              # doc comment for the full 19-vs-20 tradeoff
#                                              # (self-sustaining match generation vs. a human
#                                              # being able to queue in) -- that value flips
#                                              # between the two on real founder direction from
#                                              # time to time; this script's own default here
#                                              # always stays 19 for any other/manual invocation
#                                              # regardless of whichever the live unit currently
#                                              # passes.
set -euo pipefail
cd "$(dirname "$0")/.."

N_BOTS="${1:-19}"
mkdir -p var

if [ ! -x ./build/red_garden_arena_bot ]; then
    echo "build first: bash scripts/build.sh" >&2
    exit 1
fi

# Guard against orphaned bots from a prior unclean exit (e.g. a manual run whose
# parent shell was SIGKILLed, bypassing the cleanup trap below) surviving alongside
# a fresh launch. Found live 2026-07-29: 19 orphaned bots (PPID reparented to 1,
# stale since 05:55) plus this script's own supervised 19 (from 10:10) put 38 bots
# against the 20-slot lobby -- bots alone filled every match, so the one open human
# slot never got a real connection and the draft screen never appeared.
pkill -f "build/red_garden_arena_bot --host 127.0.0.1" 2>/dev/null || true
sleep 1

pids=()
cleanup() {
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT TERM INT

for i in $(seq 1 "$N_BOTS"); do
    ./build/red_garden_arena_bot --host 127.0.0.1 --index "$i" > "var/arena_bot_$i.log" 2>&1 &
    pids+=("$!")
done

echo "launched $N_BOTS bots into the bot pool (indices 1-$N_BOTS)"
wait -n  # exit (and let systemd Restart= relaunch the whole set) if any bot dies
