// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const os = require('os')
const http = require('http')
const zlib = require('zlib')
const { execPromise } = require('./system')
const { selectFromList } = require('./prompt')
const { logger, colors } = require('./logger')

// The served root HTML contains <title>SesameMaker</title>, which is the
// fingerprint used to positively identify a SesameMaker device when scanning
// the local network for OTA targets.
const DEVICE_MARKER = 'SesameMaker'

// IEEE-registered OUIs (first 3 bytes of a MAC) assigned to Espressif Inc.
// (and its historical vendor name "Cadence Design Systems" / "Expressif"). A
// device with one of these MAC prefixes is very likely an ESP32/ESP8266, so we
// can skip HTTP-probing every other host on the subnet. Stored as lowercase
// 6-char hex strings with no separators, e.g. '240ac4'.
const ESPRESSIF_OUIS = [
  '240ac4',
  '30aea4',
  'a4cf12',
  '4827e2',
  '5ccf7f',
  '7c9ebd',
  'acd074',
  'cc50e3',
  'dcda0c',
  'ecfabc',
  'f4cfa2',
  '8caab5',
  'a020a6',
  '246f28',
  '348518',
  'dc4f22',
  'a8032a',
  '68c63a',
  'c84c75',
]

// USB VID:PID pairs for the USB-UART bridge chips most commonly found on ESP
// dev boards (ESP32 / ESP32-C3 / ESP8266 / G60 kits), plus `303a` which is
// Espressif's own VID used by the native USB (CDC/JTAG) on ESP32-S2/S3/C3.
// `pio device list` reports `HwID: USB VID:PID=<vid>:<pid>`. Filtering USB
// candidates by VID keeps unrelated serial ports (Bluetooth, wifi debug,
// Raspberry Pi, etc.) out of the picker.
const ESP_USB_BRIDGE_VIDS = new Set(['10c4', '1a86', '0403', '303a'])

// Internal-probing only; peers probing us are blocked by the server, not here.
function getLocalIPv4Interfaces() {
  const interfaces = os.networkInterfaces()
  const result = []
  for (const [name, addrs] of Object.entries(interfaces)) {
    for (const addr of addrs || []) {
      // Skip loopback, link-local, and non-IPv4. Only consider up, non-internal
      // interfaces that have a netmask (so a /subnet can be derived).
      if (addr.family !== 'IPv4' || addr.internal) {
        continue
      }
      if (!addr.cidr || !addr.netmask) {
        continue
      }
      result.push({ name, address: addr.address, netmask: addr.netmask, cidr: addr.cidr })
    }
  }
  return result
}

// Returns the network (base) address as an integer, given an IPv4 address and
// netmask, both in dotted-quad form. Returns null on malformed input.
function ipToInt(ip) {
  const parts = String(ip).split('.').map(Number)
  if (parts.length !== 4 || parts.some((p) => Number.isNaN(p) || p < 0 || p > 255)) {
    return null
  }
  return ((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]) >>> 0
}

function intToIp(int) {
  return [24, 16, 8, 0].map((shift) => (int >>> shift) & 0xff).join('.')
}

function netmaskToInt(netmask) {
  const int = ipToInt(netmask)
  if (int === null) {
    return null
  }
  // A leading "1" run followed by zeros is the canonical form; anything else is
  // still accepted as a bitmask value as long as it is a well-formed /30-ish.
  // We simply use the 32-bit value directly.
  return int
}

// Compute the list of host addresses to probe for a given interface. Falls back
// to the default classful subnet when the netmask is unusual.
function computeHostCandidates(interfaceInfo, options = {}) {
  const { excludeSelf = true } = options
  const ip = ipToInt(interfaceInfo.address)
  const mask = netmaskToInt(interfaceInfo.netmask)
  if (ip === null || mask === null) {
    return []
  }

  // Keep all arithmetic unsigned (bitwise ops in JS are signed 32-bit).
  const networkBase = (ip & mask) >>> 0
  const inverted = ~mask >>> 0
  // Number of addresses in the subnet. Clamp so we never try to sweep a huge
  // supernet (a device on a >/16 corporate subnet won't be scanned fully).
  const size = inverted + 1
  if (size < 4 || size > 0x01000000 || size > 65536) {
    return []
  }

  const selfInt = ipToInt(interfaceInfo.address)
  const broadcast = (networkBase + size - 1) >>> 0
  const candidates = []
  for (let i = 1; i < size - 1; i++) {
    const candidate = networkBase + i
    if (excludeSelf && candidate === selfInt) {
      continue
    }
    if (candidate === broadcast) {
      continue
    }
    candidates.push(intToIp(candidate))
  }
  return candidates
}

