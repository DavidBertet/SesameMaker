// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import {
  homekitSetupCodeFromUri,
  homekitSetupCodeLines,
  formatHomekitSetupCode,
  HOMEKIT_DIGIT_GLYPHS,
  HOMEKIT_DIGIT_STEP,
} from './homekit.js'

function base36Encode(n) {
  const a = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
  if (n === 0) return '0'
  let s = ''
  while (n > 0) {
    s = a[n % 36] + s
    n = Math.floor(n / 36)
  }
  return s
}

function makeUri(code8, category = 4, setupId = 'ES32') {
  const payload = Number(code8) | (category << 31) | 0x10000000
  return `X-HM://00${base36Encode(payload)}${setupId}`
}

test('decodes mock setup_uri', () => {
  // Mock URI used across the UI: body "527813XES32" -> code 11122333.
  assert.equal(homekitSetupCodeFromUri('X-HM://00527813XES32'), '11122333')
})

test('round-trips an 8-digit code through encode/decode', () => {
  assert.equal(homekitSetupCodeFromUri(makeUri('84131633')), '84131633')
  assert.equal(homekitSetupCodeFromUri(makeUri('00000001', 4, 'AB12')), '00000001')
})

test('rejects garbage', () => {
  assert.equal(homekitSetupCodeFromUri(''), '')
  assert.equal(homekitSetupCodeFromUri(null), '')
  assert.equal(homekitSetupCodeFromUri('https://example.com'), '')
  assert.equal(homekitSetupCodeFromUri('X-HM://00!!!XXXX'), '')
})

test('digit glyphs cover 0-9 with valid path data', () => {
  assert.deepEqual(Object.keys(HOMEKIT_DIGIT_GLYPHS).sort(), [
    '0',
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
  ])
  for (const d of Object.values(HOMEKIT_DIGIT_GLYPHS)) {
    assert.match(d, /^M[\d\sA-Za-z.,-]+Z?/)
  }
  // Row pitch fits 4 glyphs in the 196-wide row viewBox.
  assert.equal(3 * HOMEKIT_DIGIT_STEP + 34, 196)
})

test('splits lines and formats display', () => {
  assert.deepEqual(homekitSetupCodeLines('84131633'), ['8413', '1633'])
  assert.deepEqual(homekitSetupCodeLines('841-31-633'), ['8413', '1633'])
  assert.deepEqual(homekitSetupCodeLines('abc'), ['', ''])
  assert.equal(formatHomekitSetupCode('84131633'), '8413 1633')
  assert.equal(formatHomekitSetupCode('bad'), '')
})
