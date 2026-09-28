// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

function loadUsbProvision() {
  delete require.cache[require.resolve('./usb_provision.js')]
  return require('./usb_provision.js')
}

const tty = () => {
  process.stdin.isTTY = true
  process.stdout.isTTY = true
}

function deps(over = {}) {
  return {
    improv: {
      resolvePortPath: async (p) => p,
      getOtaPassword: async () => 'dev-pw',
      setOtaPassword: async () => {},
      provisionWifi: async () => ({ ssid: 'x', url: null }),
      getNetworkState: async () => ({ flags: 2, urls: [] }),
      scanNetworks: async () => [],
      ...over.improv,
    },
    prompt: {
      askQuestion: async () => 'y',
      askPassword: async () => 'pw',
      selectFromList: async () => null,
      ...over.prompt,
    },
    password: { generateOtaPassword: () => 'generated-pw', ...over.password },
  }
}

test('provisionOverUsb returns null without a serial target', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  let called = false
  const d = deps({ improv: { getOtaPassword: async () => ((called = true), 'x') } })
  assert.equal(
    await provisionOverUsb(
      { otaIP: '1.2.3.4', serialPort: '/dev/x' },
      { backendUploaded: true },
      d,
    ),
    null,
  )
  assert.equal(
    await provisionOverUsb({ serialPort: '/dev/x' }, { backendUploaded: false }, d),
    null,
  )
  assert.equal(
    await provisionOverUsb(
      { serialPort: '/dev/x' },
      { backendUploaded: false, frontendUploaded: false },
      d,
    ),
    null,
  )
  assert.equal(await provisionOverUsb({}, { backendUploaded: true }, d), null)
  assert.equal(called, false)
})

test('provisionOverUsb reads the device password on frontend-only uploads', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  const d = deps({
    improv: {
      getNetworkState: async () => ({ flags: 3, urls: ['http://192.168.1.10/'] }),
    },
  })
  const res = await provisionOverUsb(
    { serialPort: '/dev/x' },
    { backendUploaded: false, frontendUploaded: true },
    d,
  )
  assert.deepEqual(res, { ssid: null, url: 'http://192.168.1.10/', otaPassword: 'dev-pw' })
})

test('provisionOverUsb keeps the device password when no flag given', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  const sets = []
  const d = deps({
    improv: {
      getOtaPassword: async () => 'dev-pw',
      setOtaPassword: async (port, pw) => void sets.push(pw),
      provisionWifi: async (port, opts) => ({ ssid: opts.ssid, url: 'http://192.168.1.10/' }),
    },
  })
  const res = await provisionOverUsb(
    { serialPort: '/dev/ttyUSB0', wifiSsid: 'home' },
    { backendUploaded: true },
    d,
  )
  assert.deepEqual(res, { ssid: 'home', url: 'http://192.168.1.10/', otaPassword: 'dev-pw' })
  assert.deepEqual(sets, [])
})

test('provisionOverUsb sends the flag password and generates when open', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  const sets = []
  const d = deps({
    improv: {
      getOtaPassword: async () => 'dev-pw',
      setOtaPassword: async (port, pw) => void sets.push([port, pw]),
      provisionWifi: async (port, opts) => ({ ssid: opts.ssid, url: null }),
    },
    prompt: { askQuestion: async () => 'n', askPassword: async () => 'pw' },
  })
  // explicit flag overwrites the device password, wifi declined
  const res = await provisionOverUsb(
    { serialPort: '/dev/ttyUSB0' },
    { uploadPassword: 'flag-pw', backendUploaded: true },
    d,
  )
  assert.deepEqual(res, { ssid: null, url: null, otaPassword: 'flag-pw' })
  assert.deepEqual(sets, [['/dev/ttyUSB0', 'flag-pw']])

  // open device without a flag generates one
  sets.length = 0
  const d2 = deps({
    improv: {
      getOtaPassword: async () => '',
      setOtaPassword: async (port, pw) => void sets.push([port, pw]),
      provisionWifi: async () => ({ ssid: null, url: null }),
    },
    prompt: { askQuestion: async () => 'n', askPassword: async () => 'pw' },
  })
  const res2 = await provisionOverUsb({ serialPort: '/dev/ttyUSB0' }, { backendUploaded: true }, d2)
  assert.deepEqual(res2, { ssid: null, url: null, otaPassword: 'generated-pw' })
  assert.deepEqual(sets, [['/dev/ttyUSB0', 'generated-pw']])
})