// Normalize a MAC address string (any separator, either case) to a lowercase
// hex string with no separators, e.g. "24:0A:C4:..." -> "240ac4...". Returns
// null if the input is not a plausible MAC.
function normalizeMac(mac) {
  const hex = String(mac || '').replace(/[^0-9a-fA-F]/g, '')
  if (hex.length !== 12) {
    return null
  }
  return hex.toLowerCase()
}

// Parse the output of `arp -a` into an array of { ip, mac } entries, returning
// entries with both an IP and a usable MAC. Handles the macOS/Linux and Windows
// formats:
//   macOS:  ? (192.168.1.1) at 1c:1f:1d:00:01:02 on en0 ifscope [ethernet]
//   Linux:  ? (192.168.1.1) at 1c:1f:1d:00:01:02 [ether] on eth0
//   Win:    192.168.1.1  1c-1f-1d-00-01-02  dynamic
function parseArpTable(output) {
  const entries = []
  const re =
    /([0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3})[^\n]*?\b((?:[0-9a-fA-F]{1,2}[:-]){5}[0-9a-fA-F]{1,2})\b/g
  for (const match of String(output || '').matchAll(re)) {
    const ip = match[1]
    const mac = normalizeMac(match[2])
    if (!mac) {
      continue
    }
    entries.push({ ip, mac })
  }
  return entries
}

// Read the local ARP table via `arp -a`, keyed by IP address -> normalized MAC.
// Returns an empty map if the command is unavailable or fails.
async function readArpTable() {
  const isWindows = process.platform === 'win32'
  try {
    const cmd = isWindows ? 'arp -a' : 'arp -an'
    const { stdout } = await execPromise(cmd)
    const table = new Map()
    for (const entry of parseArpTable(stdout)) {
      table.set(entry.ip, entry.mac)
    }
    return table
  } catch {
    return new Map()
  }
}

// Given the ARP table (Map ip -> mac) and a list of candidate IPs, keep only the
// candidates whose MAC OUI belongs to an Espressif device. Candidates with no
// ARP entry are excluded (their vendor is unknown). If nothing is known about
// any candidate this returns [].
function filterByEspressifOui(arpTable, candidates) {
  const espressif = new Set(ESPRESSIF_OUIS)
  return candidates.filter((ip) => {
    const mac = arpTable.get(ip)
    return mac ? espressif.has(mac.slice(0, 6)) : false
  })
}

// Probe a single host for the SesameMaker fingerprint on port 80.
function probeDevice(ip, { timeout = 400, port = 80 } = {}) {
  return new Promise((resolve) => {
    const req = http.get(
      {
        hostname: ip,
        port,
        path: '/',
        method: 'GET',
        timeout,
        headers: { Host: ip, 'Accept-Encoding': 'gzip' },
      },
      (res) => {
        // Collect raw Buffers: setting utf8 encoding here would corrupt the
        // gzip bytes served by the device (the frontend is stored compressed,
        // with Content-Encoding: gzip).
        const chunks = []
        let size = 0
        res.on('data', (chunk) => {
          chunk = Buffer.from(chunk)
          // Only read a small prefix; the title tag appears at the top of the
          // page. 64 KiB is plenty and never chokes an ESP's tiny file.
          if (size >= 65536) {
            return
          }
          chunks.push(chunk)
          size += chunk.length
        })
        res.on('end', () => {
          const text = decodeResponseBody(res.headers, Buffer.concat(chunks))
          const found = text != null && text.toLowerCase().includes(DEVICE_MARKER.toLowerCase())
          // Free the socket so the pool doesn't keep sockets around.
          res.destroy()
          resolve({ ip, connected: found ? true : false, isDevice: found })
        })
      },
    )
    req.on('timeout', () => {
      req.destroy()
      resolve({ ip, connected: false, isDevice: false })
    })
    req.on('error', () => {
      resolve({ ip, connected: false, isDevice: false })
    })
  })
}

// Decode a probe response body into searchable text, honouring Content-Encoding
// so the fingerprint can be found even when the device serves a gzip file.
// Returns null when no usable text can be extracted.
function decodeResponseBody(headers, body) {
  const encoding = String((headers && headers['content-encoding']) || '')
    .toLowerCase()
    .trim()
  try {
    if (encoding === 'gzip') {
      return zlib.gunzipSync(body).toString()
    }
    if (encoding === 'deflate') {
      return zlib.inflateSync(body).toString()
    }
  } catch {
    // Corrupt or truncated stream: nothing searchable, so report not-a-device.
    return null
  }
  return body.toString()
}

