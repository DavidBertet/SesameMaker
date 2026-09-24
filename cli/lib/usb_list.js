// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// USB serial discovery: parse `pio device list`, collapse duplicate /dev
// names for the same physical board, and prefer ESP UART bridges.
// `discover.js` re-exports this module so existing callers keep working.

const { execPromise } = require('./system')

// USB VID:PID pairs for the USB-UART bridge chips most commonly found on ESP
// dev boards (ESP32 / ESP32-C3 / ESP8266 / G60 kits), plus `303a` which is
// Espressif's own VID used by the native USB (CDC/JTAG) on ESP32-S2/S3/C3.
// `pio device list` reports `HwID: USB VID:PID=<vid>:<pid>`. Filtering USB
// candidates by VID keeps unrelated serial ports (Bluetooth, wifi debug,
// Raspberry Pi, etc.) out of the picker.
const ESP_USB_BRIDGE_VIDS = new Set(['10c4', '1a86', '0403', '303a'])

// Parse `pio device list` output into {port, description, hwid} entries.
// Example block:
//   /dev/cu.usbserial-14310
//   -----------------------
//   HwID: USB VID:PID=10c4:ea60
//   Description: CP210x UART Bridge
function parseDeviceList(output) {
  const devices = []
  const lines = String(output || '').split('\n')
  let current = null

  const flush = () => {
    if (current && current.port) {
      devices.push(current)
    }
    current = null
  }

  for (const rawLine of lines) {
    const line = rawLine.trimEnd()
    if (!line.trim()) {
      continue
    }
    const underline = /^-{3,}$/
    // A line like "------" separates the header from data; a fresh port block
    // starts with an absolute path like /dev/cu.* or COM*.
    if (underline.test(line.trim())) {
      continue
    }
    if (line.startsWith('/') || /^[A-Za-z]:[\\/]/.test(line) || /^COM\d+\s*$/i.test(line)) {
      flush()
      current = { port: line, description: '', hwid: '' }
      continue
    }
    if (!current) {
      continue
    }
    const hwidMatch = line.match(/^HwID:\s*(.*)$/i) || line.match(/^Hardware ID:\s*(.*)$/i)
    if (hwidMatch) {
      current.hwid = hwidMatch[1].trim()
      continue
    }
    const descMatch = line.match(/^Description:\s*(.*)$/i)
    if (descMatch) {
      current.description = descMatch[1].trim()
      continue
    }
  }
  flush()
  return devices
}

// Extract the hardware identity of a USB serial device from its pio HwID
// string, e.g. "USB VID:PID=10C4:EA60 SER=0001 LOCATION=1-1" ->
// { vid: '10c4', pid: 'ea60', ser: '0001', location: '1-1' }. Missing fields
// become ''. Returns null when there is no usable identity at all (no VID:PID
// and no SER/LOCATION), in which case the entry can't be matched to hardware.
function parseHwidIdentity(hwid) {
  const s = String(hwid || '')
  const vidPid = s.match(/VID:PID=([0-9a-fA-F]{4}):([0-9a-fA-F]{4})/)
  const ser = s.match(/\bSER=([^\s]+)/i)
  const location = s.match(/\bLOCATION=([^\s]+)/i)
  const vid = vidPid ? vidPid[1].toLowerCase() : ''
  const pid = vidPid ? vidPid[2].toLowerCase() : ''
  const serVal = ser ? ser[1] : ''
  const locVal = location ? location[1].toLowerCase() : ''
  if (!vidPid && !serVal && !locVal) {
    return null
  }
  return { vid, pid, ser: serVal, location: locVal }
}

// Prefer the most useful /dev name when several point at the same hardware:
// a name containing the USB serial number (e.g. usbserial-0001 for SER=0001)
// beats a generic driver name (e.g. SLAB_USBtoUART, which is identical for
// every CP210x board and ambiguous with 2+ boards plugged in). Otherwise
// prefer cu.* over tty.* (correct for flashing), then the longer name.
function preferUsbPort(a, b, ser = '') {
  const serLower = String(ser || '').toLowerCase()
  if (serLower) {
    const aHas = String(a.port || '')
      .toLowerCase()
      .includes(serLower)
    const bHas = String(b.port || '')
      .toLowerCase()
      .includes(serLower)
    if (aHas !== bHas) {
      return bHas ? b : a
    }
  }
  const aCu = /^\/dev\/cu\./.test(a.port)
  const bCu = /^\/dev\/cu\./.test(b.port)
  if (aCu !== bCu) {
    return bCu ? b : a
  }
  return String(b.port || '').length > String(a.port || '').length ? b : a
}

