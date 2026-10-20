"""Compatibility path; use tools/data/build_runtime.py with explicit inputs and outputs."""
from pathlib import Path
import runpy
import sys

if __name__ == "__main__":
    target=Path(__file__).resolve().parents[3]/'tools/data/build_runtime.py'
    sys.path.insert(0,str(target.parent))
    runpy.run_path(str(target),run_name="__main__")
