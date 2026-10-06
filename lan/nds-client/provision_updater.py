"""Optional one-time per-card updater seed, for installation without a LAN hub."""
import os
from pathlib import Path
import sys

card=Path(sys.argv[1]).resolve()
target=card/'openhome-updater.seed'
if target.exists():
    raise SystemExit('Updater seed already exists; preserving it.')
with target.open('xb') as stream:
    stream.write(os.urandom(32))
    stream.flush()
    os.fsync(stream.fileno())
print('Initialized private updater state on this card. Do not share the seed file.')
