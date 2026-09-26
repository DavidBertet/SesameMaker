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
      getOtaPassword: async () => 'dev-pw',
      setOtaPassword: async () => {},
      provisionWifi: async () => ({ ssid: 'x', url: null }),
      ...over.improv,
    },
    prompt: { askQuestion: async () => 'y', askPassword: async () => 'pw', ...over.prompt },
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
  assert.equal(await provisionOverUsb({}, { backendUploaded: true }, d), null)
  assert.equal(called, false)
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
  assert.equal(await provisionOverUsb({ serialPort: '/dev/x' }, { backendUploaded: true }, d), null)
})