// Run a bounded set of probes concurrently.
async function probeAll(ips, timeout) {
  const results = []
  const CONCURRENCY = 64
  let index = 0

  async function worker() {
    while (index < ips.length) {
      const ip = ips[index++]
      const res = await probeDevice(ip, { timeout })
      if (res.isDevice) {
        results.push(res)
      }
    }
  }

  const workers = []
  const count = Math.min(CONCURRENCY, ips.length)
  for (let i = 0; i < count; i++) {
    workers.push(worker())
  }
  await Promise.all(workers)
  return results
}

// Scan every local interface's subnet for SesameMaker OTA targets.
//
// Efficiency strategy (ARP/OUI filter followed by HTTP): read the local ARP
// table and HTTP-probe only the hosts whose MAC OUI identifies them as an
// Espressif device. Espressif hosts are cheap to skip HTTP on — nothing else on
// the subnet runs SesameMaker. If the ARP table yields no Espressif candidates
// (stale/empty cache, device not yet seen), fall back to probing the whole
// subnet so a device is never missed.
async function discoverNetworkDevices({ timeout = 400 } = {}) {
  const interfaces = getLocalIPv4Interfaces()
  const found = new Map() // ip -> result

  const explore = async (candidates) => {
    if (candidates.length === 0) {
      return
    }
    const results = await probeAll(candidates, timeout)
    for (const res of results) {
      found.set(res.ip, res)
    }
  }

  const arpTable = await readArpTable()
  for (const iface of interfaces) {
    const allCandidates = computeHostCandidates(iface)
    if (allCandidates.length === 0) {
      continue
    }
    const espressifCandidates = filterByEspressifOui(arpTable, allCandidates)
    await explore(espressifCandidates.length > 0 ? espressifCandidates : allCandidates)
  }

  return Array.from(found.values())
}

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

// Pick a device from the discovered candidates. ALWAYS asks the user to
// confirm — even when exactly one candidate is found. Auto-picking a single
// result silently flashed the wrong device (e.g. an online spare while the
// intended target was offline). Device selection is exempt from -y/--yes.
async function selectDevice(networkDevices, usbDevices) {
  const netCount = networkDevices.length
  const usbCount = usbDevices.length
  const usbIsFallback = Boolean(usbDevices.noEsp)

  // `noEsp` means USB enumeration fell back to "every serial port" because no
  // clearly-ESP device was found. When a SesameMaker IS on the network, those
  // unrelated serial ports are not worth offering: prefer the network device
  // and move on. The fallback is still honoured when the network is empty.
  const usableUsb = usbIsFallback && netCount > 0 ? [] : usbDevices

  if (netCount === 0 && usableUsb.length === 0) {
    logger.warning('No SesameMaker device found on the network or over USB.')
    return null
  }

  const total = netCount + usableUsb.length
  logger.info(
    total === 1
      ? 'Found 1 SesameMaker device — please confirm it is the one to use:'
      : 'Multiple SesameMaker devices detected:',
  )
  if (usableUsb.length > 0 && usbIsFallback) {
    logger.warning('No ESP device detected over USB - showing all serial ports.')
  }
  const options = []
  for (let i = 0; i < netCount; i++) {
    const ip = networkDevices[i].ip
    options.push({
      label: `${colors.cyan}Network (OTA)${colors.reset} — ${ip}`,
      value: { kind: 'ota', ip },
    })
  }
  for (let i = 0; i < usableUsb.length; i++) {
    const dev = usableUsb[i]
    const port = dev.port
    const hint = dev.description ? ` · ${dev.description}` : ''
    options.push({
      label: `${colors.cyan}USB (Serial)${colors.reset} — ${port}${colors.dim}${hint}${colors.reset}`,
      value: { kind: 'serial', port },
    })
  }

  const target = await selectFromList('Which device do you want to use?', options)
  return target || null
}

module.exports = {
  DEVICE_MARKER,
  ESPRESSIF_OUIS,
  getLocalIPv4Interfaces,
  ipToInt,
  intToIp,
  computeHostCandidates,
  normalizeMac,
  parseArpTable,
  readArpTable,
  filterByEspressifOui,
  probeDevice,
  decodeResponseBody,
  probeAll,
  discoverNetworkDevices,
  parseDeviceList,
  parseHwidIdentity,
  dedupeUsbDevices,
  looksLikeEsp,
  ESP_USB_BRIDGE_VIDS,
  discoverUsbDevices,
  selectDevice,
}
