import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="odh-native-patch-pcm-") as root:
    subprocess.run([sys.argv[1], root], check=True)
