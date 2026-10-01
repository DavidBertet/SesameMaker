// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { __test, notifyDoorEvent } from './notify.js'

test('fmtElapsed renders m:ss', () => {
  assert.equal(__test.fmtElapsed(0), '0:00')
  assert.equal(__test.fmtElapsed(65), '1:05')
  assert.equal(__test.fmtElapsed(600), '10:00')
})

test('notifyDoorEvent degrades without browser APIs', async () => {
  // No window/Notification in node: must not throw.
  globalThis.window = {}
  assert.doesNotThrow(() => notifyDoorEvent('warn', 65))
  assert.doesNotThrow(() => notifyDoorEvent('bogus', 0))
  delete globalThis.window
})
