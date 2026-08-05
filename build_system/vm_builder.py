#!/usr/bin/env python3
"""
vm_builder.py — General-purpose remote script runner.

Usage (from project root):
    python build_system/vm_builder.py

Expects build_system/script.sh to exist next to this file.
build_machine.json must exist in the project root.

Flow per VM:
  1. Optionally find a faster route via oroute
  2. Rsync project root into a fresh tmp dir on the remote
  3. Rsync script.sh into that same tmp dir
  4. Execute script.sh with bash -e (cwd = tmp dir, exports/ expected at tmp/exports/)
  5. Rsync tmp/exports/ back to ./builds/<vm_name>/
  6. Clean up tmp dir on success; leave it on failure for debugging

This is used for building projects across a fleet of both virtual and real machine (there is no diff to this machine)
It supports tailscale address for VMs if you want it to be fixed, and oRoute is used for faster routing such as
direct VM <---bridge---> HOST which tailscale cannot provide. Normally tailscale will use DERP on the VMs making this
process slower hence why we pass tailscale addrs to oRoute first. Also supports real hardware ofc.

"""

import os
import sys
import json
import uuid
import time
import shutil
import subprocess
from pathlib import Path
from json.decoder import JSONDecodeError
from concurrent.futures import ThreadPoolExecutor, as_completed

# Hosts that mean "run right here, no SSH" instead of a real remote target.
LOCAL_HOSTS = {"localhost", "127.0.0.1"}

# ---------------------------------------------------------------------------
# Sanity-check: we must be in build_system/ and project root must be its parent
# ---------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent

if SCRIPT_DIR.name != "build_system":
    print(
        f"✗ Expected this script to live inside a directory named 'build_system', "
        f"but it's in '{SCRIPT_DIR.name}'. Aborting."
    )
    sys.exit(1)

BUILD_MACHINE_JSON = PROJECT_ROOT / "build_machine.json"
if not BUILD_MACHINE_JSON.exists():
    print(f"✗ build_machine.json not found at {BUILD_MACHINE_JSON}. Aborting.")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Script to run — must live next to this file
# ---------------------------------------------------------------------------

script_path = SCRIPT_DIR / "script.sh"

if not script_path.exists():
    print(f"✗ script.sh not found at {script_path}. Aborting.")
    sys.exit(1)

if not os.access(script_path, os.R_OK):
    print(f"✗ script.sh is not readable at {script_path}. Aborting.")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Load VM configs
# ---------------------------------------------------------------------------

with open(BUILD_MACHINE_JSON, "r") as f:
    VM_CONFIGS: list[dict] = json.load(f)

# ---------------------------------------------------------------------------
# oroute: try to find a faster local route to each VM
# ---------------------------------------------------------------------------

OROUTE_BIN = "/usr/local/bin/oroute"


def resolve_host(config: dict) -> dict:
    """Return a (possibly updated) config with a faster local address if oroute finds one."""
    config = dict(config)  # don't mutate the original
    user_name, addr = config["host"].split("@", 1)
    print(f"Finding a faster way to {addr}...")

    if not Path(OROUTE_BIN).exists():
        print(f"  oroute not found at {OROUTE_BIN}, skipping.")
        return config

    try:
        output = subprocess.check_output([OROUTE_BIN, "-sresolve", config["host"]])
        try:
            oroute_info = json.loads(output)
        except JSONDecodeError:
            # print(f"  Error decoding oRoute output: {output!r}")
            return config

        if oroute_info.get("reachable"):
            local_addr = oroute_info["local_address"]
            print(f"  Found a faster route → {local_addr}")
            config["host"] = f"{user_name}@{local_addr}"
        else:
            print(f"  oRoute reports host unreachable, using original address.")

    except subprocess.SubprocessError as e:
        print(f"  oRoute subprocess error: {e}")

    return config


# ---------------------------------------------------------------------------
# Retry helper: SSH/key auth flakes intermittently (e.g. a passphrase-agent
# hiccup or a transient "Permission denied") and a bare retry with no other
# change usually succeeds. Only auth-shaped failures are retried — a real
# script error is not, since retrying that could re-run side effects.
# ---------------------------------------------------------------------------

AUTH_ERROR_PATTERNS = (
    "permission denied",
    "authentication failed",
    "too many authentication failures",
    "connection closed by remote host",
)


def _looks_like_auth_error(text: str) -> bool:
    t = (text or "").lower()
    return any(p in t for p in AUTH_ERROR_PATTERNS)


