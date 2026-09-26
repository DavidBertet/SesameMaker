// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

// parseArgs calls process.exit on validation errors; stub it per test
function parseWithExitStub(argv) {
  const originalArgv = process.argv
  const originalExit = process.exit
  let exitCode = null
  process.argv = ['node', 'index.js', ...argv]
  process.exit = (code) => {
    exitCode = code
    throw new Error(`exit:${code}`)
  }
  try {
    return { parsed: require('./args.js').parseArgs(), exitCode }
  } catch (e) {
    return { parsed: null, exitCode, exited: true }
  } finally {
    process.argv = originalArgv
    process.exit = originalExit
  }
}

test('debug is false by default', () => {
  const { parsed } = parseWithExitStub(['-y'])
  assert.equal(parsed.debug, false)
})

test('-d enables debug', () => {
  const { parsed } = parseWithExitStub(['-d', '-y'])
  assert.equal(parsed.debug, true)
})

test('--debug enables debug', () => {
  const { parsed } = parseWithExitStub(['--debug'])
  assert.equal(parsed.debug, true)
})

test('debug combines with other flags', () => {
  const { parsed } = parseWithExitStub(['--debug', '-b', '-y', '--ota', '192.168.1.10'])
  assert.equal(parsed.debug, true)
  assert.equal(parsed.backendOnly, true)
  assert.equal(parsed.otaIP, '192.168.1.10')
})

test('-o without IP no longer rejects (triggers network discovery)', () => {
  const { parsed } = parseWithExitStub(['-o', '-d'])
  assert.ok(parsed !== null)
  assert.equal(parsed.hasOta, true)
  assert.equal(parsed.otaIP, null)
  assert.equal(parsed.otaDiscoveryOnly, true)
  assert.equal(parsed.discover, true)
})

test('discovery is the default when no ota ip and no serial/usb flag', () => {
  const { parsed } = parseWithExitStub(['-y'])
  assert.equal(parsed.discover, true)
  assert.equal(parsed.otaDiscoveryOnly, false)
  assert.equal(parsed.usbOnly, false)
})

test('--ota with an explicit IP disables discovery', () => {
  const { parsed } = parseWithExitStub(['--ota', '192.168.1.10'])
  assert.equal(parsed.otaIP, '192.168.1.10')
  assert.equal(parsed.discover, false)
})

test('--serial forces USB-only and disables discovery', () => {
  const { parsed } = parseWithExitStub(['--serial'])
  assert.equal(parsed.usbOnly, true)
  assert.equal(parsed.serialFlag, true)
  assert.equal(parsed.discover, false)
})

test('-s is an alias for --serial', () => {
  const { parsed } = parseWithExitStub(['-s'])
  assert.equal(parsed.usbOnly, true)
})

test('--usb is an alias for --serial', () => {
  const { parsed } = parseWithExitStub(['--usb'])
  assert.equal(parsed.usbOnly, true)
  assert.equal(parsed.usbFlag, true)
  assert.equal(parsed.discover, false)
})

test('--serial and --usb together are rejected', () => {
  const { exitCode } = parseWithExitStub(['--serial', '--usb'])
  assert.ok(exitCode !== null)
})

test('--ota with an IP and --serial together are rejected', () => {
  const { exitCode } = parseWithExitStub(['--ota', '192.168.1.10', '--serial'])
  assert.ok(exitCode !== null)
})

test('--ota without an IP and --serial together are rejected', () => {
  const { exitCode } = parseWithExitStub(['--ota', '--serial'])
  assert.ok(exitCode !== null)
})

test('--wifi-ssid parses', () => {
  const { parsed } = parseWithExitStub(['--wifi-ssid', 'MyHome'])
  assert.equal(parsed.wifiSsid, 'MyHome')
  assert.equal(parsed.hasWifiSsid, true)
})

test('--wifi-ssid without a value is rejected', () => {
  const { exitCode } = parseWithExitStub(['--wifi-ssid'])
  assert.ok(exitCode !== null)
})

test('--wifi-ssid works with -y (SSID only pre-fills the USB prompt)', () => {
  const { parsed, exitCode } = parseWithExitStub(['--wifi-ssid', 'MyHome', '-y'])
  assert.equal(exitCode, null)
  assert.equal(parsed.wifiSsid, 'MyHome')
})

test('--wifi-ssid over 32 bytes is rejected', () => {
  const { exitCode } = parseWithExitStub(['--wifi-ssid', 'a'.repeat(33)])
  assert.ok(exitCode !== null)
})
