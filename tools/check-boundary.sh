#!/bin/sh
# Boundary test for the core/app split (Phase 1).
# core/ must never depend on app/: the reusable core has to build
# standalone for other projects (e.g. the air-quality sensor).
# Fails with the offending lines so CI can gate on it.
set -e
cd "$(dirname "$0")/.."
fail=0
echo "--- backend: core must not include app headers"
if grep -rn '#include "\(garage\|protocol\|zigbee\.h\|ws_zigbee\|mqtt_garage\|secplus1\|drycontact\|raw_json\)' backend/src/core/; then
  fail=1
else
  echo "backend core: clean"
fi
echo "--- frontend: core must not import app modules"
if grep -rn "src/app/" frontend/src/core/; then
  fail=1
else
  echo "frontend core: clean"
fi
if [ "$fail" -ne 0 ]; then
  echo "BOUNDARY VIOLATION"
  exit 1
fi
echo "boundary OK"
