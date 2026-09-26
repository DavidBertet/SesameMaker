// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)
const { generateOtaPassword } = require('./password.js')

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
