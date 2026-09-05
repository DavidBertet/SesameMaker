// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const require = createRequire(import.meta.url)
const {
  secretsTemplate,
  upsertSecret,
  readSecret,
  loadSecrets,
  writeSecrets,
} = require('./secrets.js')

function tempFile(t, name = 'secrets.h', content = null) {
  const dir = mkdtempSync(join(tmpdir(), 'secrets-'))
  const file = join(dir, name)
  if (content !== null) {
    writeFileSync(file, content)
  }
  t.after(() => rmSync(dir, { recursive: true, force: true }))
  return file
}

test('upsertSecret appends an undef+define block to a fresh file', () => {
  const updated = upsertSecret(secretsTemplate(), 'OTA_PASSWORD', 's3cret')
  assert.ok(updated.includes('#undef OTA_PASSWORD\n#define OTA_PASSWORD "s3cret"'))
})

test('upsertSecret replaces an existing block', () => {
  const content = `${secretsTemplate()}#undef FOO\n#define FOO "old"\n`
  const updated = upsertSecret(content, 'FOO', 'new')
  assert.ok(updated.includes('#define FOO "new"'))
  assert.ok(!updated.includes('"old"'))
})

test('upsertSecret upgrades a bare define to an undef block', () => {
  const updated = upsertSecret('#pragma once\n#define FOO "old"\n', 'FOO', 'new')
  assert.ok(updated.includes('#undef FOO\n#define FOO "new"'))
})

test('upsertSecret escapes quotes and backslashes', () => {
  const updated = upsertSecret(secretsTemplate(), 'FOO', 'a"b\\c')
  assert.ok(updated.includes('#define FOO "a\\"b\\\\c"'))
})

test('readSecret round-trips through upsertSecret', () => {
  const content = upsertSecret(upsertSecret(secretsTemplate(), 'A', 'x'), 'B', 'a"b\\c')
  assert.equal(readSecret(content, 'A'), 'x')
  assert.equal(readSecret(content, 'B'), 'a"b\\c')
  assert.equal(readSecret(content, 'MISSING'), null)
})

test('loadSecrets returns a template when the file is missing', (t) => {
  const file = join(mkdtempSync(join(tmpdir(), 'secrets-')), 'missing.h')
  assert.ok(loadSecrets(file).includes('DO NOT COMMIT'))
})

test('writeSecrets persists multiple secrets in one file', (t) => {
  const file = tempFile(t)
  writeSecrets({ OTA_PASSWORD: 'ota-pw', DEFAULT_WIFI_SSID: 'Home' }, file)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#define OTA_PASSWORD "ota-pw"'))
  assert.ok(content.includes('#define DEFAULT_WIFI_SSID "Home"'))
  assert.ok(content.includes('DO NOT COMMIT'))
})
