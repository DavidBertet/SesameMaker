// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import { mkdtempSync, writeFileSync, readFileSync, rmSync, unlinkSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const require = createRequire(import.meta.url)
const {
  generateOtaPassword,
  readStoredPassword,
  writeStoredPassword,
  resolveOtaPassword,
  escapeCString,
  injectOtaPassword,
} = require('./password.js')

function tempFile(t, name = 'store', content = null) {
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

test('writeStoredPassword then readStoredPassword round-trips', (t) => {
  const file = tempFile(t)
  writeStoredPassword('secret123', file)
  assert.strictEqual(readStoredPassword(file), 'secret123')
})

test('readStoredPassword returns null when file is missing', (t) => {
  assert.strictEqual(readStoredPassword(tempFile(t)), null)
})

test('readStoredPassword returns null when file is empty', (t) => {
  const file = tempFile(t, 'store', '\n')
  assert.strictEqual(readStoredPassword(file), null)
})

test('resolveOtaPassword uses the provided password and persists it', (t) => {
  const file = tempFile(t)
  const password = resolveOtaPassword('user-pass', file)
  assert.strictEqual(password, 'user-pass')
  assert.strictEqual(readStoredPassword(file), 'user-pass')
})

test('resolveOtaPassword reuses a stored password across runs', (t) => {
  const file = tempFile(t)
  writeStoredPassword('persisted', file)
  assert.strictEqual(resolveOtaPassword(null, file), 'persisted')
})

test('resolveOtaPassword generates and persists when nothing is stored', (t) => {
  const file = tempFile(t)
  const first = resolveOtaPassword(null, file)
  assert.strictEqual(first.length, 16)
  assert.strictEqual(readStoredPassword(file), first)
  assert.strictEqual(resolveOtaPassword(null, file), first)
})

test('injectOtaPassword replaces the existing OTA_PASSWORD define', (t) => {
  const file = tempFile(t, 'constants.h', '#pragma once\n\n#define OTA_PASSWORD "old_password"\n')
  injectOtaPassword('new-password', file)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#define OTA_PASSWORD "new-password"'))
  assert.ok(!content.includes('old_password'))
})

test('injectOtaPassword appends the define when missing', (t) => {
  const file = tempFile(t, 'constants.h', '#pragma once\n')
  injectOtaPassword('added', file)
  assert.ok(readFileSync(file, 'utf8').includes('#define OTA_PASSWORD "added"'))
})

test('injectOtaPassword escapes quotes and backslashes', (t) => {
  const file = tempFile(t, 'constants.h', '#pragma once\n#define OTA_PASSWORD "x"\n')
  injectOtaPassword('a"b\\c', file)
  const content = readFileSync(file, 'utf8')
  assert.ok(content.includes('#define OTA_PASSWORD "a\\"b\\\\c"'))
})

test('escapeCString escapes backslashes and quotes', () => {
  assert.strictEqual(escapeCString('a"b\\c'), 'a\\"b\\\\c')
})
