# Agent rules

- Project conventions (Svelte 5, ESP-IDF, WebSocket endpoints, deploy,
  live debugging): follow `LLM_GUIDE.md`.

- NEVER read `backend/src/secrets.h`. It holds real credentials (WiFi,
  OTA password). You do not need its contents for any task: treat the
  `DEFAULT_WIFI_*` / `OTA_PASSWORD` placeholders in
  `backend/src/constants.h` as the values. If debugging requires knowing
  whether secrets exist, only check file presence via the shell
  (e.g. `test -f`), never `cat` / read its contents.
