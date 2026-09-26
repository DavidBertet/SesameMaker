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

test('provisionOverUsb returns null without a serial target', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  let called = false
  const deps = {
    improv: { provisionWifi: async () => ((called = true), { ssid: 'x' }) },
    prompt: { askQuestion: async () => 'y', askPassword: async () => 'p' },
  }
  assert.equal(
    await provisionOverUsb(
      { otaIP: '1.2.3.4', serialPort: '/dev/x' },
      { backendUploaded: true },
      deps,
    ),
    null,
  )
  assert.equal(
    await provisionOverUsb({ serialPort: '/dev/x' }, { backendUploaded: false }, deps),
    null,
  )
  assert.equal(await provisionOverUsb({}, { backendUploaded: true }, deps), null)
  assert.equal(called, false)
})

test('provisionOverUsb provisions and reports the URL', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  const seen = []
  const logs = []
  const origInfo = require('./logger').logger.info
  require('./logger').logger.info = (m) => logs.push(String(m))
  const deps = {
    improv: {
      provisionWifi: async (port, opts) => {
        seen.push([port, opts])
        return { ssid: opts.ssid, url: 'http://192.168.1.10/' }
      },
    },
    prompt: { askQuestion: async () => 'y', askPassword: async () => 'pw' },
  }
  try {
    const res = await provisionOverUsb(
      { serialPort: '/dev/ttyUSB0', wifiSsid: 'home' },
      { otaPassword: 'ota', backendUploaded: true },
      deps,
    )
    assert.deepEqual(res, { ssid: 'home', url: 'http://192.168.1.10/' })
    assert.deepEqual(seen, [['/dev/ttyUSB0', { ssid: 'home', password: 'pw', otaPassword: 'ota' }]])
    assert.ok(logs.some((m) => m.includes('http://192.168.1.10/')))
  } finally {
    require('./logger').logger.info = origInfo
  }
})

test('provisionOverUsb aborts on empty SSID or declined prompt', async () => {
  tty()
  const { provisionOverUsb } = loadUsbProvision()
  let called = false
  const deps = {
    improv: { provisionWifi: async () => ((called = true), {}) },
    prompt: { askQuestion: async () => 'n', askPassword: async () => 'pw' },
  }
  assert.equal(
    await provisionOverUsb({ serialPort: '/dev/x' }, { backendUploaded: true }, deps),
    null,
  )
  assert.equal(called, false)
})
