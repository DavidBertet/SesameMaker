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
  return { label: 'Not joined', variant: 'destructive' }
}

export function formatPanId(panId) {
  if (!panId) return '—'
  return '0x' + Number(panId).toString(16).toUpperCase().padStart(4, '0')
}
