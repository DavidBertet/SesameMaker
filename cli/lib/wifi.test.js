// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const require = createRequire(import.meta.url)
const { readDefaultWifi, injectWifiCredentials, configureWifi } = require('./wifi.js')

function tempFile(t, name = 'secrets.h', content = null) {
  const dir = mkdtempSync(join(tmpdir(), 'wifi-'))
  const file = join(dir, name)
  if (content !== null) {
    writeFileSync(file, content)
  }
  t.after(() => rmSync(dir, { recursive: true, force: true }))
  return file
}

const creds = (ssid, pass, gen) =>
  `#pragma once\n#undef DEFAULT_WIFI_SSID\n#define DEFAULT_WIFI_SSID "${ssid}"\n#undef DEFAULT_WIFI_PASSWORD\n#define DEFAULT_WIFI_PASSWORD "${pass}"\n#undef DEFAULT_WIFI_GEN\n#define DEFAULT_WIFI_GEN "${gen}"\n`

test('readDefaultWifi returns nulls when file is missing', (t) => {
  const { ssid, hasPassword } = readDefaultWifi(tempFile(t))
  assert.equal(ssid, null)
  assert.equal(hasPassword, false)
})

test('injectWifiCredentials creates secrets.h with DO NOT COMMIT', (t) => {
  const file = tempFile(t)
  assert.equal(injectWifiCredentials('MyHome', 's3cret', file), true)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('DO NOT COMMIT'))
  assert.ok(content.includes('#define DEFAULT_WIFI_SSID "MyHome"'))
  assert.ok(content.includes('#define DEFAULT_WIFI_PASSWORD "s3cret"'))
  assert.match(content, /#define DEFAULT_WIFI_GEN "[0-9a-f]{8}"/)
})

test('injectWifiCredentials escapes quotes and backslashes', (t) => {
  const file = tempFile(t)
  injectWifiCredentials('a"b', 'c\\d', file)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#define DEFAULT_WIFI_SSID "a\\"b"'))
  assert.ok(content.includes('#define DEFAULT_WIFI_PASSWORD "c\\\\d"'))
})

test('injectWifiCredentials bumps gen on every provision', (t) => {
  const file = tempFile(t)
  injectWifiCredentials('Same', 'pw', file)
  const first = readDefaultWifi(file).gen
  assert.equal(injectWifiCredentials('Same', 'pw', file), true)
  const second = readDefaultWifi(file).gen
  assert.ok(first && second && first !== second)
})

test('configureWifi returns null without --wifi-ssid', async (t) => {
  const file = tempFile(t)
  assert.equal(
    await configureWifi(
      {},
      file,
      async () => 'pw',
      async () => '',
    ),
    null,
  )
})

test('configureWifi reuses stored password without prompting', async (t) => {
  const file = tempFile(t, 'secrets.h', creds('Home', 'pw', 'aaaa'))
  let prompted = false
  const res = await configureWifi(
    { wifiSsid: 'Home' },
    file,
    async () => {
      prompted = true
      return 'pw'
    },
    async () => '',
  )
  assert.equal(prompted, false)
  assert.equal(res.injected, false)
})

test('configureWifi re-prompts when password changed since last install', async (t) => {
  const file = tempFile(t, 'secrets.h', creds('Home', 'old', 'aaaa'))
  const res = await configureWifi(
    { wifiSsid: 'Home' },
    file,
    async () => 'new-pw',
    async () => 'n',
  )
  assert.equal(res.injected, true)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#define DEFAULT_WIFI_PASSWORD "new-pw"'))
  assert.ok(!content.includes('"aaaa"'))
})

test('configureWifi prompts securely and injects new SSID', async (t) => {
  const file = tempFile(t)
  const res = await configureWifi(
    { wifiSsid: 'NewNet' },
    file,
    async () => 'hidden-pw',
    async () => '',
  )
  assert.equal(res.injected, true)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#define DEFAULT_WIFI_SSID "NewNet"'))
  assert.ok(content.includes('#define DEFAULT_WIFI_PASSWORD "hidden-pw"'))
})
