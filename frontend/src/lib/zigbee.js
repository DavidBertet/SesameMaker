// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Pure Zigbee helpers (no Svelte runes, unit tested under node:test).

export function zigbeeSupported(settings) {
  return Boolean(settings?.features?.zigbee && settings?.features?.zigbee !== false)
}

// Status badge meta for the Zigbee card.
export function zigbeeStatusMeta(cfg) {
  if (!cfg || cfg.supported === false) return { label: 'Unavailable', variant: 'outline' }
  if (!cfg.enabled) return { label: 'Disabled', variant: 'outline' }
  if (cfg.pairing_remaining_s > 0) return { label: 'Pairing…', variant: 'default' }
  if (cfg.joined) return { label: 'Joined', variant: 'secondary' }
  // Credentials stored but no live link (reboot during coordinator outage,
  // rejoin retries running): distinct from never-paired "Not joined".
  if (cfg.commissioned) return { label: 'Reconnecting…', variant: 'default' }
  return { label: 'Not joined', variant: 'destructive' }
}

export function formatPanId(panId) {
  if (!panId) return '—'
  return '0x' + Number(panId).toString(16).toUpperCase().padStart(4, '0')
}

// Scan-channel options (0 = auto = all 11..26). Kept in the lib so the
// DeviceTab and any future callers share the same list/labels.
export const ZIGBEE_CHANNEL_OPTIONS = [
  0, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
]

export function zigbeeChannelLabel(channel) {
  if (channel === 0) return 'Auto (all channels)'
  return `Channel ${channel}`
}

// Parent-link quality label for the 0-255 LQI scale (mirrors the WiFi page's
// getSignalStrength wording so both radios read the same way).
export function zigbeeLqiLabel(lqi) {
  if (lqi >= 200) return 'Excellent'
  if (lqi >= 150) return 'Good'
  if (lqi >= 100) return 'Fair'
  return 'Weak'
}

// Parent description: short address 0x0000 is always the coordinator,
// anything else is a router short address. Keyed off the address, not
// the depth (the stack has reported addr 0 with nonzero depth).
// Shown only when the LQI poll has produced a reading (lqi_valid).
export function zigbeeParentLabel(cfg) {
  if (!cfg || !cfg.lqi_valid) return ''
  if (cfg.parent_addr === 0) return 'via coordinator'
  return 'via ' + formatPanId(cfg.parent_addr)
}
