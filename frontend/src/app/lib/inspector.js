// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Generic pin-inspector display helpers: turn a `gpio_state` pin entry
// {gpio, name, role, type, mode, level?, hit?, raw?, mv?, detail?} into
// human-readable labels. No SesameMaker specifics — copy with the tab.

export function pinTypeLabel(type) {
  switch (type) {
    case 'analog':
      return 'ANALOG'
    case 'uart':
      return 'UART'
    case 'i2c':
      return 'I2C'
    case 'spi':
      return 'SPI'
    default:
      return 'DIGITAL'
  }
}

// Level cell: analog pins carry no level (show raw/mV in Meaning instead).
export function pinLevelText(pin) {
  if (pin.type === 'analog') return '—'
  return pin.level ? 'HIGH' : 'LOW'
}

// "340" -> "0.3s", "12000" -> "12s", "90000" -> "2m"
export function fmtAge(ms) {
  if (ms === undefined || ms === null) return '—'
  if (ms < 1000) return `${(ms / 1000).toFixed(1)}s`
  if (ms < 60000) return `${Math.round(ms / 1000)}s`
  return `${Math.round(ms / 60000)}m`
}

// Edge activity counted on-device every 5 ms: visible even when the 2 s UI
// poll misses the waveform itself.
function activitySuffix(pin) {
  if (pin.edges === undefined || pin.edges === null) return ''
  if (!pin.edges) return ' · no edges yet'
  if (pin.idle_ms === undefined || pin.idle_ms === null) return ` · ${pin.edges} edges`
  return ` · ${pin.edges} edges (idle ${fmtAge(pin.idle_ms)})`
}
export function pinMeaning(pin) {
  if (pin.type === 'analog') {
    if (pin.mv === undefined || pin.raw === undefined) return 'read error'
    return `${pin.mv} mV (raw ${pin.raw})`
  }
  const levelWord = pin.level ? 'HIGH' : 'LOW'
  const activity = activitySuffix(pin)
  if (pin.hit !== undefined) return `${pin.hit ? 'hit' : 'clear'} · ${levelWord}${activity}`
  if (pin.detail) return `${pin.detail} · ${levelWord}${activity}`
  if (pin.mode === 'output') return `driving ${levelWord}${activity}`
  return `idle ${levelWord}${activity}`
}

// ---- Bus framing results (buses[] entries) ----

export function busStatusMeta(status) {
  switch (status) {
    case 'valid':
      return { label: 'VALID', variant: 'default' }
    case 'stale':
      return { label: 'STALE', variant: 'secondary' }
    case 'error':
      return { label: 'ERROR', variant: 'destructive' }
    default:
      return { label: 'NO TRAFFIC', variant: 'outline' }
  }
}

export function formatI2cAddr(addr) {
  return `0x${Number(addr).toString(16).toUpperCase().padStart(2, '0')}`
}

export function busMeaning(bus) {
  if (bus.status === 'no-traffic') return 'listening — no frames yet'
  const framing = (bus.frame_errors ?? 0) - (bus.parity_errors ?? 0)
  let s = `${bus.frames_ok ?? 0} frames, ${bus.frame_errors ?? 0} errors`
  if (bus.frame_errors > 0) {
    const bits = []
    if (framing > 0) bits.push(`${framing} framing`)
    if (bus.parity_errors > 0) bits.push(`${bus.parity_errors} parity`)
    if (bits.length) s += ` (${bits.join(', ')})`
    if (bus.error_idle_ms !== undefined) s += `, last ${fmtAge(bus.error_idle_ms)} ago`
  }
  if (bus.idle_ms !== undefined) s += ` (idle ${fmtAge(bus.idle_ms)})`
  if (bus.addresses?.length) {
    const addrs = bus.addresses
      .map((a) => `${formatI2cAddr(a.addr)} R${a.reads}/W${a.writes}`)
      .join(', ')
    s += ` · ${addrs}`
  }
  return s
}
