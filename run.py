#!/usr/bin/env python3
"""
Build and run the power-analyzer C++ project
"""

import subprocess
import sys
import os
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

def main():
    # Get project root
    project_root = Path(__file__).parent
    cpp_dir = project_root / "src" / "cpp"
    
    if not cpp_dir.exists():
        print(f"[ERROR] C++ directory not found: {cpp_dir}")
        sys.exit(1)
    
    os.chdir(cpp_dir)
    print(f"Working directory: {os.getcwd()}")
    
    # Build command
    build_cmd = [
        "g++",
        "-std=c++17",
        "-I.",
        "-pthread",
        "-g",
        "-o", "power_analyzer",
        "main.cpp",
        "lld/ModbusRtuSerial.cpp",
        "lld/IEM3250LLD.cpp",
        "hld/MeterHLD.cpp",
        "controller/Controller.cpp"
    ]
    
    # Build
    if not run_command(build_cmd, "Building C++ project"):
        sys.exit(1)
    
    # Run
    if not run_command(["./power_analyzer"], "Running power_analyzer"):
        sys.exit(1)
    
    print(f"\n{'='*60}")
    print("[✓] Success!")
    print(f"{'='*60}\n")

if __name__ == "__main__":
    main()
