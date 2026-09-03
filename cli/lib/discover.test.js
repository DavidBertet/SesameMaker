// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

function loadDiscover() {
  delete require.cache[require.resolve('./discover.js')]
  return require('./discover.js')
}

test('ipToInt converts dotted-quad and rejects malformed input', () => {
  const d = loadDiscover()
  assert.equal(d.ipToInt('192.168.1.10'), 0xc0a8010a)
  assert.equal(d.ipToInt('255.255.255.0'), 0xffffff00)
  assert.equal(d.ipToInt('not-an-ip'), null)
  assert.equal(d.ipToInt('1.2.3'), null)
  assert.equal(d.ipToInt('1.2.3.999'), null)
})

test('intToIp round-trips', () => {
  const d = loadDiscover()
  assert.equal(d.intToIp(d.ipToInt('192.168.1.10')), '192.168.1.10')
})

test('computeHostCandidates produces 254 hosts for a /24 and excludes self', () => {
  const d = loadDiscover()
  const iface = { address: '192.168.1.5', netmask: '255.255.255.0' }
  const hosts = d.computeHostCandidates(iface)
  // /24 has hosts .1–.254 (254 addresses); the scan skips the machine's own
  // address (.5), leaving 253.
  assert.equal(hosts.length, 253)
  assert.ok(!hosts.includes('192.168.1.5'), 'self excluded')
  assert.ok(!hosts.includes('192.168.1.255'), 'broadcast excluded')
  assert.ok(hosts.includes('192.168.1.1'), 'network gateway included')
  assert.ok(hosts.includes('192.168.1.254'))
})

test('computeHostCandidates handles small /30 subnet', () => {
  const d = loadDiscover()
  const iface = { address: '10.0.0.1', netmask: '255.255.255.252' }
  const hosts = d.computeHostCandidates(iface)
  // network 10.0.0.0/30: hosts 1,2,3; self is 1, broadcast is 3
  assert.deepEqual(hosts, ['10.0.0.2'])
})

test('computeHostCandidates rejects unusable netmask', () => {
  const d = loadDiscover()
  assert.deepEqual(d.computeHostCandidates({ address: '192.168.1.5', netmask: 'nope' }), [])
})

test('computeHostCandidates rejects oversized supernet scan', () => {
  const d = loadDiscover()
  assert.deepEqual(d.computeHostCandidates({ address: '192.168.1.5', netmask: '0.0.0.0' }), [])
})

test('parseDeviceList extracts port, hwid and description from pio device list', () => {
  const d = loadDiscover()
  const output = `
----------------------------------------------------------------
/dev/cu.usbserial-14310
-----------------------
HwID: USB VID:PID=10c4:ea60
Description: CP210x UART Bridge
Serial number: 0001

----------------------------------------------------------------
/dev/cu.SOC-DevTeam1
--------------------
HwID: USB VID:PID=303a:1001
Description: USB JTAG/serial debug unit
`
  const devices = d.parseDeviceList(output)
  assert.equal(devices.length, 2)
  assert.equal(devices[0].port, '/dev/cu.usbserial-14310')
  assert.equal(devices[0].hwid, 'USB VID:PID=10c4:ea60')
  assert.equal(devices[0].description, 'CP210x UART Bridge')
  assert.equal(devices[1].port, '/dev/cu.SOC-DevTeam1')
  assert.equal(devices[1].description, 'USB JTAG/serial debug unit')
})

test('parseDeviceList handles empty and COM ports', () => {
  const d = loadDiscover()
  assert.deepEqual(d.parseDeviceList(''), [])
  const windows = `
COM3
----
HwID: USB VID:PID=303a:1001
Description: USB JTAG/serial debug unit
`
  const devices = d.parseDeviceList(windows)
  assert.equal(devices[0].port, 'COM3')
})

test('looksLikeEsp matches known ESP UART-bridge VIDs', () => {
  const d = loadDiscover()
  assert.equal(d.looksLikeEsp({ hwid: 'USB VID:PID=10c4:ea60' }), true) // CP210x
  assert.equal(d.looksLikeEsp({ hwid: 'USB VID:PID=1a86:7523' }), true) // CH340
  assert.equal(d.looksLikeEsp({ hwid: 'USB VID:PID=0403:6001' }), true) // FTDI
  assert.equal(d.looksLikeEsp({ hwid: 'USB VID:PID=303a:1001' }), true) // Espressif native USB
})

test('looksLikeEsp rejects unrelated VIDs and missing hwid', () => {
  const d = loadDiscover()
  assert.equal(d.looksLikeEsp({ hwid: 'USB VID:PID=05ac:12a8' }), false) // Apple
  assert.equal(d.looksLikeEsp({ hwid: '' }), false) // unknown -> only true if lenient
  assert.equal(d.looksLikeEsp({ hwid: '' }, true), true)
  assert.equal(d.looksLikeEsp(undefined), false)
})

