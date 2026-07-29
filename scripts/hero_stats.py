#!/usr/bin/env python3
"""
scripts/hero_stats.py (2026-07-29) -- aggregates per-hero win rate / pick rate from real match
logs, in answer to the founder's own question: "can we start crunching the data on the heroes
that are the strongest? does our match replay system let us start tracking stats like win rate
etc." The honest answer at the time this was asked was NO -- apps/arena_server/src/main.c's own
match log (var/matches/*.jsonl) recorded per-tick x/z/hp/alive snapshots and a final winner, but
never which hero_id a given owner actually played; report_match_result's separate IDUNA POST
(player_game_stats) tracks win/loss per PLAYER but has no hero_id column at all, and adding one
there would mean a cross-repo IDUNA migration this pass deliberately doesn't take on, since this
whole analysis is fully answerable from REDGARDEN's own local match logs alone. Same commit that
added this script also added the missing piece: a `draft_complete` event
(apps/arena_server/src/main.c's own `match_log_draft_complete()`), written once per match right
when the draft finishes, recording {owner, team, hero_id} for all 20 slots.

IMPORTANT, flagged not faked: the 5,860+ match logs already sitting in var/matches/ from BEFORE
this fix cannot be used here -- they have no draft_complete event, so there is no way to
recover which hero was played in any of them. Every number this script prints only reflects
matches played AFTER this pass landed; hero stats start from zero, not from this repo's real
match history. This script re-scans that (initially empty, growing) window every time it's run,
not a one-shot backfill, since there's nothing to backfill.

Usage:
    python3 scripts/hero_stats.py [--matches-dir var/matches] [--min-games N]
"""

import argparse
import glob
import json
import os
import sys

# Hand-synced copy of packages/simulation/arena_ai_bridge.c's own arena_hero_name() switch --
# this script has no C header/library access, same "duplicated by hand" reasoning
# apps/arena_bot/src/main.c's own ARENA_HERO_COUNT copy already documents for itself. Bump
# alongside that function whenever a new hero is added.
HERO_NAMES = [
    "unicorn", "duck", "ghost", "frog", "doc_wheel", "tree", "pizza", "flamel",
    "morrigan", "dagda", "courier", "loki", "gary", "flute_debt", "bacon_puck",
    "abraham", "ada", "tyler", "paimon", "noor1", "cain", "gunnr", "vassago",
    "he_xiangu", "beleth", "mnm", "weatherman", "zagan",
]


def hero_name(hero_id):
    if 0 <= hero_id < len(HERO_NAMES):
        return HERO_NAMES[hero_id]
    return f"unknown({hero_id})"


def scan_match_file(path):
    """Returns (heroes, winner) if this file has both a draft_complete and a match_end event,
    else None -- either a pre-fix log (no draft_complete ever written) or a match that never
    actually finished (timed out mid-draft/mid-game, no match_end -- see REDGARDEN Apple #11297/
    #11301's own session for how often that's happened live: matchmaker restarts, phantom queue
    entries, etc. are all real, common reasons a match log ends without ever reaching
    match_end). Both are legitimately excluded from win-rate stats, not silently miscounted as a
    loss or a draw."""
    heroes = None
    winner = None
    try:
        with open(path, "r") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue  # a partially-written last line (server killed mid-write) -- skip, don't crash the whole scan over one file
                if event.get("event") == "draft_complete":
                    heroes = event.get("heroes")
                elif event.get("event") == "match_end":
                    winner = event.get("winner")
    except OSError:
        return None
    if heroes is None or winner is None or winner == 0:
        return None
    return heroes, winner


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--matches-dir", default=os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "var", "matches"))
    parser.add_argument("--min-games", type=int, default=1,
                         help="hide heroes with fewer than this many recorded games (default 1 -- "
                              "everything, since this data is brand new and even 1 game is real "
                              "signal worth seeing while the sample is still tiny)")
    args = parser.parse_args()

    files = sorted(glob.glob(os.path.join(args.matches_dir, "*.jsonl")))
    if not files:
        print(f"no match logs found in {args.matches_dir}", file=sys.stderr)
        sys.exit(1)

    # hero_id -> [wins, games]
    stats = {}
    matches_with_data = 0
    matches_without_data = 0

    for path in files:
        result = scan_match_file(path)
        if result is None:
            matches_without_data += 1
            continue
        matches_with_data += 1
        heroes, winner = result
        for h in heroes:
            hero_id = h["hero_id"]
            team = h["team"]
            won = (team + 1) == winner
            wins, games = stats.get(hero_id, [0, 0])
            stats[hero_id] = [wins + (1 if won else 0), games + 1]

    print(f"Scanned {len(files)} match log(s): {matches_with_data} with real draft+outcome data, "
          f"{matches_without_data} without (pre-fix logs with no draft_complete event, or "
          f"matches that never reached match_end -- see this script's own module doc comment).")
    if matches_with_data == 0:
        print("\nNo usable data yet -- this is expected immediately after this fix lands. "
              "Re-run this script after a few real matches have actually finished.")
        return

    print(f"\n{'Hero':<14} {'Games':>6} {'Wins':>6} {'Win rate':>9}")
    print("-" * 38)
    rows = []
    for hero_id, (wins, games) in stats.items():
        if games < args.min_games:
            continue
        rows.append((hero_name(hero_id), games, wins, wins / games))
    rows.sort(key=lambda r: (-r[3], -r[1]))  # win rate desc, then games desc as a tiebreak
    for name, games, wins, rate in rows:
        print(f"{name:<14} {games:>6} {wins:>6} {rate:>8.1%}")

    if rows and rows[0][1] < 20:
        print(f"\nSample size warning: the most-played hero above has only {rows[0][1]} recorded "
              f"game(s) -- treat every win rate here as noisy until real volume builds up. This "
              f"is a brand-new data source (see this script's own module doc comment), not a "
              f"backfill of REDGARDEN's real match history.")


if __name__ == "__main__":
    main()
