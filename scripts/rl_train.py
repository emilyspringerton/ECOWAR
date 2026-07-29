#!/usr/bin/env python3
"""
scripts/rl_train.py (S170-226, NORTHSTAR §21) -- trains the REDGARDEN arena bot AI via PPO
(Stable-Baselines3) against scripts/rl_env.py's ArenaTrainingEnv, reward-driven, in place of
S170-194/195/220's own corpus-based next-token-prediction pretraining (founder: "i want
unsupervised learning with rewards like in the unity ml-agents plugin").

Same delivery pattern S170-220's own scripts/colab_train.py already established: config from
CLI args (with env-var defaults so a notebook bootstrap cell can drive it), runs equally well
locally (this whole sim has no display dependency -- confirmed by this repo's own headless test
suite) or on Colab. Requires build/libarena_training.so to already exist (`bash
scripts/build_training.sh`) -- this script does not build it itself, since building is a fast,
deterministic, environment-independent step that doesn't belong gated behind a slow pip install
the way scripts/colab_train.py's own deferred-import pattern gates torch/transformers.

Trains a small policy network (SB3's own default `net_arch=[64, 64]` MLP for both actor and
critic -- NORTHSTAR §21.3 explicitly leaves this as a default, not confirmed final tuning) --
after training, hands off to scripts/export_rl_policy_to_c.py (S170-227) to extract just the
POLICY (actor) network's weights into the embedded-C format packages/common's own small MLP
inference module expects.

NOT independently verified end to end in the environment this was written in: `gymnasium`/
`stable_baselines3` are not installable here (no venv module, externally-managed system Python,
no sudo) -- see scripts/rl_env.py's own doc comment for the same gap and what WAS verified there
(the environment's own ctypes/observation/reward logic, directly). This script's own PPO
configuration is written to Stable-Baselines3's documented API but has not been run. Whoever
next has a normal Python environment should `pip install gymnasium stable-baselines3` and run
this file to close that gap -- flagged here, not claimed.
"""

import argparse
import os
import sys


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--lib-path", default=os.environ.get("ARENA_TRAINING_LIB", None),
                   help="default: <repo-root>/build/libarena_training.so (scripts/rl_env.py's "
                        "own default)")
    p.add_argument("--total-timesteps", type=int,
                   default=int(os.environ.get("RL_TOTAL_TIMESTEPS", 200_000)))
    p.add_argument("--n-envs", type=int, default=int(os.environ.get("RL_N_ENVS", 4)),
                   help="parallel environment copies PPO collects rollouts from -- each is an "
                        "independent in-process libarena_training.so instance (ctypes.CDLL "
                        "loads its own copy of the sim's global ArenaState per process when run "
                        "under SB3's own SubprocVecEnv, so parallelism is real, not simulated)")
    p.add_argument("--learning-rate", type=float, default=float(os.environ.get("RL_LEARNING_RATE", 3e-4)))
    p.add_argument("--n-steps", type=int, default=int(os.environ.get("RL_N_STEPS", 2048)),
                   help="PPO rollout length per env before each policy update -- SB3's own default")
    p.add_argument("--batch-size", type=int, default=int(os.environ.get("RL_BATCH_SIZE", 64)))
    p.add_argument("--net-arch", type=int, nargs="+",
                   default=[int(x) for x in os.environ.get("RL_NET_ARCH", "64,64").split(",")],
                   help="hidden layer sizes for BOTH the policy (actor) and value (critic) "
                        "networks -- NORTHSTAR §21.3: SB3's own default, not confirmed final "
                        "tuning. Only the policy half gets exported to C (S170-227) -- the "
                        "value network is training-only scaffolding, a real game never needs it.")
    p.add_argument("--hero0-id", type=int, default=int(os.environ.get("RL_HERO0_ID", 0)),
                   help="which hero ID the Agent plays -- default 0 (Unicorn), matching "
                        "scripts/rl_env.py's own default")
    p.add_argument("--hero1-id", type=int, default=int(os.environ.get("RL_HERO1_ID", 1)),
                   help="which hero ID the heuristic-AI opponent plays -- default 1 (Duck)")
    p.add_argument("--output-dir", default=os.environ.get("RL_OUTPUT_DIR", "rl_checkpoints"))
    p.add_argument("--save-freq", type=int, default=int(os.environ.get("RL_SAVE_FREQ", 20_000)),
                   help="save a checkpoint every N timesteps, in addition to the final save")
    p.add_argument("--eval-episodes", type=int, default=int(os.environ.get("RL_EVAL_EPISODES", 20)),
                   help="episodes to run for the final win-rate report against the same "
                        "heuristic opponent it trained against")
    return p.parse_args()


