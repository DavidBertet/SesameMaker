// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { onMessageType, sendMessage } from 'src/core/lib/ws.svelte.js'
import {
  parseGarageStatus,
  optimisticLight,
  optimisticLock,
  FULL_CAPS,
} from 'src/app/lib/garage.js'

export const garageState = $state({
  protocol: 'secplus1',
  caps: { ...FULL_CAPS },
  door: 'unknown',
  moving: false,
  light: 'unknown',
  locked: 'unknown',
  obstruction: false,
  motion: false,
  panel: 'waiting',
  sensors: { open: false, close: false, valid: false },
})

export const protocolState = $state({
  id: 'secplus1',
  supported: true,
  caps: { ...FULL_CAPS },
  loaded: false,
})

export const dryCfg = $state({
  relay_gpio: 5,
  open_gpio: 17,
  close_gpio: 18,
  sensor_mode: 2,
  active_low: true,
  pulse_ms: 500,
  debounce_ms: 200,
  travel_s: 15,
})

export const pendingCommands = $state({
  light: null,
  lock: null,
  sync: false,
  protocol: null,
})

export function initializeGarage() {
  sendMessage({ type: 'get_garage_status' })
  sendMessage({ type: 'get_protocol' })

  const unsubs = [
    onMessageType('garage_status', (data) => {
      Object.assign(garageState, parseGarageStatus(data))
      pendingCommands.light = null
      pendingCommands.lock = null
      pendingCommands.sync = false
    }),
    onMessageType('protocol', (data) => {
      if (typeof data.id === 'string') protocolState.id = data.id
      protocolState.supported = data.supported !== false
      if (data.caps) protocolState.caps = { ...FULL_CAPS, ...data.caps }
      // Dry settings ride along on the same message (single save path).
      if (data.dry) Object.assign(dryCfg, data.dry)
      protocolState.loaded = true
      pendingCommands.protocol = null
    }),
  ]
  return () => unsubs.forEach((u) => u())
}

export function doorCommand(action) {
  sendMessage({ type: 'door_command', action })
}

export function lightCommand(action) {
  if (!garageState.caps.light) return
  pendingCommands.light = action
  garageState.light = optimisticLight(action, garageState.light)
  sendMessage({ type: 'light_command', action })
}

export function lockCommand(action) {
  if (!garageState.caps.lock) return
  pendingCommands.lock = action
  garageState.locked = optimisticLock(action, garageState.locked)
  sendMessage({ type: 'lock_command', action })
}

export function syncGarage() {
  pendingCommands.sync = true
  sendMessage({ type: 'garage_sync' })
}

export function setProtocol(id) {
  pendingCommands.protocol = id
  sendMessage({ type: 'set_protocol', id })
}

// Single save path: protocol id + dry settings in one call. The backend
// echoes the applied state on the `protocol` message.
export function saveProtocol(id, dry) {
  pendingCommands.protocol = id
  sendMessage({ type: 'set_protocol', id, dry })
}

export function fetchGarageRaw() {
  sendMessage({ type: 'get_garage_raw' })
}

export function onGarageRaw(callback) {
  return onMessageType('garage_raw', callback)
}
