// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Network discovery: subnet math, ARP-table pre-filtering and HTTP probing
// for SesameMaker OTA targets. No USB or prompting here (see usb_list.js,
// select.js); `discover.js` re-exports this module so existing callers keep
// working.

const os = require('os')
const http = require('http')
const zlib = require('zlib')
const { execPromise } = require('./system')

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

// Poll the network for a device that just (re)booted — e.g. after a serial
// flash with fresh WiFi credentials the device reboots, joins WiFi via DHCP
// and shows up under a new, unknown IP. Scans repeatedly until at least one
// device answers or attempts are exhausted. `discoverFn` is injectable for
// tests. Returns the found devices (possibly []).
async function waitForDeviceOnNetwork({
  attempts = 6,
  intervalMs = 10000,
  timeout = 400,
  discoverFn = discoverNetworkDevices,
} = {}) {
  let found = []
  for (let attempt = 1; attempt <= attempts; attempt++) {
    found = await discoverFn({ timeout })
    if (found.length > 0) {
      return found
    }
    if (attempt < attempts) {
      await new Promise((resolve) => setTimeout(resolve, intervalMs))
    }
  }
  return found
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
  waitForDeviceOnNetwork,
}
