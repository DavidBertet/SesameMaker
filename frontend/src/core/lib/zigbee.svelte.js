// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { onMessageType, sendMessage } from 'src/core/lib/ws.svelte.js'

// Zigbee bridge state (C6 builds only; hidden otherwise via settings.features)
export const zigbeeState = $state({
  config: {
    supported: false,
    enabled: false,
    joined: false,
    commissioned: false,
    channel: 0,
    channel_cfg: 0,
    pan_id: 0,
    pairing_remaining_s: 0,
    lqi: 0,
    lqi_valid: false,
    parent_addr: 0,
    parent_depth: 0,
  },
})

function applyConfig(data) {
  const c = zigbeeState.config
  if (typeof data.supported === 'boolean') c.supported = data.supported
  if (typeof data.enabled === 'boolean') c.enabled = data.enabled
  if (typeof data.joined === 'boolean') c.joined = data.joined
  if (typeof data.commissioned === 'boolean') c.commissioned = data.commissioned
  if (typeof data.channel === 'number') c.channel = data.channel
  if (typeof data.channel_cfg === 'number') c.channel_cfg = data.channel_cfg
  if (typeof data.pan_id === 'number') c.pan_id = data.pan_id
  if (typeof data.pairing_remaining_s === 'number') c.pairing_remaining_s = data.pairing_remaining_s
  if (typeof data.lqi === 'number') c.lqi = data.lqi
  if (typeof data.lqi_valid === 'boolean') c.lqi_valid = data.lqi_valid
  if (typeof data.parent_addr === 'number') c.parent_addr = data.parent_addr
  if (typeof data.parent_depth === 'number') c.parent_depth = data.parent_depth
}

export function initializeZigbee() {
  sendMessage({ type: 'get_zigbee_config' })
  return onMessageType('zigbee_config', applyConfig)
}

export function saveZigbeeConfig(enabled) {
  sendMessage({ type: 'set_zigbee_config', enabled })
}

// Pin the Zigbee scan channel (0 = auto/all, 11-26 = single channel).
// Sends only channel_cfg so the backend leaves the enabled flag untouched.
export function saveZigbeeChannel(channel_cfg) {
  sendMessage({ type: 'set_zigbee_config', channel_cfg })
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
