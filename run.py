#!/usr/bin/env python3
"""
Build and run the power-analyzer C++ project using CMake
"""

import subprocess
import sys
import os
import signal
import platform
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
        if platform.system() == "Windows":
            # On Windows, just create the process normally
            proc = subprocess.Popen(cmd)
        else:
            # On Unix/Linux, create a new process group for proper signal forwarding
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
            if platform.system() == "Windows":
                proc.send_signal(signal.CTRL_C_EVENT)
            else:
                os.killpg(proc.pid, signal.SIGINT)
        except Exception:
            pass
        proc.wait()
        return False


def main():
    # Get project root
    project_root = Path(__file__).parent
    cpp_dir = project_root / "src" / "cpp"
    build_dir = cpp_dir / "build"

    if not cpp_dir.exists():
        print(f"[ERROR] C++ directory not found: {cpp_dir}")
        sys.exit(1)

    print(f"Project root: {project_root}")
    print(f"C++ source dir: {cpp_dir}")
    print(f"Build dir: {build_dir}")

    # Clean and prepare build directory
    if build_dir.exists():
        print(f"\n[*] Cleaning build directory...")
        import shutil
        shutil.rmtree(build_dir)

    build_dir.mkdir(parents=True, exist_ok=True)

    # Determine CMake generator based on platform
    generator = None
    if platform.system() == "Windows":
        generator = "Visual Studio 17 2022"
    else:
        generator = "Unix Makefiles"

    # Configure with CMake
    config_cmd = [
        "cmake",
        "-G", generator,
        "-DCMAKE_BUILD_TYPE=Release",
        "-B", str(build_dir),
        "-S", str(cpp_dir)
    ]

    if not run_command(config_cmd, "Configuring with CMake"):
        sys.exit(1)

    # Build with CMake
    build_cmd = ["cmake", "--build", str(build_dir), "--config", "Release"]
    if not run_command(build_cmd, "Building with CMake"):
        sys.exit(1)

    # Determine executable path
    if platform.system() == "Windows":
        exe_path = build_dir / "Release" / "power_analyzer.exe"
    else:
        exe_path = build_dir / "power_analyzer"

    if not exe_path.exists():
        print(f"[ERROR] Executable not found at {exe_path}")
        sys.exit(1)

    # Parse optional command-line arguments to pass to the program
    program_args = sys.argv[1:] if len(sys.argv) > 1 else ["--run-seconds", "0"]
    run_cmd = [str(exe_path)] + program_args

    # Run the program
    if not run_and_forward(run_cmd, "Running power_analyzer"):
        sys.exit(1)

    print(f"\n{'='*60}")
    print("[✓] Success!")
    print(f"{'='*60}\n")


if __name__ == "__main__":
    main()
