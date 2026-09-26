# Agent rules

- Project conventions (Svelte 5, ESP-IDF, WebSocket endpoints, deploy,
  live debugging): follow `LLM_GUIDE.md`.

- Credentials live in device NVS (set over USB provisioning or the portal),
  never in the tree: there is no secrets file. If you find one on disk it
  is a leftover from the old build-time flow — do not read it, suggest
  deleting it.
