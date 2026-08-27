#!/usr/bin/env python3
"""
scripts/git_sync_utils.py (S170-227) -- shared git-commit-and-push-from-Colab logic, factored
out of scripts/colab_train.py's own git_sync_weights_to_repo() (S170-220, which now delegates
here) so scripts/export_rl_policy_to_c.py (S170-227) doesn't duplicate it for a second,
differently-shaped artifact (a generated C header instead of a binary weights blob).

Founder: "i will put the keys in MyDrive/.ssh" / "sync to git." Same "always commit and push,
standing instruction" convention every other REDGARDEN change this session followed, just
automated here instead of a human running the commands.
"""

import os
import shutil
import subprocess


def git_sync_file_to_repo(src_path, dest_relpath, repo_dir, ssh_key_path, commit_message):
    """Copies `src_path` to `<repo_dir>/<dest_relpath>`, then commits and pushes straight to
    origin/main over SSH using the key at `ssh_key_path`. Returns True if the push succeeded (or
    there was nothing new to push), False if skipped (no key found) or failed (logged, not
    raised -- an artifact's own success -- a trained checkpoint, an exported weights file --
    shouldn't be undone by a git-sync failure at the very end)."""
    if not os.path.exists(ssh_key_path):
        print(f"No SSH key at {ssh_key_path} -- skipping git-sync (the artifact is still saved "
              f"wherever the caller already put it; commit/push {dest_relpath} by hand, or fix "
              f"the ssh-key-path argument).")
        return False

    os.chmod(ssh_key_path, 0o600)  # git/ssh refuses to use a key with loose permissions
    dest = os.path.join(repo_dir, dest_relpath)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.copyfile(src_path, dest)

    ssh_cmd = (f"ssh -i {ssh_key_path} -o StrictHostKeyChecking=accept-new "
               f"-o UserKnownHostsFile=/content/.ssh_known_hosts")
    env = dict(os.environ, GIT_SSH_COMMAND=ssh_cmd)

    def run(cmd):
        return subprocess.run(cmd, cwd=repo_dir, env=env, check=True,
                               capture_output=True, text=True)

    try:
        run(["git", "remote", "set-url", "origin", "git@github.com:emilyspringerton/REDGARDEN.git"])
        run(["git", "config", "user.email", "colab-training@einhorn-industrial.local"])
        run(["git", "config", "user.name", "REDGARDEN Colab Training"])
        run(["git", "add", dest_relpath])
        status = run(["git", "status", "--porcelain"])
        if not status.stdout.strip():
            print(f"No change in {dest_relpath} vs. what's already committed -- nothing to push.")
            return True
        run(["git", "commit", "-m", commit_message])
        run(["git", "push", "origin", "HEAD:main"])
        print(f"Pushed {dest} to origin/main.")
        return True
    except subprocess.CalledProcessError as e:
        print(f"git-sync FAILED (artifact is still safe at {dest}): {e}\n"
              f"stdout: {e.stdout}\nstderr: {e.stderr}")
        return False
