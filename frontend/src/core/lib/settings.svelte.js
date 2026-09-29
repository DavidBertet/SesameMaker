import { onMessageType, sendMessage } from 'src/core/lib/ws.svelte.js'

// Settings state
export const settingsState = $state({})

// Initialize settings
export function initializeSettings() {
  // Request settings from server
  sendMessage({ type: 'get_settings' })

  // Handle settings response
  const unsubscribe = onMessageType('settings', (data) => {
    settingsState.ota = data.ota
    settingsState.wifi = data.wifi
    settingsState.time = data.time
    settingsState.features = data.features || { zigbee: false }
  })

  return unsubscribe
}

// Generic partial writer for the common settings endpoint
// (e.g. saveSettings({ time: { ntp_server, tz } })). The backend acks
// 'settings_saved' and broadcasts fresh 'settings'.
export function saveSettings(payload) {
  sendMessage({ type: 'set_settings', ...payload })
}
