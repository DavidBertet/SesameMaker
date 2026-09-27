// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import { getSignalStrength } from './wifi.js'

test('getSignalStrength bands', () => {
  assert.equal(getSignalStrength(-20), 'Excellent')
  assert.equal(getSignalStrength(-30), 'Excellent')
  assert.equal(getSignalStrength(-50), 'Good')
  assert.equal(getSignalStrength(-60), 'Fair')
  assert.equal(getSignalStrength(-70), 'Weak')
  assert.equal(getSignalStrength(-100), 'Very Weak')
})
