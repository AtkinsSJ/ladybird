python
import sys
from pathlib import Path

gdb_scripts_dir = Path(__file__).parent / "Meta" / "gdb" if "__file__" in dir() else None
print(f"Looking for GDB scripts in {gdb_scripts_dir}")
if gdb_scripts_dir is None:
    import os
    gdb_scripts_dir = Path(os.getcwd()) / "Meta" / "gdb"

init_script = gdb_scripts_dir / "init.py"
if init_script.exists():
    print(f"Loading GDB init script from {init_script}")
    sys.path.insert(0, str(gdb_scripts_dir))
    import init
else:
    print(f"Warning: Could not find GDB init script at {init_script}")
end
