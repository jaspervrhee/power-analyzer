#!/usr/bin/env python3
"""
Build and run the power-analyzer C++ project
"""

import subprocess
import sys
import os
import signal
from pathlib import Path

def run_command(cmd, description):
    """Execute a command and report status"""
    print(f"\n{'='*60}")
    print(f"[*] {description}")
    print(f"{'='*60}")
    print(f"Command: {' '.join(cmd)}\n")
    
    try:
        result = subprocess.run(cmd, check=True)
        return result.returncode == 0
    except subprocess.CalledProcessError as e:
        print(f"\n[ERROR] {description} failed with exit code {e.returncode}")
        return False
    except FileNotFoundError as e:
        print(f"\n[ERROR] Command not found: {e}")
        return False


def run_and_forward(cmd, description):
    """Run a command and forward SIGINT to the child process (graceful Ctrl-C)."""
    print(f"\n{'='*60}")
    print(f"[*] {description}")
    print(f"{'='*60}")
    print(f"Command: {' '.join(cmd)}\n")

    try:
        # Start child in its own process group so we can forward signals.
        proc = subprocess.Popen(cmd, preexec_fn=os.setsid)
    except FileNotFoundError as e:
        print(f"\n[ERROR] Command not found: {e}")
        return False

    try:
        proc.wait()
        return proc.returncode == 0
    except KeyboardInterrupt:
        print("\n[INFO] Interrupted: forwarding SIGINT to child process...")
        try:
            os.killpg(proc.pid, signal.SIGINT)
        except Exception:
            pass
        proc.wait()
        return False

def main():
    # Get project root
    project_root = Path(__file__).parent
    cpp_dir = project_root / "src" / "cpp"
    
    if not cpp_dir.exists():
        print(f"[ERROR] C++ directory not found: {cpp_dir}")
        sys.exit(1)
    
    os.chdir(cpp_dir)
    print(f"Working directory: {os.getcwd()}")
    
    # Collect all .cpp source files under src/cpp (exclude build/)
    cpp_files = []
    for p in sorted(cpp_dir.rglob('*.cpp')):
        rel = p.relative_to(cpp_dir)
        if 'build' in rel.parts:
            continue
        cpp_files.append(str(rel))

    if not cpp_files:
        print("[ERROR] No .cpp files found to compile")
        sys.exit(1)

    # Build command
    build_cmd = [
        "g++",
        "-std=c++17",
        "-I.",
        "-pthread",
        "-g",
        "-o", "power_analyzer",
    ] + cpp_files + ["-lgpiod"]
    
    # Build
    if not run_command(build_cmd, "Building C++ project"):
        sys.exit(1)
    
    # Run (pass 5 seconds runtime by default)
    if not run_and_forward(["./power_analyzer", "0"], "Running power_analyzer"):
        sys.exit(1)
    
    print(f"\n{'='*60}")
    print("[✓] Success!")
    print(f"{'='*60}\n")

if __name__ == "__main__":
    main()
