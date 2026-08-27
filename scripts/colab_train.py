#!/usr/bin/env python3
"""
Colab training driver for REDGARDEN's arena bot AI -- NORTHSTAR §18.4's own
"unsupervised pretraining" stage (S170-194, founder: "do the work to prepare
for unsupervised learning" / "target torch training on colab").

Ported from the sibling gpt2-alpine-c repo's own scripts/colab_train.py
(same proven pattern: notebook cell mounts Drive + git pulls + runs this
file, all real training logic lives here in git so a fresh Colab run always
executes the latest version without re-pasting cells) -- reused rather than
reinvented, per this session's own "check what already exists first"
convention.

What's different from gpt2-alpine-c's version, and why:
  - Corpus is packages/simulation/arena_ai_bridge.c's own arena_corpus_record()
    output (see scripts/build_ai_corpus.py), not the Emily Prime corpus --
    already `{"text": "..."}` per line, so record_to_text() below needs no
    changes at all, same reusable shape.
  - This is genuinely UNSUPERVISED pretraining, not the supervised,
    NORN-graded fine-tune NORTHSTAR §12 Phase E's own Milestone 7+ names as
    a LATER stage -- next-token prediction over raw state+action sequences,
    no win/loss label, no reward signal. The resulting checkpoint is meant
    to be the STARTING WEIGHTS for that later supervised stage (standard
    unsupervised-pretrain -> supervised-fine-tune ML pipeline shape), not
    itself a finished game-playing policy.
  - Output checkpoint dir/tarball name says "pretrain" explicitly so it's
    never confused with a later fine-tuned checkpoint on Drive.
  - S170-220 (founder: "we want to embed the weights right into the c code...
    we can do it all with colab scripts running python to do it all"):
    trains a SMALL CUSTOM architecture from scratch by default now, not a
    fine-tune of the public 124M-param "gpt2" checkpoint. gpt2-alpine-c's own
    real GPT-2-small weights are ~497MB as raw float32 -- comfortably fine
    for that repo's own use (a served model, not embedded in a real-time game
    loop), but both too large to reasonably commit to this git repo on every
    training run AND almost certainly too slow for real-time CPU inference
    inside REDGARDEN's own C game loop. --n-layer/--n-embd/--n-head/--n-ctx
    default to a genuinely small config (see parse_args below) sized to be
    both embeddable and fast, at the real cost of losing GPT-2's own public
    English-language pretraining as a warm start (a from-scratch small model
    can't load those weights -- different dimensions entirely). After
    training, the checkpoint is exported to the same flat binary weight
    format packages/common/gpt2_infer.c's ported C inference engine expects
    (scripts/export_weights_to_c.py, mirroring gpt2-alpine-c's own
    convert_checkpoint.py write order exactly) and, if a Drive SSH key is
    present, committed and pushed straight to origin/main -- see
    git_sync_weights_to_repo() below.

Config is read from environment variables (set by the notebook cell) with
defaults matching this project's own values, so it also runs standalone for
local smoke-testing (`python scripts/colab_train.py --help`).
"""

import argparse
import json
import math
import os
import subprocess
import sys
import tarfile


