# LLM Guide

# Generic rules that must always be applied

## Frontend

- Write valid Svelte 5 code
  - Use runes like $state, $props. Don't import them
  - Remove colon when necessary, ex: `onclick` instead of `on:click`
- You must use shadcn-ui to create a slick and modern UI
- Make sure you use the right imports for Svelte & Shadcn-UI

## Backend

- You must use ESP-IDF to access hardware
- Use #pragma once for header files

# Adding a new tab

## Frontend

- Add new tab in `frontend/src/components/tab`
- You must take example on `frontend/src/components/tab/SystemTab.svelte` for the file structure
- A table must have a `SectionHeader` to expose tab context
- Declare tab in `const tabs` on `frontend/src/App.svelte`. You must add it at the top

# Adding a WebSocket Endpoint

## Frontend

- Mock: Add a case for `<endpoint>` in `generateMockResponse(data)` (`frontend/src/lib/mockdata.js`). Returns a list, even if one response
  In any Svelte file, import `import { sendMessage, onMessageType } from 'src/lib/ws.svelte.js'`, then
- Send: `sendMessage({ type: '<endpoint>', ...payload });`
- Listen: Use `onMessageType('<response_type>', callback)` to consume responses

## Backend

- You can group related endpoints (ex: write/read) in the same file
- Handler: Implement `void ws_handle_<endpoint>(const cJSON *root, int sockfd);` in `backend/src/ws_<endpoint_context>.c/.h`. Take example on `backend/src/ws_settings.c`
- Register: Add `register_callback("<endpoint>", ws_handle_<endpoint>);` in `app_main()` (`backend/src/main.c`). Don't forget the include
- Respond: Use `send_message_sockfd(char* json, sockfd);` (single user), `broadcast_message(char* json);` (all users), or `send_message_token(char* json, token);` (by token). Token is used to target a user from an http request.

## Example

```js
import { sendMessage, onMessageType } from "src/lib/ws.svelte.js";
// Send
sendMessage({ type: "<endpoint>", ...payload });
// Listen
const unsub = onMessageType("<response_type>", (data) => {
  /* handle */
});
```

## Summary

| Step     | File(s)                                 | Action                                                       |
| -------- | --------------------------------------- | ------------------------------------------------------------ |
| Handler  | backend/src/ws\_<endpoint_context>.c/.h | ws*handle*<endpoint>(...)                                    |
| Register | backend/src/main.c                      | register_callback(...)                                       |
| Respond  | backend/src/websocket.c                 | send_message_sockfd / broadcast_message / send_message_token |
| Mock     | frontend/src/lib/mockdata.js            | generateMockResponse                                         |
| Send     | frontend/src/lib/ws.svelte.js           | sendMessage                                                  |
| Listen   | frontend/src/lib/ws.svelte.js           | onMessageType                                                |

# Deploying to the device

The device IP is **not fixed** (DHCP) - the agent must ask the user for the current IP before any deploy or log session, never guess or reuse a stale one.

The agent may push changes to a device over OTA using `./install.sh` (put the user-provided IP in place of `<IP>`):

- Full deploy (firmware + web files): `./install.sh -o <IP> -y -p <password>`
- Backend only - firmware, the common case for backend/log changes: `./install.sh -o <IP> -y -b -p <password>`
- Frontend only - web files: `./install.sh -o <IP> -y -f -p <password>`

Flags:

- `-o, --ota <IP>` - target device
- `-p, --upload-password <PWD>` - OTA password (default `OTA_PASSWORD` in `backend/src/constants.h`)
- `-y, --yes` - auto-confirm the upload prompts (required for non-interactive agent use)
- `-b, --backend-only` / `-f, --frontend-only` - target a single component

The CLI builds first (PlatformIO for firmware, Vite for web) then uploads over HTTP. A firmware upload makes the device reboot - wait a few seconds before connecting to it.

# Debugging against the live device

Static analysis cannot always explain timing / bus / state-machine issues (wire protocol, task interleaving, broadcast behavior). When it cannot, work in a tight loop:

1. **Add logs**: `ESP_LOGI/ESP_LOGW` at the point under suspicion - log the concrete values with enough context to reason later.
2. **Push**: `./install.sh -o <IP-from-user> -y -b -p <password>` for backend changes (add `-f` or drop `-b` for frontend).
3. **Read the live logs**: `node tools/ws-logs.cjs <IP-from-user>`
   - Omitting the IP makes the script prompt for it.
   - `--tag <TAG>` - only lines containing the tag (e.g. `GARAGE_CTRL`)
   - `--all` - also dump non-log frames (`garage_status`, `garage_raw`, ...)
   - `--timeout N` - exit after N seconds
   - The device streams its ESP-IDF log output over the debug websocket; prefix is the client reception time.
4. **Correlate** with the Protocol tab on the web UI (raw bus traffic) and repeat.

This instrument → deploy → read-logs loop is the intended debugging method for issues that cannot be proven statically. Note: forwarded log lines are sanitized to printable ASCII by the backend (`ws_log.c`), so binary/control characters do not appear. ESP-IDF's own per-send debug logs (`websocket: Send message to ...`) are expected noise.
