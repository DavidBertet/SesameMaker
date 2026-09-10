// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { onMessageType, sendMessage } from 'src/lib/ws.svelte.js'

// Zigbee bridge state (C6 builds only; hidden otherwise via settings.features)
export const zigbeeState = $state({
  config: {
    supported: false,
    enabled: false,
    joined: false,
    channel: 0,
    pan_id: 0,
    pairing_remaining_s: 0,
  },
})

function applyConfig(data) {
  const c = zigbeeState.config
  if (typeof data.supported === 'boolean') c.supported = data.supported
  if (typeof data.enabled === 'boolean') c.enabled = data.enabled
  if (typeof data.joined === 'boolean') c.joined = data.joined
  if (typeof data.channel === 'number') c.channel = data.channel
  if (typeof data.pan_id === 'number') c.pan_id = data.pan_id
  if (typeof data.pairing_remaining_s === 'number')
    c.pairing_remaining_s = data.pairing_remaining_s
}

export function initializeZigbee() {
  sendMessage({ type: 'get_zigbee_config' })
  return onMessageType('zigbee_config', applyConfig)
}

export function saveZigbeeConfig(enabled) {
  sendMessage({ type: 'set_zigbee_config', enabled })
}

export function zigbeePair(duration_s = 60) {
  sendMessage({ type: 'zigbee_pair', duration_s })
}

export function zigbeeLeave() {
  sendMessage({ type: 'zigbee_leave' })
}

export function zigbeeReset() {
  sendMessage({ type: 'zigbee_reset' })
}