// Collapse duplicate /dev names for the same physical USB device so the
// picker lists each board once. Two alias mechanisms exist on macOS:
//   1. cu/tty dial-in pair: /dev/cu.X + /dev/tty.X (same suffix, same hw).
//   2. driver synonyms: /dev/cu.usbserial-0001 + /dev/cu.SLAB_USBtoUART —
//      different names, but identical HwID incl. SER + LOCATION (same chip,
//      same USB port). LOCATION/SER is the hardware truth here, not the name.
// Entries are grouped by (VID:PID + SER + LOCATION) when at least SER or
// LOCATION is present; grouping by VID:PID alone would wrongly merge distinct
// boards with the same bridge chip. Entries without SER/LOCATION (older pio
// format, Bluetooth, etc.) only dedupe on exact-port or cu/tty-suffix match.
// Linux ports (/dev/ttyUSB0, /dev/ttyACM0, ...) have distinct suffixes and
// distinct LOCATIONs, so distinct boards are never merged.
function dedupeUsbDevices(devices) {
  const byHw = new Map() // hw identity key -> device
  const byAlias = new Map() // fallback key -> device
  const hwKeyOf = (d) => {
    const id = parseHwidIdentity(d && d.hwid)
    if (!id || (!id.ser && !id.location)) {
      return null
    }
    return `hw:${id.vid}:${id.pid}:${id.ser}:${id.location}`
  }
  const aliasKeyOf = (d) => {
    const port = (d && d.port) || ''
    const m = port.match(/^\/dev\/(cu|tty)\.(.*)$/)
    return m ? `macos:${m[2]}` : `port:${port}`
  }
  for (const d of devices) {
    const hwKey = hwKeyOf(d)
    if (hwKey) {
      const existing = byHw.get(hwKey)
      if (!existing) {
        byHw.set(hwKey, d)
      } else {
        const id = parseHwidIdentity(d.hwid)
        byHw.set(hwKey, preferUsbPort(existing, d, id && id.ser))
      }
      continue
    }
    const key = aliasKeyOf(d)
    const existing = byAlias.get(key)
    if (!existing) {
      byAlias.set(key, d)
    } else {
      byAlias.set(key, preferUsbPort(existing, d))
    }
  }
  return [...byHw.values(), ...byAlias.values()]
}
// Best-effort guess that a USB serial device is an ESP dev board by checking
// the USB VID of its UART-bridge chip. Returns true for known CP210x (10c4),
// CH340/CH9102 (1a86) and FTDI (0403) bridges; false otherwise. Missing/unused
// hwid means we can't tell, so `lenient` controls whether it counts as a match.
function looksLikeEsp(device, lenient = false) {
  const hwid = (device && device.hwid) || ''
  const m = hwid.match(/VID:PID=([0-9a-fA-F]{4}):[0-9a-fA-F]{4}/)
  if (m) {
    return ESP_USB_BRIDGE_VIDS.has(m[1].toLowerCase())
  }
  return lenient
}

// Enumerate USB serial devices using PlatformIO.
async function discoverUsbDevices() {
  const { findPIOExecutable } = require('./pio')
  const pioCmd = await findPIOExecutable()
  const { stdout } = await execPromise(`"${pioCmd}" device list`)
  const devices = parseDeviceList(stdout)
  // Keep only serial port candidates that look like something usable on macOS/
  // Linux (cu/tty) or Windows (COM). Filter out parallel/other entries.
  const serial = dedupeUsbDevices(
    devices.filter((d) => /^\/dev\/(?:tty|cu)\./.test(d.port) || /^COM\d+$/i.test(d.port)),
  )
  // Prefer ports whose UART-bridge VID matches a known ESP dev board. If that
  // leaves nothing, fall back to every serial port so we never miss a device
  // whose hwid is blank/unrecognised. Flag the fallback so the caller can warn
  // that no clearly-ESP device was found.
  const esp = serial.filter((d) => looksLikeEsp(d))
  if (esp.length > 0) {
    return esp
  }
  serial.noEsp = true
  return serial
}

module.exports = {
  ESP_USB_BRIDGE_VIDS,
  parseDeviceList,
  parseHwidIdentity,
  dedupeUsbDevices,
  looksLikeEsp,
  discoverUsbDevices,
}
