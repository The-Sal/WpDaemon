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
"""

import os
import sys
import json
import uuid
import subprocess
from pathlib import Path
from json.decoder import JSONDecodeError
from concurrent.futures import ThreadPoolExecutor, as_completed


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
# VM identity: auto-detect OS / arch for naming the output directory
# ---------------------------------------------------------------------------

def get_vm_identity(host: str, password: str) -> str:
    """SSH into the VM and derive a human-readable identity string."""
    env = {**os.environ, "SSHPASS": password}

    result = subprocess.run(
        [
            "sshpass", "-e", "ssh",
            "-o", "StrictHostKeyChecking=no",
            "-o", "BatchMode=no",
            host,
            "grep -E '^(ID=|VERSION_ID=)' /etc/os-release 2>/dev/null || echo 'ID=unknown'; "
            "uname -m; "
            "hostname",
        ],
        env=env,
        capture_output=True,
        text=True,
        check=True,
    )

    lines = result.stdout.strip().split("\n")
    os_id = "unknown"
    version_id = ""

    for line in lines:
        if line.startswith("ID="):
            os_id = line.split("=", 1)[1].strip('"')
        elif line.startswith("VERSION_ID="):
            version_id = line.split("=", 1)[1].strip('"')

    arch = lines[-2].strip() if len(lines) >= 2 else "unknown"
    hostname = lines[-1].strip() if lines else "unknown"

    if os_id != "unknown" and version_id:
        name = f"{os_id}-{version_id}-{arch}"
    elif os_id != "unknown":
        name = f"{os_id}-{arch}"
    else:
        name = f"{hostname}-{arch}"

    return name.replace(" ", "-").lower()


# ---------------------------------------------------------------------------
# Core runner: send project + script, execute, pull exports/
# ---------------------------------------------------------------------------

def run_on_vm(vm_config: dict, local_script: Path) -> dict:
    """
    Full lifecycle for a single VM:
      rsync project → rsync script → execute → pull exports/ → cleanup
    """
    host = vm_config.get("host", "")
    password = vm_config.get("password", "")

    # --- validate config ---
    if not host:
        return {"name": "unknown", "status": "failed", "error": "Host is not configured"}
    if not password:
        return {"name": host, "status": "failed", "error": "Password is not set"}

    env = {**os.environ, "SSHPASS": password}
    ssh_opts = ["-o", "StrictHostKeyChecking=no", "-o", "BatchMode=no"]

    def ssh(*remote_cmd_parts):
        return subprocess.run(
            ["sshpass", "-e", "ssh", *ssh_opts, host, *remote_cmd_parts],
            env=env, check=True, capture_output=True, text=True,
        )

    def rsync_to(local_src, remote_dst, excludes=None):
        cmd = [
            "sshpass", "-e", "rsync", "-a", "--info=progress2",
            "-e", "ssh " + " ".join(ssh_opts),
                  ]
        for ex in (excludes or []):
            cmd += ["--exclude", ex]
        cmd += [str(local_src), f"{host}:{remote_dst}"]
        subprocess.run(cmd, env=env, check=True)

    def rsync_from(remote_src, local_dst):
        cmd = [
            "sshpass", "-e", "rsync", "-a", "--info=progress2",
            "-e", "ssh " + " ".join(ssh_opts),
            f"{host}:{remote_src}", str(local_dst),
                  ]
        subprocess.run(cmd, env=env, check=True)

    # --- identity ---
    try:
        vm_name = get_vm_identity(host, password)
    except Exception as e:
        return {"name": host, "status": "failed", "error": f"Identity probe failed: {e}"}

    tmp_dir = f"/tmp/vm_runner_{uuid.uuid4().hex}"
    output_dir = PROJECT_ROOT / "builds" / vm_name

    print(f"[{vm_name}] Starting → tmp dir: {tmp_dir}")

    try:
        # 1. Create tmp dir on remote
        ssh(f"mkdir -p {tmp_dir}")

        # 2. Rsync project root into tmp dir
        print(f"[{vm_name}] Syncing project root...")
        rsync_to(
            str(PROJECT_ROOT) + "/",  # trailing slash = contents, not the directory itself
            tmp_dir,
            excludes=[
                ".git", ".build", ".env", "*.zip", "*.enc",
                "build", "cmake-build-*", "builds",
            ],
            )

        # 3. Rsync the script into tmp dir (explicit push so it's always current and executable)
        print(f"[{vm_name}] Sending script: {local_script.name}")
        rsync_to(local_script, tmp_dir + "/")

        # 4. Make script executable and run it (cwd = tmp_dir)
        # bash -e: any command failure exits immediately and propagates non-zero back to us
        print(f"[{vm_name}] Executing {local_script.name}...")
        result = ssh(
            f"chmod +x {tmp_dir}/{local_script.name} && "
            f"cd {tmp_dir} && "
            f"bash -e {tmp_dir}/{local_script.name}"
        )

        script_output = result.stdout + result.stderr

        # 5. Check exports/ exists
        check = subprocess.run(
            ["sshpass", "-e", "ssh", *ssh_opts, host,
             f"test -d {tmp_dir}/exports && echo yes || echo no"],
            env=env, capture_output=True, text=True,
        )
        if check.stdout.strip() != "yes":
            raise RuntimeError(
                f"Script finished but exports/ was not created at {tmp_dir}/exports/"
            )

        # 6. Pull exports/ back
        output_dir.mkdir(parents=True, exist_ok=True)
        print(f"[{vm_name}] Pulling exports/...")
        rsync_from(f"{tmp_dir}/exports/", str(output_dir) + "/")

        # 7. Save script stdout/stderr alongside exports
        (output_dir / "run.log").write_text(script_output)

        # 8. Cleanup tmp on success
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
        error = f"Command failed (exit {e.returncode})"
        if stdout:
            error += f"\nstdout: {stdout}"
        if stderr:
            error += f"\nstderr: {stderr}"
        print(f"[{vm_name}] ✗ Failed — tmp left at {tmp_dir} for debugging")
        return {"name": vm_name, "host": host, "status": "failed",
                "error": error, "tmp_dir": tmp_dir}

    except Exception as e:
        print(f"[{vm_name}] ✗ Failed — tmp left at {tmp_dir} for debugging")
        return {"name": vm_name, "host": host, "status": "failed",
                "error": f"{type(e).__name__}: {e}", "tmp_dir": tmp_dir}


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
        if not config.get("password"):
            print(f"✗ {config['host']}: Password not set, skipping.")
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
    failed     = [r for r in results if r["status"] == "failed"]

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