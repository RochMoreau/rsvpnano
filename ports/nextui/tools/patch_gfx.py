"""Apply the firmware's Arduino_GFX u8g2 signed-coordinate patch (tools/pio_gfx.py) for host builds."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from pio_gfx import patch_u8g2_coordinates  # noqa: E402

source, destination = map(Path, sys.argv[1:3])
patched = patch_u8g2_coordinates(source.read_text(encoding="utf-8"))
destination.parent.mkdir(parents=True, exist_ok=True)
if not destination.exists() or destination.read_text(encoding="utf-8") != patched:
    destination.write_text(patched, encoding="utf-8")
