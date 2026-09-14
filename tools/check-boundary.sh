#!/bin/sh
# Boundary test for the core/app split (Phase 1).
# core/ must never depend on app/: the reusable core has to build
# standalone for other projects (e.g. the air-quality sensor).
# Backend check is structural: every quoted #include in core must NOT
# resolve to a header that lives in app/ (self-includes and IDF headers
# are fine). Frontend check: no src/app/ imports inside src/core/.
# Fails with the offending lines so CI can gate on it.
set -e
cd "$(dirname "$0")/.."
fail=0
echo "--- backend: core must not include app headers"
violations=$(for f in backend/src/core/*.[ch]; do
  grep -o '#include "[^"]*"' "$f" | sed 's/#include "//;s/"//' | while read -r inc; do
    if [ -f "backend/src/app/$inc" ]; then
      echo "$f -> $inc"
    fi
  done
done)
if [ -n "$violations" ]; then
  echo "$violations"
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
