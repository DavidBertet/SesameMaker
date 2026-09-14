// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { onMessageType, sendMessage } from 'src/core/lib/ws.svelte.js'

// HomeKit accessory state (any WiFi build; hidden otherwise via settings)
export const homekitState = $state({
  config: {
    supported: true,
    started: false,
    paired: false,
    setup_uri: '',
  },
})

function applyConfig(data) {
  const c = homekitState.config
  if (typeof data.supported === 'boolean') c.supported = data.supported
  if (typeof data.started === 'boolean') c.started = data.started
  if (typeof data.paired === 'boolean') c.paired = data.paired
  // setup_uri only arrives while unpaired; a reply without it clears a
  // stale code (e.g. just got paired elsewhere).
  if (typeof data.setup_uri === 'string') c.setup_uri = data.setup_uri
  else if (c.paired) c.setup_uri = ''
}

export function initializeHomekit() {
  sendMessage({ type: 'get_homekit' })
  return onMessageType('homekit_config', applyConfig)
}

export function refreshHomekit() {
  sendMessage({ type: 'get_homekit' })
}
