// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { onMessageType, sendMessage } from 'src/lib/ws.svelte.js'
import { parseGarageStatus, optimisticLight, optimisticLock } from 'src/lib/garage.js'

export const garageState = $state({
  door: 'unknown',
  moving: false,
  light: 'unknown',
  locked: 'unknown',
  obstruction: false,
  motion: false,
  panel: 'waiting',
})

export const pendingCommands = $state({
  light: null,
  lock: null,
  sync: false,
})

export function initializeGarage() {
  sendMessage({ type: 'get_garage_status' })

  const unsub = onMessageType('garage_status', (data) => {
    Object.assign(garageState, parseGarageStatus(data))
    pendingCommands.light = null
    pendingCommands.lock = null
    pendingCommands.sync = false
  })
  return unsub
}

export function doorCommand(action) {
  sendMessage({ type: 'door_command', action })
}

export function lightCommand(action) {
  pendingCommands.light = action
  garageState.light = optimisticLight(action, garageState.light)
  sendMessage({ type: 'light_command', action })
}

export function lockCommand(action) {
  pendingCommands.lock = action
  garageState.locked = optimisticLock(action, garageState.locked)
  sendMessage({ type: 'lock_command', action })
}

export function syncGarage() {
  pendingCommands.sync = true
  sendMessage({ type: 'garage_sync' })
}

export function fetchGarageRaw() {
  sendMessage({ type: 'get_garage_raw' })
}

export function onGarageRaw(callback) {
  return onMessageType('garage_raw', callback)
}
