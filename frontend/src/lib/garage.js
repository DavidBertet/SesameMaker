// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Pure helpers for the garage door UI and mock. No Svelte, no ws.

export const DOOR_STATES = ['unknown', 'open', 'closed', 'opening', 'closing', 'stopped']

export function doorMeta(state) {
  switch (state) {
    case 'open':
      return { label: 'Open', color: 'green', detail: 'Door is fully open' }
    case 'closed':
      return { label: 'Closed', color: 'gray', detail: 'Door is fully closed' }
    case 'opening':
      return { label: 'Opening', color: 'amber', detail: 'Door is moving up' }
    case 'closing':
      return {
        label: 'Closing',
        color: 'amber',
        detail: 'Door is moving down',
      }
    case 'stopped':
      return {
        label: 'Stopped',
        color: 'amber',
        detail: 'Door stopped mid-travel',
      }
    default:
      return {
        label: 'Unknown',
        color: 'gray',
        detail: 'Trigger a sync to learn the state',
      }
  }
}

export function isSettled(state) {
  return state === 'open' || state === 'closed' || state === 'stopped'
}

// Optimistic state helpers for the light / remote-lock buttons. Each returns
// the new optimistic value to show immediately while a command is in flight.
export function optimisticLight(action, pending) {
  if (action !== 'on' && action !== 'off') return pending
  return action
}

export function optimisticLock(action, pending) {
  return action === 'lock' ? 'locked' : 'unlocked'
}

export const PROTOCOLS = ['secplus1', 'drycontact', 'secplus2']

// Full Sec1 caps: assumed when an older firmware omits the caps object so
// existing units keep rendering light/lock/panel after an OTA mismatch.
export const FULL_CAPS = {
  light: true,
  lock: true,
  obstruction: true,
  motion: true,
  panel: true,
  sensors: false,
}

export const DRY_CAPS = {
  light: false,
  lock: false,
  obstruction: false,
  motion: false,
  panel: false,
  sensors: true,
}

// Normalize an incoming garage_status payload into the store shape.
export function parseGarageStatus(data = {}) {
  // Caps default by protocol when the backend omits them (old firmware =
  // full Sec1). The UI only ever reads caps, never the protocol id.
  const rawCaps = data.caps ?? (data.protocol === 'drycontact' ? DRY_CAPS : FULL_CAPS)
  const caps = { ...FULL_CAPS, ...rawCaps }
  return {
    protocol: PROTOCOLS.includes(data.protocol) ? data.protocol : 'secplus1',
    caps,
    door: DOOR_STATES.includes(data.door) ? data.door : 'unknown',
    moving: data.moving === true,
    light: ['on', 'off', 'unknown'].includes(data.light) ? data.light : 'unknown',
    locked: ['locked', 'unlocked', 'unknown'].includes(data.locked) ? data.locked : 'unknown',
    obstruction: data.obstruction === true,
    motion: data.motion === true,
    panel: ['waiting', 'detected', 'emulated', 'none'].includes(data.panel)
      ? data.panel
      : 'waiting',
    sensors: {
      open: data.sensors?.open === true,
      close: data.sensors?.close === true,
      valid: data.sensors?.valid === true,
    },
  }
}

// Mirror of the backend secplus1 byte names, for the protocol inspector.
const RX_NAMES = {
  0x30: 'TOGGLE_DOOR_PRESS',
  0x31: 'TOGGLE_DOOR_RELEASE',
  0x32: 'TOGGLE_LIGHT_PRESS / MOTION',
  0x33: 'TOGGLE_LIGHT_RELEASE',
  0x34: 'TOGGLE_LOCK_PRESS',
  0x35: 'TOGGLE_LOCK_RELEASE',
  0x37: 'QUERY_DOOR_STATUS_0X37',
  0x38: 'QUERY_DOOR_STATUS',
  0x39: 'OBSTRUCTION',
  0x3a: 'QUERY_OTHER_STATUS',
}

export function decodeByte(byte) {
  return RX_NAMES[byte] ?? null
}

// Decode the payload byte of a 0x38 reply (door status).
export function decodeDoorResp(resp) {
  switch (resp & 0x7) {
    case 0x2:
      return 'open'
    case 0x5:
      return 'closed'
    case 0x0:
    case 0x6:
      return 'stopped'
    case 0x1:
      return 'opening'
    case 0x4:
      return 'closing'
    default:
      return 'unknown'
  }
}

const TRAVEL_MS = 4000

// Plan the animated status frames a mock opener produces for a command.
// Returns [{delay, door, moving}]; empty when the command is a no-op.
export function planDoorTransition(state, action) {
  if (action === 'toggle') {
    if (state === 'closed' || state === 'unknown')
      return [
        { delay: 0, door: 'opening', moving: true },
        { delay: TRAVEL_MS, door: 'open', moving: false },
      ]
    if (state === 'open')
      return [
        { delay: 0, door: 'closing', moving: true },
        { delay: TRAVEL_MS, door: 'closed', moving: false },
      ]
    if (state === 'stopped')
      return [
        { delay: 0, door: 'closing', moving: true },
        { delay: TRAVEL_MS, door: 'closed', moving: false },
      ]
    if (state === 'opening') return [{ delay: 0, door: 'stopped', moving: false }]
    if (state === 'closing') return [{ delay: 0, door: 'open', moving: false }]
    return []
  }
  if (action === 'open') {
    if (state === 'open' || state === 'opening') return []
    return [
      { delay: 0, door: 'opening', moving: true },
      { delay: TRAVEL_MS, door: 'open', moving: false },
    ]
  }
  if (action === 'close') {
    if (state === 'closed' || state === 'closing') return []
    return [
      { delay: 0, door: 'closing', moving: true },
      { delay: TRAVEL_MS, door: 'closed', moving: false },
    ]
  }
  if (action === 'stop') {
    if (!['opening', 'closing'].includes(state)) return []
    return [{ delay: 0, door: 'stopped', moving: false }]
  }
  return []
}