def make_env(lib_path, hero0_id, hero1_id):
    """Factory closure for SB3's VecEnv constructors (each parallel env needs its own callable,
    not a shared instance -- SB3's own documented pattern)."""
    def _init():
        # Imported here, not at module level, so --help and argument parsing work even without
        # gymnasium installed (same deferred-import reasoning scripts/colab_train.py's own
        # pip_install()-gated imports already use for torch/transformers).
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from rl_env import ArenaTrainingEnv
        return ArenaTrainingEnv(lib_path=lib_path, hero0_id=hero0_id, hero1_id=hero1_id)
    return _init


def main():
    args = parse_args()

    from stable_baselines3 import PPO
    from stable_baselines3.common.vec_env import SubprocVecEnv, DummyVecEnv

    env_fns = [make_env(args.lib_path, args.hero0_id, args.hero1_id) for _ in range(args.n_envs)]
    # DummyVecEnv (single process) when n_envs==1 -- SubprocVecEnv's own process-pool overhead
    # isn't worth it for a single env, same reasoning SB3's own docs give.
    vec_env = DummyVecEnv(env_fns) if args.n_envs == 1 else SubprocVecEnv(env_fns)

    os.makedirs(args.output_dir, exist_ok=True)

    # A flat net_arch list (same hidden sizes for both actor and critic) rather than the
    # separate-pi/vf dict form -- SB3's dict-based net_arch syntax has changed shape across
    # major versions (a version-sensitivity risk this script can't pin down without a real SB3
    # install to test against, see this file's own doc comment), while the flat-list form has
    # stayed stable and is exactly what NORTHSTAR §21.3 itself describes ("SB3's own default
    # net_arch=[64, 64]").
    policy_kwargs = dict(net_arch=args.net_arch)
    model = PPO(
        "MlpPolicy",
        vec_env,
        learning_rate=args.learning_rate,
        n_steps=args.n_steps,
        batch_size=args.batch_size,
        policy_kwargs=policy_kwargs,
        verbose=1,
    )

    print(f"Training PPO: total_timesteps={args.total_timesteps} n_envs={args.n_envs} "
          f"net_arch={args.net_arch} hero0={args.hero0_id} hero1={args.hero1_id}")
    print("Reward function: NORTHSTAR §21.2 / scripts/rl_env.py's own compute_reward() -- "
          "damage dealt/taken, kill/death, Flow/XP, alive bonus, terminal win/loss.")

    checkpoint_path_template = os.path.join(args.output_dir, "ppo_arena_step_{}")
    timesteps_done = 0
    while timesteps_done < args.total_timesteps:
        chunk = min(args.save_freq, args.total_timesteps - timesteps_done)
        model.learn(total_timesteps=chunk, reset_num_timesteps=False)
        timesteps_done += chunk
        ckpt_path = checkpoint_path_template.format(timesteps_done)
        model.save(ckpt_path)
        print(f"Checkpoint saved: {ckpt_path}.zip ({timesteps_done}/{args.total_timesteps} timesteps)")

    final_path = os.path.join(args.output_dir, "ppo_arena_final")
    model.save(final_path)
    print(f"Final model saved: {final_path}.zip")

    # Evaluation: real episodes against the same heuristic-AI opponent (arena_bot_enabled's own
    # existing bot_cast_kit_if_ready), not a synthetic metric -- win rate is the actual objective
    # the terminal +-10.0 reward term is shaped around.
    print(f"\nEvaluating over {args.eval_episodes} episodes...")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from rl_env import ArenaTrainingEnv
    eval_env = ArenaTrainingEnv(lib_path=args.lib_path, hero0_id=args.hero0_id, hero1_id=args.hero1_id)
    wins = losses = draws = 0
    for ep in range(args.eval_episodes):
        obs, _ = eval_env.reset()
        terminated = truncated = False
        while not (terminated or truncated):
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, terminated, truncated, info = eval_env.step(action)
        winner = info.get("winner", 0)
        if winner == 1:
            wins += 1
        elif winner == 2:
            losses += 1
        else:
            draws += 1
    print(f"Eval results: {wins}W {losses}L {draws}D over {args.eval_episodes} episodes "
          f"({100.0 * wins / args.eval_episodes:.1f}% win rate vs. the heuristic bot AI)")

    print("=" * 60)
    print(f"DONE. Final policy: {final_path}.zip")
    print("This is a trained PPO policy against the EXISTING heuristic bot AI only (NORTHSTAR")
    print("§21.3: self-play/curriculum explicitly out of scope for this first pass).")
    print("Next: scripts/export_rl_policy_to_c.py (S170-227) extracts the policy network's own")
    print("weights into the embedded-C format, then git-syncs them the same way S170-220's own")
    print("git_sync_weights_to_repo() already does for the corpus-pretrained model.")
    print("File a completion Apple and mark the relevant EMILY/BACKLOG.md item done.")
    print("=" * 60)


if __name__ == "__main__":
    main()
