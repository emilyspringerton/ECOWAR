#!/usr/bin/env python3
"""
Aggregates apps/arena_server's own per-match AI training corpus files
(var/corpus/arena-corpus-<port>-<ts>.jsonl, one arena_corpus_record() line
per active hero per tick -- see packages/simulation/arena_ai_bridge.c's own
doc comment) into a single combined corpus file, ready to sync to Drive and
train against with scripts/colab_train.py.

NORTHSTAR §18.4's own "corpus: every replay log from every hero, from every
match, undifferentiated" -- this script is that aggregation step. No
filtering by hero, outcome, or match: unsupervised pretraining wants the
whole, undifferentiated corpus, not a curated subset (that's the LATER,
supervised, NORN-graded stage NORTHSTAR §12 Phase E already names).

Usage:
    python3 scripts/build_ai_corpus.py                      # var/corpus/*.jsonl -> var/corpus/combined.jsonl
    python3 scripts/build_ai_corpus.py --out /tmp/corpus.jsonl
    python3 scripts/build_ai_corpus.py --min-records 1000    # warn (not fail) if the corpus looks too small to be worth training on yet
"""

import argparse
import glob
import json
import os
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--corpus-dir", default="var/corpus",
                   help="directory of per-match arena-corpus-*.jsonl files (default: var/corpus)")
    p.add_argument("--out", default=None,
                   help="default: <corpus-dir>/combined.jsonl")
    p.add_argument("--min-records", type=int, default=0,
                   help="warn if fewer than this many records were collected (default: 0, no warning)")
    args = p.parse_args()

    out_path = args.out or os.path.join(args.corpus_dir, "combined.jsonl")
    pattern = os.path.join(args.corpus_dir, "arena-corpus-*.jsonl")
    files = sorted(glob.glob(pattern))
    if not files:
        print(f"No corpus files found matching {pattern} -- nothing to aggregate.", file=sys.stderr)
        print("Corpus files are written by a real running apps/arena_server match "
              "(packages/simulation/arena_ai_bridge.c's arena_corpus_record, wired into "
              "apps/arena_server/src/main.c's corpus_log_tick). Play or run some bot "
              "matches first.", file=sys.stderr)
        sys.exit(1)

    total = 0
    skipped = 0
    with open(out_path, "w") as out_f:
        for path in files:
            with open(path) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        rec = json.loads(line)
                    except json.JSONDecodeError:
                        skipped += 1
                        continue
                    if "text" not in rec or not rec["text"].strip():
                        skipped += 1
                        continue
                    out_f.write(line + "\n")
                    total += 1

    print(f"Aggregated {len(files)} match corpus file(s) -> {out_path}")
    print(f"{total} records written" + (f", {skipped} skipped (malformed or empty)" if skipped else ""))
    if args.min_records and total < args.min_records:
        print(f"WARNING: only {total} records, fewer than --min-records {args.min_records} -- "
              "probably not enough real match data yet for a meaningful pretrain run.", file=sys.stderr)


if __name__ == "__main__":
    main()
