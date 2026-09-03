// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Which messages may NEVER be queued while the WebSocket is down and replayed
// on reconnect. Everything else queues by default so a page refresh during a
// disconnect recovers (fetches like get_settings / get_mqtt_config must not be
// lost). The blocklist is intentionally tiny: physical actuation commands
// (door/light/lock) must never be replayed - a stale queued "toggle" would
// open the garage door by itself after a controller restart - and WiFi state
// changes are already applied by the device, so a stale replay would silently
// connect/disconnect a network the user didn't ask for right now.
const NON_QUEUABLE_TYPES = new Set([
  'door_command',
  'light_command',
  'lock_command',
  'wifi_connect',
  'wifi_disconnect',
])

export function shouldQueueMessage(msg) {
  return Boolean(msg && msg.type && !NON_QUEUABLE_TYPES.has(msg.type))
}