test('provisionOverUsb follows the port when flashing re-enumerates it', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  const seen = []
  const d = deps({
    improv: {
      resolvePortPath: async (p) => {
        assert.equal(p, undefined)
        return '/dev/cu.usbmodem1101'
      },
      getOtaPassword: async () => 'dev-pw',
      setOtaPassword: async () => {},
      provisionWifi: async (port, opts) => {
        seen.push(port)
        return { ssid: opts.ssid, url: null }
      },
    },
    prompt: { askQuestion: async () => 'y', askPassword: async () => 'pw' },
  })
  // no serialPort known (-s flow): resolves the single candidate, wifi on it
  const res = await provisionOverUsb({ wifiSsid: 'home' }, { backendUploaded: true }, d)
  assert.equal(res.otaPassword, 'dev-pw')
  assert.equal(res.ssid, 'home')
  assert.deepEqual(seen, ['/dev/cu.usbmodem1101'])
})

test('provisionOverUsb returns null with no USB port at all', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  let contacted = false
  const d = deps({
    improv: {
      resolvePortPath: async () => undefined,
      getOtaPassword: async () => ((contacted = true), 'x'),
    },
  })
  assert.equal(await provisionOverUsb({}, { backendUploaded: true }, d), null)
  assert.equal(contacted, false)
})

test('provisionOverUsb returns null when the device does not answer', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  const d = deps({
    improv: {
      getOtaPassword: async () => {
        throw new Error('timed out')
      },
    },
  })
  assert.equal(
    await provisionOverUsb(
      { serialPort: '/dev/x' },
      { backendUploaded: true, retry: { attempts: 2, retryMs: 1 } },
      d,
    ),
    null,
  )
})

test('provisionOverUsb waits out a rebooting device, then resolves', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  let calls = 0
  const d = deps({
    improv: {
      getOtaPassword: async () => {
        calls++
        if (calls < 3) throw new Error('timed out')
        return 'dev-pw'
      },
      setOtaPassword: async () => {},
      provisionWifi: async (port, opts) => ({ ssid: opts.ssid, url: null }),
    },
    prompt: { askQuestion: async () => 'n', askPassword: async () => 'pw' },
  })
  const res = await provisionOverUsb(
    { serialPort: '/dev/x' },
    { backendUploaded: true, retry: { attempts: 5, retryMs: 1 } },
    d,
  )
  assert.deepEqual(res, { ssid: null, url: null, otaPassword: 'dev-pw' })
  assert.equal(calls, 3)
})

test('provisionOverUsb skips WiFi entirely when already online', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  let prompted = false
  let provisioned = false
  const d = deps({
    improv: {
      getNetworkState: async () => ({ flags: 3, urls: ['http://192.168.1.10/'] }),
      provisionWifi: async () => ((provisioned = true), {}),
    },
    prompt: {
      askQuestion: async () => ((prompted = true), 'y'),
      askPassword: async () => 'pw',
      selectFromList: async () => ((prompted = true), 'x'),
    },
  })
  const res = await provisionOverUsb({ serialPort: '/dev/x' }, { backendUploaded: true }, d)
  assert.deepEqual(res, { ssid: null, url: 'http://192.168.1.10/', otaPassword: 'dev-pw' })
  assert.equal(prompted, false)
  assert.equal(provisioned, false)
})

test('provisionOverUsb picks strongest scanned network, deduped', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  let pickedOptions = null
  const provisioned = []
  const d = deps({
    improv: {
      scanNetworks: async () => [
        { ssid: 'Far', rssi: -80, auth: true },
        { ssid: 'Near', rssi: -50, auth: true },
        { ssid: 'Near', rssi: -70, auth: true },
        { ssid: 'Open', rssi: -60, auth: false },
      ],
      provisionWifi: async (port, opts) => {
        provisioned.push(opts.ssid)
        return { ssid: opts.ssid, url: null }
      },
    },
    prompt: {
      askQuestion: async (q) => {
        if (q.startsWith('Configure')) return 'y'
        throw new Error('should pick from scan, not prompt')
      },
      askPassword: async () => 'pw',
      selectFromList: async (q, options) => {
        pickedOptions = options
        return options[0].value
      },
    },
  })
  const res = await provisionOverUsb({ serialPort: '/dev/x' }, { backendUploaded: true }, d)
  assert.equal(res.ssid, 'Near')
  assert.deepEqual(
    pickedOptions.map((o) => o.value),
    ['Near', 'Open', 'Far', null],
  )
  assert.deepEqual(provisioned, ['Near'])
})

test('provisionOverUsb falls back to manual SSID when scan is empty', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  const d = deps({
    improv: { scanNetworks: async () => [] },
    prompt: {
      askQuestion: async (q) => (q === 'WiFi SSID:' ? 'typed' : 'y'),
      askPassword: async () => 'pw',
      selectFromList: async () => null,
    },
  })
  const res = await provisionOverUsb({ serialPort: '/dev/x' }, { backendUploaded: true }, d)
  assert.equal(res.ssid, 'typed')
})