test('discoverUsbDevices keeps ESP ports and falls back to all when none match', async () => {
  const origExec = require('./system').execPromise
  const origFind = require('./pio').findPIOExecutable
  try {
    require('./pio').findPIOExecutable = async () => 'pio'
    require('./system').execPromise = async () => ({
      stdout: `
/dev/cu.wlan-debug
------------------
HwID: USB VID:PID=0bda:8153
Description: WLAN debug

/dev/cu.Bluetooth-Incoming-Port
-------------------------------
HwID: USB VID:PID=05ac:12a8

/dev/cu.usbserial-14310
-----------------------
HwID: USB VID:PID=10c4:ea60
Description: CP210x UART Bridge
`,
    })
    // Discover destructures execPromise at require time, so reload after mocking.
    const d = loadDiscover()
    const result = await d.discoverUsbDevices()
    const ports = result.map((x) => x.port)
    // Only the CP210x ESP port survives the filter.
    assert.deepEqual(ports, ['/dev/cu.usbserial-14310'])
    assert.equal(result.noEsp, undefined, 'no fallback flag when an ESP port is found')
  } finally {
    require('./system').execPromise = origExec
    require('./pio').findPIOExecutable = origFind
  }
})

test('discoverUsbDevices falls back to all serial ports when none are ESP', async () => {
  const origExec = require('./system').execPromise
  const origFind = require('./pio').findPIOExecutable
  try {
    require('./pio').findPIOExecutable = async () => 'pio'
    require('./system').execPromise = async () => ({
      stdout: `
/dev/cu.Bluetooth-Incoming-Port
-------------------------------
HwID: USB VID:PID=05ac:12a8

/dev/cu.unknowndebug
--------------------
Description: mystery serial
`,
    })
    const d = loadDiscover()
    const result = await d.discoverUsbDevices()
    const ports = result.map((x) => x.port)
    // No ESP VID present, so the fallback keeps every serial port.
    assert.deepEqual(ports, ['/dev/cu.Bluetooth-Incoming-Port', '/dev/cu.unknowndebug'])
    assert.equal(result.noEsp, true, 'fallback flag set when no ESP port is found')
  } finally {
    require('./system').execPromise = origExec
    require('./pio').findPIOExecutable = origFind
  }
})

test('normalizeMac strips separators and lowercases', () => {
  const d = loadDiscover()
  assert.equal(d.normalizeMac('24:0A:C4:11:22:33'), '240ac4112233')
  assert.equal(d.normalizeMac('24-0A-C4-11-22-33'), '240ac4112233')
  assert.equal(d.normalizeMac('240AC4112233'), '240ac4112233')
  assert.equal(d.normalizeMac('nope'), null)
})

test('parseArpTable extracts ip/mac across macOS, Linux and Windows formats', () => {
  const d = loadDiscover()
  const macos = `
? (192.168.1.1) at 1c:1f:1d:0:1:2 on en0 ifscope [ethernet]
? (192.168.1.42) at 30:ae:a4:de:ad:be on en0 ifscope [ethernet]
? (192.168.1.255) at ff:ff:ff:ff:ff:ff on en0 ifscope [ethernet]
`
  // macOS pads single-hex octets (e.g. ":0:"), which normalizeMac rejects since
  // it needs exactly 12 hex chars; such entries are skipped. Only full-width
  // octet entries (.42 and the ff broadcast) survive.
  const entries = d.parseArpTable(macos)
  assert.ok(entries.some((e) => e.ip === '192.168.1.42' && e.mac === '30aea4deadbe'))
  assert.equal(entries.length, 2)
})

test('parseArpTable handles Windows hex-dash format', () => {
  const d = loadDiscover()
  const windows = `
Interface: 192.168.1.5 --- 0xc
  Internet Address      Physical Address      Type
  192.168.1.1           1c-1f-1d-00-01-02     dynamic
  192.168.1.99          a4-cf-12-aa-bb-cc     dynamic
`
  const entries = d.parseArpTable(windows)
  assert.equal(entries.length, 2)
  assert.equal(entries[0].mac, '1c1f1d000102')
  assert.equal(entries[1].ip, '192.168.1.99')
})

test('parseArpTable returns empty for garbage', () => {
  const d = loadDiscover()
  assert.deepEqual(d.parseArpTable('no entries here'), [])
  assert.deepEqual(d.parseArpTable(''), [])
})

