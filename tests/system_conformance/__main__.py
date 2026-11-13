"""python3 tests/system_conformance [system ...]"""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).with_name('check.py')), run_name='__main__')
