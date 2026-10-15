"""Private synthetic XA fixtures for the host factory integration; no game data."""
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="odh-xa-sound-") as folder:
    subprocess.run([sys.argv[1], folder], check=True)