test('filterByEspressifOui keeps only Espressif MAC hosts', () => {
  const d = loadDiscover()
  const arp = new Map([
    ['192.168.1.1', '1c1f1d000102'], // not espressif (router)
    ['192.168.1.42', '30aea4deadbe'], // Espressif 30:AE:A4
    ['192.168.1.43', 'a4cf12aabbcc'], // Espressif A4:CF:12
  ])
  const candidates = ['192.168.1.1', '192.168.1.42', '192.168.1.43', '192.168.1.99']
  const result = d.filterByEspressifOui(arp, candidates)
  assert.deepEqual(result, ['192.168.1.42', '192.168.1.43'])
})

test('filterByEspressifOui returns [] when nothing is Espressif or unknown', () => {
  const d = loadDiscover()
  assert.deepEqual(d.filterByEspressifOui(new Map(), ['192.168.1.1']), [])
  const arp = new Map([['192.168.1.1', '1c1f1d000102']])
  assert.deepEqual(d.filterByEspressifOui(arp, ['192.168.1.1']), [])
})

test('decodeResponseBody decompresses gzip and deflate to searchable text', () => {
  const d = loadDiscover()
  const html = '<html><title>' + d.DEVICE_MARKER + '</title></html>'
  const gzip = require('zlib').gzipSync(Buffer.from(html))
  assert.equal(d.decodeResponseBody({ 'content-encoding': 'gzip' }, gzip), html)
  const deflate = require('zlib').deflateSync(Buffer.from(html))
  assert.equal(d.decodeResponseBody({ 'content-encoding': 'deflate' }, deflate), html)
  // Plain responses pass through unchanged; missing header decodes as utf8.
  assert.equal(d.decodeResponseBody({}, Buffer.from(html)), html)
  assert.equal(d.decodeResponseBody(null, Buffer.from(html)), html)
})

test('decodeResponseBody returns null when a gzip stream is unreadable', () => {
  const d = loadDiscover()
  assert.equal(d.decodeResponseBody({ 'content-encoding': 'gzip' }, Buffer.from('not-gzip')), null)
  assert.equal(d.decodeResponseBody({ 'content-encoding': 'deflate' }, Buffer.from('nope')), null)
})

test('probeDevice identifies a device that serves a gzip-compressed page', async () => {
  const httpMod = require('http')
  const origGet = httpMod.get
  const d = loadDiscover()
  const html = `<html><head><title>${d.DEVICE_MARKER}</title></head></html>`
  try {
    httpMod.get = (opts, cb) => {
      const res = new (require('stream').Readable)()
      res.statusCode = 200
      res.headers = { 'content-type': 'text/html', 'content-encoding': 'gzip' }
      cb(res)
      res.push(require('zlib').gzipSync(Buffer.from(html)))
      res.push(null)
      res.destroy = () => {}
      const req = { on: () => req, destroy: () => {} }
      return req
    }
    const result = await d.probeDevice('192.168.1.99', { timeout: 1000 })
    assert.equal(result.isDevice, true)
    assert.equal(result.connected, true)
  } finally {
    httpMod.get = origGet
  }
})

test('probeDevice does not flag a gzip page without the device marker', async () => {
  const httpMod = require('http')
  const origGet = httpMod.get
  const d = loadDiscover()
  try {
    httpMod.get = (opts, cb) => {
      const res = new (require('stream').Readable)()
      res.statusCode = 200
      res.headers = { 'content-encoding': 'gzip' }
      cb(res)
      res.push(require('zlib').gzipSync(Buffer.from('<html><title>Someone Else</title></html>')))
      res.push(null)
      res.destroy = () => {}
      const req = { on: () => req, destroy: () => {} }
      return req
    }
    const result = await d.probeDevice('192.168.1.99', { timeout: 1000 })
    assert.equal(result.isDevice, false)
  } finally {
    httpMod.get = origGet
  }
})

test('selectDevice returns the single ota device without prompting', async () => {
  const loggerMod = require('./logger')
  const orig = loggerMod.logger.success
  let reported = null
  loggerMod.logger.success = (msg) => {
    reported = msg
  }
  try {
    const d = loadDiscover()
    const target = await d.selectDevice([{ ip: '192.168.1.10' }], [])
    assert.deepEqual(target, { kind: 'ota', ip: '192.168.1.10' })
    assert.match(reported, /Found 1.*network at 192\.168\.1\.10/)
  } finally {
    loggerMod.logger.success = orig
  }
})

test('selectDevice returns the single usb device without prompting', async () => {
  const loggerMod = require('./logger')
  const orig = loggerMod.logger.success
  let reported = null
  loggerMod.logger.success = (msg) => {
    reported = msg
  }
  try {
    const d = loadDiscover()
    const target = await d.selectDevice([], [{ port: '/dev/cu.usbserial-1' }])
    assert.deepEqual(target, { kind: 'serial', port: '/dev/cu.usbserial-1' })
    assert.match(reported, /Found 1.*USB on \/dev\/cu\.usbserial-1/)
  } finally {
    loggerMod.logger.success = orig
  }
})