def pip_install():
    subprocess.check_call([
        sys.executable, "-m", "pip", "install", "-q",
        "transformers", "datasets", "accelerate",
    ])


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--drive-folder", default=os.environ.get(
        "DRIVE_FOLDER", "/content/drive/MyDrive/redgarden-training"))
    p.add_argument("--corpus-file", default=None,
                   help="default: <drive-folder>/redgarden-corpus.jsonl "
                        "(built locally with scripts/build_ai_corpus.py, then synced to Drive)")
    p.add_argument("--output-dir", default=None,
                   help="default: <drive-folder>/checkpoint-unsupervised-pretrain")
    p.add_argument("--model-name", default=os.environ.get("MODEL_NAME", "gpt2"),
                   help="tokenizer source only now (S170-220) -- the MODEL itself trains from "
                        "scratch at the small --n-layer/--n-embd/--n-head/--n-ctx config below, "
                        "not a fine-tune of this checkpoint's own pretrained weights, which are "
                        "a different (much larger) shape entirely once the architecture shrinks")
    p.add_argument("--n-layer", type=int, default=int(os.environ.get("N_LAYER", 4)),
                   help="S170-220: small enough to embed + run in real time in the C game loop, "
                        "unlike GPT-2-small's 12")
    p.add_argument("--n-embd", type=int, default=int(os.environ.get("N_EMBD", 128)),
                   help="S170-220: vs. GPT-2-small's 768 -- the wte embedding table "
                        "(vocab_size x n_embd) still dominates total size at this scale, since "
                        "the tokenizer's real ~50k-token vocab isn't shrunk in this pass")
    p.add_argument("--n-head", type=int, default=int(os.environ.get("N_HEAD", 4)),
                   help="must evenly divide --n-embd")
    p.add_argument("--n-ctx", type=int, default=int(os.environ.get("N_CTX", 256)),
                   help="S170-220: max context length the exported model supports -- kept <= "
                        "--max-length below")
    p.add_argument("--max-length", type=int, default=int(os.environ.get("MAX_LENGTH", 256)))
    p.add_argument("--batch-size", type=int, default=int(os.environ.get("BATCH_SIZE", 4)))
    p.add_argument("--grad-accum", type=int, default=int(os.environ.get("GRADIENT_ACCUMULATION", 4)))
    p.add_argument("--epochs", type=int, default=int(os.environ.get("NUM_EPOCHS", 3)))
    p.add_argument("--learning-rate", type=float, default=float(os.environ.get("LEARNING_RATE", 5e-5)))
    p.add_argument("--warmup-steps", type=int, default=int(os.environ.get("WARMUP_STEPS", 100)))
    p.add_argument("--save-steps", type=int, default=int(os.environ.get("SAVE_STEPS", 250)))
    p.add_argument("--skip-pip-install", action="store_true",
                   help="local smoke-testing only -- Colab always installs")
    p.add_argument("--skip-export", action="store_true",
                   help="local smoke-testing only -- skip converting the trained checkpoint to "
                        "the C inference engine's flat binary weight format")
    p.add_argument("--skip-git-sync", action="store_true",
                   help="skip committing/pushing the exported weights back to origin/main -- "
                        "on by default when no Drive SSH key is found (see "
                        "git_sync_weights_to_repo below), pass explicitly to force-skip even "
                        "when a key IS present")
    p.add_argument("--ssh-key-path", default=os.environ.get(
        "REDGARDEN_DRIVE_SSH_KEY", "/content/drive/MyDrive/.ssh/id_ed25519"),
        help="founder: 'i will put the keys in MyDrive/.ssh' -- default matches the "
             "conventional ed25519 key filename; override via REDGARDEN_DRIVE_SSH_KEY or this "
             "flag if the real key has a different name")
    p.add_argument("--repo-dir", default=os.environ.get("REPO_DIR", "/content/REDGARDEN"),
                   help="the notebook's own bootstrap cell clones/pulls REDGARDEN here before "
                        "running this script")
    return p.parse_args()


def record_to_text(rec):
    if "text" in rec:
        return rec["text"]
    if "prompt" in rec and "completion" in rec:
        return f"{rec['prompt']}\n\n### Response:\n{rec['completion']}"
    return ""


def export_weights_to_c_bin(model, output_bin_path):
    """S170-220: writes a flat float32 binary matching packages/common/gpt2_infer.c's own
    gpt2_model_load_weights expected layout EXACTLY -- mirrors the sibling gpt2-alpine-c repo's
    own convert_checkpoint.py write order verbatim (wte, wpe, then per-layer ln_1/c_attn/c_proj/
    ln_2/mlp_fc/mlp_proj, then ln_f), including the same Conv1D-to-row-major transpose GPT-2's
    own attention/MLP weights need (HuggingFace stores them as (in, out); the C engine's own
    `linear()` expects (out, in) for a standard y = W*x + b row-major matmul). Kept in this file
    rather than a separate script since it needs the in-memory `model` object right after
    training, not a saved checkpoint reloaded from disk -- scripts/export_weights_to_c.py exists
    separately for exporting an ALREADY-SAVED checkpoint later, without retraining."""
    import numpy as np

    def to_np(t):
        return t.detach().cpu().float().numpy()

    sd = model.state_dict()

    def get(key):
        if key in sd:
            return to_np(sd[key])
        raise KeyError(f"Missing weight key: {key}")

    def write_f32(f, arr):
        f.write(arr.astype(np.float32).flatten().tobytes())

    n_layer = model.config.n_layer
    with open(output_bin_path, "wb") as f:
        write_f32(f, get("transformer.wte.weight"))   # (V, D)
        write_f32(f, get("transformer.wpe.weight"))   # (T, D)
        for l in range(n_layer):
            p = f"transformer.h.{l}."
            write_f32(f, get(p + "ln_1.weight"))
            write_f32(f, get(p + "ln_1.bias"))
            write_f32(f, get(p + "attn.c_attn.weight").T)  # Conv1D (in,out) -> (out,in)
            write_f32(f, get(p + "attn.c_attn.bias"))
            write_f32(f, get(p + "attn.c_proj.weight").T)
            write_f32(f, get(p + "attn.c_proj.bias"))
            write_f32(f, get(p + "ln_2.weight"))
            write_f32(f, get(p + "ln_2.bias"))
            write_f32(f, get(p + "mlp.c_fc.weight").T)
            write_f32(f, get(p + "mlp.c_fc.bias"))
            write_f32(f, get(p + "mlp.c_proj.weight").T)
            write_f32(f, get(p + "mlp.c_proj.bias"))
        write_f32(f, get("transformer.ln_f.weight"))
        write_f32(f, get("transformer.ln_f.bias"))

    size_mb = os.path.getsize(output_bin_path) / 1024 / 1024
    print(f"Exported C inference weights: {output_bin_path} ({size_mb:.1f} MB)")
    return output_bin_path