def _run_with_retry(fn, *, retries=3, delay=2.0, label=""):
    """Call fn() (a subprocess.run invocation); retry on auth-looking failures."""
    for attempt in range(1, retries + 1):
        try:
            return fn()
        except subprocess.CalledProcessError as e:
            if attempt < retries and _looks_like_auth_error(e.stderr):
                print(f"  ⚠ {label or 'command'}: auth error on attempt {attempt}/{retries}, retrying in {delay:.0f}s...")
                time.sleep(delay)
                continue
            raise


# ---------------------------------------------------------------------------
# VM identity: auto-detect OS / arch for naming the output directory
# ---------------------------------------------------------------------------

def get_vm_identity(host: str, password: str = "", key_file: str = "") -> str:
    """SSH into the VM and derive a human-readable identity string."""
    use_key = bool(key_file)
    ssh_opts = ["-o", "StrictHostKeyChecking=no", "-o", "BatchMode=no"]
    env = os.environ.copy()

    if use_key:
        ssh_opts.extend(["-i", str(key_file)])
        cmd = ["ssh", *ssh_opts, host, "uname -sm"]
    else:
        env["SSHPASS"] = password
        cmd = ["sshpass", "-e", "ssh", *ssh_opts, host, "uname -sm"]

    result = _run_with_retry(
        lambda: subprocess.run(cmd, env=env, capture_output=True, text=True, check=True),
        label="identity probe",
    )
    name = result.stdout.strip()
    return name.replace(" ", "-").lower()


def get_local_identity() -> str:
    """Derive a human-readable identity string for the local machine."""
    result = subprocess.run(["uname", "-sm"], capture_output=True, text=True, check=True)
    name = result.stdout.strip()
    return name.replace(" ", "-").lower()


# ---------------------------------------------------------------------------
# Local runner: same contract as run_on_vm, minus SSH entirely
# ---------------------------------------------------------------------------

def run_locally(local_script: Path) -> dict:
    """
    Run the script directly in PROJECT_ROOT (no rsync, no SSH) and move
    (not copy) the resulting exports/ into builds/<name>/.
    """
    try:
        vm_name = get_local_identity()
    except Exception as e:
        return {"name": "localhost", "status": "failed",
                "error": f"Identity probe failed: {type(e).__name__}: {e}"}

    output_dir = PROJECT_ROOT / "builds" / vm_name
    exports_dir = PROJECT_ROOT / "exports"

    print(f"[{vm_name}] Running locally in {PROJECT_ROOT} (no SSH)")

    stage = "unknown"
    try:
        stage = f"executing {local_script.name}"
        print(f"[{vm_name}] Executing {local_script.name}...")
        result = subprocess.run(
            ["bash", "-e", str(local_script)],
            cwd=PROJECT_ROOT,
            capture_output=True, text=True, check=True,
        )
        script_output = result.stdout + result.stderr

        stage = "checking exports/ directory"
        if not exports_dir.is_dir():
            raise RuntimeError(f"Script finished but exports/ was not created at {exports_dir}")

        stage = "moving exports/ into builds/"
        output_dir.parent.mkdir(parents=True, exist_ok=True)
        if output_dir.exists():
            shutil.rmtree(output_dir)
        shutil.move(str(exports_dir), str(output_dir))

        stage = "saving run.log"
        (output_dir / "run.log").write_text(script_output)

        print(f"[{vm_name}] ✓ Done → {output_dir}")
        return {
            "name": vm_name,
            "host": "localhost",
            "status": "success",
            "output_dir": str(output_dir),
        }

    except subprocess.CalledProcessError as e:
        stderr = e.stderr.strip() if e.stderr else ""
        stdout = e.stdout.strip() if e.stdout else ""
        error = f"Failed during: {stage} (exit {e.returncode})"
        if stdout:
            error += f"\nstdout: {stdout}"
        if stderr:
            error += f"\nstderr: {stderr}"
        print(f"[{vm_name}] ✗ Failed")
        return {"name": vm_name, "host": "localhost", "status": "failed", "error": error}

    except Exception as e:
        print(f"[{vm_name}] ✗ Failed")
        return {"name": vm_name, "host": "localhost", "status": "failed",
                "error": f"Failed during: {stage} — {type(e).__name__}: {e}"}


# ---------------------------------------------------------------------------
# Core runner: send project + script, execute, pull exports/
# ---------------------------------------------------------------------------