test('selectDevice returns null when nothing found', async () => {
  const d = loadDiscover()
  const target = await d.selectDevice([], [])
  assert.equal(target, null)
})

test('selectDevice prompts and returns chosen option when multiple', async () => {
  const prompt = require('./prompt')
  const orig = prompt.selectFromList
  let received = null
  prompt.selectFromList = async (q, options) => {
    received = { q, options }
    return options[1].value
  }

  const d = loadDiscover()
  try {
    const target = await d.selectDevice([{ ip: '192.168.1.10' }], [{ port: '/dev/cu.X' }])
    assert.deepEqual(target, { kind: 'serial', port: '/dev/cu.X' })
    assert.ok(received.q.includes('device'))
    assert.equal(received.options.length, 2)
    assert.deepEqual(received.options[0].value, { kind: 'ota', ip: '192.168.1.10' })
    assert.deepEqual(received.options[1].value, { kind: 'serial', port: '/dev/cu.X' })
  } finally {
    prompt.selectFromList = orig
  }
})

test('selectDevice returns null when the picker is aborted', async () => {
  const prompt = require('./prompt')
  const orig = prompt.selectFromList
  prompt.selectFromList = async () => null
  const d = loadDiscover()
  try {
    const target = await d.selectDevice([{ ip: '192.168.1.10' }], [{ port: '/dev/cu.X' }])
    assert.equal(target, null)
  } finally {
    prompt.selectFromList = orig
  }
})

test('selectDevice warns when USB enumeration fell back to non-ESP ports', async () => {
  const prompt = require('./prompt')
  const origPrompt = prompt.selectFromList
  prompt.selectFromList = async (q, options) => options[0].value

  const loggerMod = require('./logger')
  const origWarning = loggerMod.logger.warning
  let warned = null
  loggerMod.logger.warning = (msg) => {
    warned = msg
  }

  try {
    const d = loadDiscover()
    const usb = [
      { port: '/dev/cu.debug-console' },
      { port: '/dev/cu.wlan-debug' },
      { port: '/dev/cu.Bluetooth-Incoming-Port' },
    ]
    usb.noEsp = true
    const target = await d.selectDevice([], usb)
    assert.deepEqual(target, { kind: 'serial', port: '/dev/cu.debug-console' })
    assert.ok(warned, 'expected a warning about no detected ESP device')
    assert.match(warned, /no ESP/i)
  } finally {
    prompt.selectFromList = origPrompt
    loggerMod.logger.warning = origWarning
  }
})

test('selectDevice skips USB fallback ports and auto-picks a single network device', async () => {
  const prompt = require('./prompt')
  const origPrompt = prompt.selectFromList
  let prompted = false
  prompt.selectFromList = async () => {
    prompted = true
    return null
  }

  const loggerMod = require('./logger')
  const origWarning = loggerMod.logger.warning
  let warned = null
  loggerMod.logger.warning = (msg) => {
    warned = msg
  }

  try {
    const d = loadDiscover()
    const usb = [{ port: '/dev/cu.debug-console' }, { port: '/dev/cu.Bluetooth-Incoming-Port' }]
    usb.noEsp = true
    const target = await d.selectDevice([{ ip: '192.168.1.10' }], usb)
    assert.deepEqual(target, { kind: 'ota', ip: '192.168.1.10' })
    assert.equal(prompted, false, 'network device auto-picked without a picker')
    assert.equal(warned, null, 'no warning when fallback ports are ignored')
  } finally {
    prompt.selectFromList = origPrompt
    loggerMod.logger.warning = origWarning
  }
})

test('selectDevice prompts with network-only options when USB is a fallback', async () => {
  const prompt = require('./prompt')
  const origPrompt = prompt.selectFromList
  let received = null
  prompt.selectFromList = async (q, options) => {
    received = { q, options }
    return options[1].value
  }

  try {
    const d = loadDiscover()
    const usb = [{ port: '/dev/cu.wlan-debug' }]
    usb.noEsp = true
    const target = await d.selectDevice([{ ip: '192.168.1.10' }, { ip: '192.168.1.11' }], usb)
    assert.deepEqual(target, { kind: 'ota', ip: '192.168.1.11' })
    assert.equal(received.options.length, 2, 'USB fallback ports are not offered')
    assert.deepEqual(received.options[0].value, { kind: 'ota', ip: '192.168.1.10' })
    assert.deepEqual(received.options[1].value, { kind: 'ota', ip: '192.168.1.11' })
  } finally {
    prompt.selectFromList = origPrompt
  }
})
