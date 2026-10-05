// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Which messages may NEVER be queued while the WebSocket is down and replayed
// on reconnect. Everything else queues by default so a page refresh during a
// disconnect recovers (fetches like get_settings must not be lost).
//
// Two layers, because core cannot know what a product actuates:
//  - The fixed set below is transport-level and identical for every product.
//    WiFi state changes are already applied by the device, so a stale replay
//    would silently connect/disconnect a network the user didn't ask for now.
//  - Each app registers its own physical actuation commands (door/light/lock,
//    motor run, ...) through registerNonQueuableTypes. Replaying one of those
//    would move hardware by itself after a controller restart.
const NON_QUEUABLE_TYPES = new Set(['wifi_connect', 'wifi_disconnect'])

// Register the calling app's physical actuation commands. Call once at app
// startup, before the first send. Idempotent and additive, so re-registering
// (hot reload, repeated mount) is harmless.
export function registerNonQueuableTypes(types) {
  for (const type of types || []) {
    if (type) NON_QUEUABLE_TYPES.add(type)
  }
}

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