def run_on_vm(vm_config: dict, local_script: Path) -> dict:
    """
    Full lifecycle for a single VM:
      rsync project → rsync script → execute → pull exports/ → cleanup
    """
    host = vm_config.get("host", "")

    if host in LOCAL_HOSTS:
        return run_locally(local_script)

    password = vm_config.get("password", "")
    key_file = vm_config.get("key_file", "")
    key_password = vm_config.get("key_password", "")

    # --- validate config ---
    if not host:
        return {"name": "unknown", "status": "failed", "error": "Host is not configured"}
    if not password and not key_file:
        return {"name": host, "status": "failed", "error": "Neither password nor key_file is set"}

    # Determine authentication method and build environment/ssh options
    use_key_auth = bool(key_file)
    env = os.environ.copy()
    ssh_opts = ["-o", "StrictHostKeyChecking=no", "-o", "BatchMode=no"]

    if use_key_auth:
        # Key-based authentication
        if not Path(key_file).exists():
            return {"name": host, "status": "failed", "error": f"Key file not found: {key_file}"}
        ssh_opts.extend(["-i", str(key_file)])
        # If key has a passphrase, use ssh-agent with sshpass
        if key_password:
            env["SSHPASS"] = key_password
    else:
        # Password-based authentication (original behavior)
        env["SSHPASS"] = password

    def build_ssh_base():
        """Build base SSH command list."""
        if use_key_auth and key_password:
            # Use sshpass with SSH_ASKPASS for key passphrase
            return ["sshpass", "-e", "ssh", *ssh_opts]
        elif not use_key_auth:
            # Use sshpass for password auth
            return ["sshpass", "-e", "ssh", *ssh_opts]
        else:
            # Direct SSH with key (no passphrase)
            return ["ssh", *ssh_opts]

    def build_rsync_base():
        """Build base rsync command list."""
        if use_key_auth and key_password:
            return ["sshpass", "-e", "rsync", "-a", "--info=progress2", "-e", "ssh " + " ".join(ssh_opts)]
        elif not use_key_auth:
            return ["sshpass", "-e", "rsync", "-a", "--info=progress2", "-e", "ssh " + " ".join(ssh_opts)]
        else:
            return ["rsync", "-a", "--info=progress2", "-e", "ssh " + " ".join(ssh_opts)]

    def ssh(*remote_cmd_parts):
        cmd = [*build_ssh_base(), host, *remote_cmd_parts]
        return _run_with_retry(
            lambda: subprocess.run(cmd, env=env, check=True, capture_output=True, text=True),
            label=f"ssh ({' '.join(remote_cmd_parts)[:40]})",
        )

    def rsync_to(local_src, remote_dst, excludes=None):
        cmd = build_rsync_base()
        for ex in (excludes or []):
            cmd += ["--exclude", ex]
        cmd += [str(local_src), f"{host}:{remote_dst}"]
        _run_with_retry(
            lambda: subprocess.run(cmd, env=env, check=True, capture_output=True, text=True),
            label="rsync to remote",
        )

    def rsync_from(remote_src, local_dst):
        cmd = build_rsync_base()
        cmd += [f"{host}:{remote_src}", str(local_dst)]
        _run_with_retry(
            lambda: subprocess.run(cmd, env=env, check=True, capture_output=True, text=True),
            label="rsync from remote",
        )

    # --- identity ---
    try:
        vm_name = get_vm_identity(host, password, key_file)
    except subprocess.CalledProcessError as e:
        stderr = e.stderr.strip() if e.stderr else ""
        detail = f" — {stderr}" if stderr else ""
        return {"name": host, "status": "failed",
                "error": f"Identity probe failed (exit {e.returncode}){detail}"}
    except Exception as e:
        return {"name": host, "status": "failed", "error": f"Identity probe failed: {type(e).__name__}: {e}"}

    tmp_dir = f"/tmp/vm_runner_{uuid.uuid4().hex}"
    output_dir = PROJECT_ROOT / "builds" / vm_name

    print(f"[{vm_name}] Starting → tmp dir: {tmp_dir}")

    stage = "unknown"
    try:
        # 1. Create tmp dir on remote
        stage = "creating remote tmp dir"
        ssh(f"mkdir -p {tmp_dir}")

        # 2. Rsync project root into tmp dir
        stage = "syncing project root"
        print(f"[{vm_name}] Syncing project root...")
        rsync_to(
            str(PROJECT_ROOT) + "/",  # trailing slash = contents, not the directory itself
            tmp_dir,
            excludes=[
                ".git", ".build", ".env", "*.zip", "*.enc",
                "build", "cmake-build-*", "builds", 'target', '.venv', 'ghost_logs', 'logs'
            ],
        )

        # 3. Rsync the script into tmp dir (explicit push so it's always current and executable)
        stage = "sending script"
        print(f"[{vm_name}] Sending script: {local_script.name}")
        rsync_to(local_script, tmp_dir + "/")

        # 4. Make script executable and run it (cwd = tmp_dir)
        # bash -e: any command failure exits immediately and propagates non-zero back to us
        stage = f"executing {local_script.name}"
        print(f"[{vm_name}] Executing {local_script.name}...")
        result = ssh(
            f"chmod +x {tmp_dir}/{local_script.name} && "
            f"cd {tmp_dir} && "
            f"bash -e {tmp_dir}/{local_script.name}"
        )

        script_output = result.stdout + result.stderr

        # 5. Check exports/ exists
        stage = "checking exports/ directory"
        check_cmd = build_ssh_base() + [host, f"test -d {tmp_dir}/exports && echo yes || echo no"]
        check = subprocess.run(
            check_cmd,
            env=env, capture_output=True, text=True,
        )
        if check.stdout.strip() != "yes":
            raise RuntimeError(
                f"Script finished but exports/ was not created at {tmp_dir}/exports/"
            )

        # 6. Pull exports/ back
        stage = "pulling exports/ back"
        output_dir.mkdir(parents=True, exist_ok=True)
        print(f"[{vm_name}] Pulling exports/...")
        rsync_from(f"{tmp_dir}/exports/", str(output_dir) + "/")

        # 7. Save script stdout/stderr alongside exports
        stage = "saving run.log"
        (output_dir / "run.log").write_text(script_output)

        # 8. Cleanup tmp on success
        stage = "cleaning up remote tmp dir"
        ssh(f"rm -rf {tmp_dir}")
        print(f"[{vm_name}] ✓ Done → {output_dir}")

        return {
            "name": vm_name,
            "host": host,
            "status": "success",
            "output_dir": str(output_dir),
        }

    except subprocess.CalledProcessError as e:
        stderr = e.stderr.strip() if e.stderr else ""
        stdout = e.stdout.strip() if e.stdout else ""
        error = f"Failed during: {stage} (exit {e.returncode})"
        if stdout:
            error += f"\nstdout: {stdout}"
        if stderr:
            error += f"\nstderr: {stderr}"
        print(f"[{vm_name}] ✗ Failed during: {stage} — tmp left at {tmp_dir} for debugging")
        return {"name": vm_name, "host": host, "status": "failed",
                "error": error, "tmp_dir": tmp_dir}

    except Exception as e:
        print(f"[{vm_name}] ✗ Failed during: {stage} — tmp left at {tmp_dir} for debugging")
        return {"name": vm_name, "host": host, "status": "failed",
                "error": f"Failed during: {stage} — {type(e).__name__}: {e}", "tmp_dir": tmp_dir}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Resolve faster routes upfront (sequential is fine, network probing is fast)
    resolved_configs = []
    for config in VM_CONFIGS:
        if not config.get("host"):
            print(f"✗ Skipping entry with no host: {config}")
            continue
        # localhost needs no auth and no oroute — run in-process, no SSH
        if config["host"] in LOCAL_HOSTS:
            resolved_configs.append(config)
            continue
        # Support either password or key_file for authentication (backward compatible)
        if not config.get("password") and not config.get("key_file"):
            print(f"✗ {config['host']}: Neither password nor key_file is set, skipping.")
            continue
        # Validate key_file exists if specified
        if config.get("key_file") and not Path(config["key_file"]).exists():
            print(f"✗ {config['host']}: Key file not found: {config['key_file']}, skipping.")
            continue
        resolved_configs.append(resolve_host(config))

    if not resolved_configs:
        print("✗ No valid VM configurations found.")
        sys.exit(1)

    print(f"\nRunning '{script_path.name}' on {len(resolved_configs)} VM(s) in parallel...\n")

    results = []
    with ThreadPoolExecutor(max_workers=len(resolved_configs)) as executor:
        futures = {
            executor.submit(run_on_vm, vm, script_path): vm
            for vm in resolved_configs
        }
        for future in as_completed(futures):
            try:
                result = future.result()
                results.append(result)
            except Exception as e:
                vm = futures[future]
                results.append({
                    "name": vm.get("host", "unknown"),
                    "status": "failed",
                    "error": f"Unhandled executor error: {e}",
                })

    # --- Summary ---
    print("\n=== Run Summary ===")
    successful = [r for r in results if r["status"] == "success"]
    failed = [r for r in results if r["status"] == "failed"]

    if successful:
        print("Successful:")
        for r in successful:
            print(f"  ✓ {r['name']} ({r.get('host', '')}) → {r['output_dir']}")

    if failed:
        print("Failed:")
        for r in failed:
            tmp_hint = f" (tmp: {r['tmp_dir']})" if "tmp_dir" in r else ""
            print(f"  ✗ {r['name']}{tmp_hint}")
            for line in r.get("error", "Unknown error").splitlines():
                print(f"      {line}")

    print(f"\nTotal: {len(successful)} succeeded, {len(failed)} failed")

    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