def git_sync_weights_to_repo(bin_path, repo_dir, ssh_key_path, backlog_id="S170-220"):
    """S170-220, founder: 'i will put the keys in MyDrive/.ssh' / 'sync to git.' Delegates to
    scripts/git_sync_utils.py's own git_sync_file_to_repo() (S170-227 factored the actual
    copy+commit+push logic out into that shared module, since scripts/export_rl_policy_to_c.py
    needed the exact same capability for a second, differently-shaped artifact -- a generated C
    header, not a binary weights blob -- and duplicating it per-script would drift). Kept as a
    thin wrapper under its original name/signature rather than deleted, so nothing calling it by
    this name needs to change."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from git_sync_utils import git_sync_file_to_repo
    commit_message = f"feat(arena): update trained bot AI weights from Colab run\n\nbacklog: {backlog_id}"
    return git_sync_file_to_repo(bin_path, "weights/redgarden-arena-bot.bin", repo_dir,
                                  ssh_key_path, commit_message)


def main():
    args = parse_args()
    corpus_file = args.corpus_file or os.path.join(args.drive_folder, "redgarden-corpus.jsonl")
    output_dir = args.output_dir or os.path.join(args.drive_folder, "checkpoint-unsupervised-pretrain")

    print(f"Corpus: {corpus_file}")
    print(f"Output: {output_dir}")
    if not os.path.exists(corpus_file):
        raise FileNotFoundError(
            f"Corpus not found: {corpus_file}\n"
            "Run scripts/build_ai_corpus.py locally against var/corpus/ (populated by real "
            "apps/arena_server matches, see packages/simulation/arena_ai_bridge.c's "
            "arena_corpus_record) and sync the result to Drive, or drop it in the Drive "
            "folder by hand."
        )

    if not args.skip_pip_install:
        pip_install()

    # Imports deferred until after pip_install so a fresh Colab runtime
    # doesn't need these packages pre-installed to even parse this file.
    import torch
    from datasets import Dataset
    from transformers import (
        DataCollatorForLanguageModeling, GPT2Config, GPT2LMHeadModel, GPT2TokenizerFast,
        Trainer, TrainingArguments, pipeline,
    )

    records = []
    with open(corpus_file) as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    print(f"Loaded {len(records)} records")

    texts = [record_to_text(r) for r in records if record_to_text(r).strip()]
    dataset = Dataset.from_dict({"text": texts})
    print(f"Dataset size: {len(dataset)} examples")

    tokenizer = GPT2TokenizerFast.from_pretrained(args.model_name)
    tokenizer.pad_token = tokenizer.eos_token

    def tokenize(batch):
        out = tokenizer(batch["text"], truncation=True,
                         max_length=args.max_length, padding="max_length")
        out["labels"] = out["input_ids"].copy()
        return out

    tokenized = dataset.map(tokenize, batched=True, remove_columns=["text"])
    tokenized.set_format("torch")
    print(f"Tokenized: {len(tokenized)} examples, max_length={args.max_length}")

    # S170-220: a fresh, small-from-scratch config, NOT GPT2LMHeadModel.from_pretrained(args.model_name)
    # -- see this file's own module doc comment for why the architecture shrank (embeddability +
    # real-time C inference speed) and what that costs (no public GPT-2 pretraining transfer,
    # since a from-scratch small model can't load 768-dim/12-layer weights into a 128-dim/4-layer
    # shape). n_ctx must be >= max_length (the tokenizer pads/truncates to max_length; a shorter
    # n_ctx than that would silently drop the tail of every long training example).
    if args.n_ctx < args.max_length:
        raise ValueError(f"--n-ctx ({args.n_ctx}) must be >= --max-length ({args.max_length})")
    config = GPT2Config(
        vocab_size=tokenizer.vocab_size,
        n_positions=args.n_ctx,
        n_ctx=args.n_ctx,
        n_embd=args.n_embd,
        n_layer=args.n_layer,
        n_head=args.n_head,
        bos_token_id=tokenizer.bos_token_id,
        eos_token_id=tokenizer.eos_token_id,
    )
    print(f"Small from-scratch config: n_layer={args.n_layer} n_embd={args.n_embd} "
          f"n_head={args.n_head} n_ctx={args.n_ctx} vocab_size={tokenizer.vocab_size}")
    model = GPT2LMHeadModel(config)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model parameters: {n_params:,} (~{n_params * 4 / 1024 / 1024:.1f} MB as raw float32)")
    split = tokenized.train_test_split(test_size=0.1, seed=42)
    train_ds, eval_ds = split["train"], split["test"]
    print(f"Train: {len(train_ds)}, Eval: {len(eval_ds)}")

    data_collator = DataCollatorForLanguageModeling(tokenizer=tokenizer, mlm=False)
    training_args = TrainingArguments(
        output_dir=output_dir,
        overwrite_output_dir=True,
        num_train_epochs=args.epochs,
        per_device_train_batch_size=args.batch_size,
        gradient_accumulation_steps=args.grad_accum,
        per_device_eval_batch_size=args.batch_size,
        warmup_steps=args.warmup_steps,
        learning_rate=args.learning_rate,
        weight_decay=0.01,
        logging_dir="/content/logs",
        logging_steps=50,
        evaluation_strategy="steps",
        eval_steps=args.save_steps,
        save_steps=args.save_steps,
        save_total_limit=2,
        load_best_model_at_end=True,
        metric_for_best_model="eval_loss",
        fp16=torch.cuda.is_available(),
        report_to="none",
    )

    trainer = Trainer(model=model, args=training_args, train_dataset=train_ds,
                       eval_dataset=eval_ds, data_collator=data_collator)

    print("Starting unsupervised pretraining (next-token prediction, no win/loss label)...")
    trainer.train()
    print("Training complete.")

    trainer.save_model(output_dir)
    tokenizer.save_pretrained(output_dir)
    print(f"Saved to {output_dir}")

    tar_path = output_dir + ".tar.gz"
    with tarfile.open(tar_path, "w:gz") as tf:
        tf.add(output_dir, arcname="checkpoint-unsupervised-pretrain")
    size_mb = os.path.getsize(tar_path) / 1024 / 1024
    print(f"Archived: {tar_path} ({size_mb:.1f} MB)")

    eval_results = trainer.evaluate()
    perplexity = math.exp(eval_results["eval_loss"])
    print(f"Eval loss:   {eval_results['eval_loss']:.4f}")
    print(f"Perplexity:  {perplexity:.2f}")
    print("(No public GPT-2-small baseline to compare against any more -- S170-220 trains a "
          "small custom architecture from scratch, not a fine-tune of that checkpoint.)")

    bin_path = None
    if not args.skip_export:
        bin_path = export_weights_to_c_bin(model, output_dir + ".bin")

    if bin_path and not args.skip_git_sync:
        git_sync_weights_to_repo(bin_path, args.repo_dir, args.ssh_key_path)

    if torch.cuda.is_available():
        gen = pipeline("text-generation", model=model, tokenizer=tokenizer, device=0)
        prompts = [
            "redgarden arena tick:1000\nself hero:gary tags:ranged has_homing_attack pos:",
            "redgarden arena tick:1000\nself hero:unicorn tags:melee has_heal has_dash pos:-6.00,0.00 hp:30 max_hp:100 alive:1",
        ]
        print("\n--- Generation samples ---")
        for prompt in prompts:
            out = gen(prompt, max_new_tokens=60, do_sample=True, temperature=0.8, top_p=0.95)
            print(f"PROMPT: {prompt}")
            print(f"OUTPUT: {out[0]['generated_text']}\n")

    print("=" * 60)
    print(f"DONE. Checkpoint: {tar_path}")
    if bin_path:
        print(f"C inference weights: {bin_path} (packages/common/gpt2_infer.c's own binary "
              f"format -- see gpt2_model_load_weights)")
    print("This is the UNSUPERVISED PRETRAINING stage (NORTHSTAR §18.4) -- the starting")
    print("weights for §12 Phase E's own later supervised, NORN-graded fine-tune")
    print("(Milestone 7+), not a finished playing policy on its own.")
    print("Next: download from Drive, then start the model from this checkpoint")
    print("(not a from-scratch config) for that later supervised stage.")
    print("NOT done: wiring gpt2_infer.c's inference into the LIVE bot AI decision loop --")
    print("this pipeline trains + exports + syncs the weights, it doesn't yet make any bot")
    print("actually use them in a real match. Separate, future work.")
    print("File a completion Apple and mark the relevant EMILY/BACKLOG.md item done.")
    print("=" * 60)


if __name__ == "__main__":
    main()
