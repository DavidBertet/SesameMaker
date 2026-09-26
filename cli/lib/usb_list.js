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

// Rank a /dev name when several point at the same hardware, highest wins:
// serial-number match first (e.g. usbserial-0001 for SER=0001 beats the
// generic SLAB_USBtoUART name shared by every CP210x board), then cu.* over
// tty.* (correct for flashing), then the longer name. Compared element-wise.
function usbPortRank(port, ser = '') {
  const p = String(port || '')
  return [
    ser && p.toLowerCase().includes(String(ser).toLowerCase()) ? 1 : 0,
    /^\/dev\/cu\./.test(p) ? 1 : 0,
    p.length,
  ]
}

function preferUsbPort(a, b, ser = '') {
  const ra = usbPortRank(a.port, ser)
  const rb = usbPortRank(b.port, ser)
  for (let i = 0; i < ra.length; i++) {
    if (ra[i] !== rb[i]) {
      return rb[i] > ra[i] ? b : a
    }
  }
  return a // fully tied: keep the first seen
}

// Canonical dedupe key for one physical USB device: hardware identity when
// the HwID carries SER or LOCATION (same chip + same USB port = same board,
// whatever the /dev name); otherwise the macOS cu/tty suffix, otherwise the
// exact port. Grouping by VID:PID alone would wrongly merge distinct boards
// sharing a bridge chip, so entries without SER/LOCATION (older pio format,
// Bluetooth, COM ports, ...) only collapse on suffix or exact-port match.
function canonicalUsbKey(d) {
  const id = parseHwidIdentity(d && d.hwid)
  if (id && (id.ser || id.location)) {
    return `hw:${id.vid}:${id.pid}:${id.ser}:${id.location}`
  }
  const port = (d && d.port) || ''
  const m = port.match(/^\/dev\/(cu|tty)\.(.*)$/)
  return m ? `macos:${m[2]}` : `port:${port}`
}

// Collapse duplicate /dev names for the same physical USB device so the
// picker lists each board once, in first-seen (pio listing) order.
// Linux ports (/dev/ttyUSB0, /dev/ttyACM0, ...) have distinct suffixes and
// distinct LOCATIONs, so distinct boards are never merged.
function dedupeUsbDevices(devices) {
  const seen = new Map() // canonical key -> device
  for (const d of devices) {
    const key = canonicalUsbKey(d)
    const existing = seen.get(key)
    if (!existing) {
      seen.set(key, d)
    } else {
      const id = parseHwidIdentity(d && d.hwid)
      seen.set(key, preferUsbPort(existing, d, id && id.ser))
    }
  }
  return [...seen.values()]
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
