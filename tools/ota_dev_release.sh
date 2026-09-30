#!/bin/sh
# Copyright (c) 2026 David Bertet. Licensed under the MIT License.
#
# Local end-to-end test for on-device OTA without touching GitHub.
# Mirrors .github/workflows/release.yml for ONE chip, then serves the result
# over plain HTTP on the LAN:
#
#   ./tools/ota_dev_release.sh [sesame-cli|sesame-c6|sesame-c3|sesame-s3]
#
# Then flash a build with the test trust anchors and point the device at it:
#
#   export OTA_RELEASE_BASE="http://<laptop-lan-ip>:8000"
#   export OTA_PUBKEY_HEX="<hex>"  # see below
#   ./install.sh -y -b -s
#
# The public key for OTA_PUBKEY_HEX comes from the dev keypair this script
# mints (uncompressed P-256 point, hex):
#
#   python3 -c "
#   from cryptography.hazmat.primitives import serialization
#   from cryptography.hazmat.primitives.asymmetric import ec
#   k = serialization.load_pem_private_key(open('build/ota-dev/key.pem','rb').read(), None)
#   print(k.public_key().public_bytes(serialization.Encoding.X962,
#         serialization.PublicFormat.UncompressedPoint).hex())"
#
# Everything lands in build/ota-dev/ (gitignored). The device under test
# must reach the laptop over WiFi (same LAN, no client isolation).
set -e
cd "$(dirname "$0")/.."

ENV="${1:-sesame-cli}"
OUT="build/ota-dev"
PORT="${OTA_DEV_PORT:-8000}"

# Same lookup as the CLI: PATH first, then the PlatformIO installer location.
if command -v pio >/dev/null 2>&1; then
  PIO=pio
elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
  PIO="$HOME/.platformio/penv/bin/pio"
else
  echo "pio not found (install PlatformIO or add it to PATH)" >&2
  exit 1
fi

case "$ENV" in
  sesame-cli|sesame-c6) CHIP=esp32c6; MANIFEST_CHIP="ESP32-C6" ;;
  sesame-c3) CHIP=esp32c3; MANIFEST_CHIP="ESP32-C3" ;;
  sesame-s3) CHIP=esp32s3; MANIFEST_CHIP="ESP32-S3" ;;
  *) echo "unknown env: $ENV (want sesame-cli|sesame-c6|sesame-c3|sesame-s3)" >&2; exit 1 ;;
esac
PREFIX="sesamemaker-${CHIP}-dev"
MANIFEST="sesamemaker-${CHIP}-manifest.json"

mkdir -p "$OUT"
echo "--- frontend into backend/data"
(cd frontend && npm run build >/dev/null)
echo "--- firmware ($ENV)"
(cd backend && "$PIO" run -e "$ENV" 2>&1 | tail -2)
echo "--- spiffs image"
(cd backend && "$PIO" run -t buildfs -e "$ENV" 2>&1 | tail -2)
B="backend/.pio/build/$ENV"
cp "$B/firmware.bin" "$OUT/${PREFIX}-app.bin"
cp "$B/spiffs.bin" "$OUT/${PREFIX}-spiffs.bin"

echo "--- manifest + dev signature"
(
  cd "$OUT"
  PREFIX="$PREFIX" MANIFEST="$MANIFEST" MANIFEST_CHIP="$MANIFEST_CHIP" python3 - <<'EOF'
import hashlib, json, os
prefix = os.environ["PREFIX"]
def entry(name, offset):
    h = hashlib.sha256()
    with open(name, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return {"name": name, "offset": offset, "size": os.path.getsize(name),
            "sha256": h.hexdigest()}
manifest = {
    "version": "v99.99.99-dev",
    "chip": os.environ["MANIFEST_CHIP"],
    "files": [entry(f"{prefix}-spiffs.bin", "0x37C000")],
    "app": entry(f"{prefix}-app.bin", "ota"),
}
with open(os.environ["MANIFEST"], "w") as f:
    json.dump(manifest, f, indent=2)
print(json.dumps(manifest, indent=2))
EOF
)
if [ ! -f "$OUT/key.pem" ]; then
  # cryptography lives in a throwaway venv (system pythons routinely forbid it).
  if ! python3 -c "import cryptography" >/dev/null 2>&1; then
    python3 -m venv "$OUT/.venv"
    "$OUT/.venv/bin/pip" -q install cryptography
  fi
  PY="$OUT/.venv/bin/python"
  [ -x "$PY" ] || PY=python3
  "$PY" tools/sign_manifest.py genkey --out "$OUT/key.pem" --pubkey-out "$OUT/key.pem.pub"
else
  PY="$OUT/.venv/bin/python"
  [ -x "$PY" ] || PY=python3
fi
"$PY" tools/sign_manifest.py sign --manifest "$OUT/$MANIFEST" --key "$OUT/key.pem" --key-id 0
"$PY" tools/sign_manifest.py verify --manifest "$OUT/$MANIFEST" --pubkey "$OUT/key.pem.pub"

IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo "<laptop-lan-ip>")
echo "--- serving $OUT on :$PORT"
echo "device manifest URL: http://$IP:$PORT/$MANIFEST"
(cd "$OUT" && python3 -m http.server "$PORT")
