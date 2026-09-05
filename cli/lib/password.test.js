// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const require = createRequire(import.meta.url)
const {
  generateOtaPassword,
  readSecretsPassword,
  resolveOtaPassword,
  escapeCString,
  injectOtaPassword,
} = require('./password.js')

function tempFile(t, name = 'secrets.h', content = null) {
  const dir = mkdtempSync(join(tmpdir(), 'ota-password-'))
  const file = join(dir, name)
  if (content !== null) {
    writeFileSync(file, content)
  }
  t.after(() => rmSync(dir, { recursive: true, force: true }))
  return file
}

test('generateOtaPassword returns a password of the requested length', () => {
  const password = generateOtaPassword(16)
  assert.strictEqual(password.length, 16)
})

test('generateOtaPassword is random and C-string safe', () => {
  const a = generateOtaPassword()
  const b = generateOtaPassword()
  assert.notStrictEqual(a, b)
  assert.doesNotMatch(a, /["\\]/)
})

test('readSecretsPassword returns null when file is missing', (t) => {
  assert.strictEqual(readSecretsPassword(tempFile(t)), null)
})

test('readSecretsPassword returns null when no password is baked', (t) => {
  const file = tempFile(t, 'secrets.h', '#pragma once\n')
  assert.strictEqual(readSecretsPassword(file), null)
})

test('resolveOtaPassword uses the provided password and bakes it', (t) => {
  const file = tempFile(t)
  const password = resolveOtaPassword('user-pass', file)
  assert.strictEqual(password, 'user-pass')
  assert.strictEqual(readSecretsPassword(file), 'user-pass')
})

test('resolveOtaPassword reuses the baked password across runs', (t) => {
  const file = tempFile(t)
  injectOtaPassword('persisted', file)
  assert.strictEqual(resolveOtaPassword(null, file), 'persisted')
})

test('resolveOtaPassword generates and bakes when nothing is stored', (t) => {
  const file = tempFile(t)
  const first = resolveOtaPassword(null, file)
  assert.strictEqual(first.length, 16)
  assert.strictEqual(readSecretsPassword(file), first)
  assert.strictEqual(resolveOtaPassword(null, file), first)
})

test('injectOtaPassword writes an undef+define block to secrets.h', (t) => {
  const file = tempFile(t, 'secrets.h', '#pragma once\n\n#define OTA_PASSWORD "old_password"\n')
  injectOtaPassword('new-password', file)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#undef OTA_PASSWORD\n#define OTA_PASSWORD "new-password"'))
  assert.ok(!content.includes('old_password'))
})

test('injectOtaPassword creates secrets.h with DO NOT COMMIT when missing', (t) => {
  const file = tempFile(t)
  injectOtaPassword('added', file)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('DO NOT COMMIT'))
  assert.ok(content.includes('#define OTA_PASSWORD "added"'))
})

test('injectOtaPassword escapes quotes and backslashes', (t) => {
  const file = tempFile(t, 'secrets.h', '#pragma once\n#define OTA_PASSWORD "x"\n')
  injectOtaPassword('a"b\\c', file)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#define OTA_PASSWORD "a\\"b\\\\c"'))
})

test('escapeCString escapes backslashes and quotes', () => {
  assert.strictEqual(escapeCString('a"b\\c'), 'a\\"b\\\\c')
})
