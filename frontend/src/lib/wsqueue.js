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

// Collapse a reconnect backlog to the last message of each type, ordered by
// last occurrence. A 2 s poller (get_garage_raw) queues dozens of identical
// reads during an outage; replaying all of them floods the backend, while
// only the newest one matters. Actuation commands never reach the queue
// (blocklist above), so collapsing reads here can't replay a stale toggle.
export function drainQueue(queue) {
  const lastIndex = new Map()
  queue.forEach((msg, i) => {
    if (msg && msg.type) lastIndex.set(msg.type, i)
  })
  return [...lastIndex.entries()].sort((a, b) => a[1] - b[1]).map(([, i]) => queue[i])
}
