// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import { zigbeeSupported, zigbeeStatusMeta, formatPanId } from './zigbee.js'

test('zigbeeSupported gates on settings.features.zigbee', () => {
  assert.equal(zigbeeSupported({}), false)
  assert.equal(zigbeeSupported({ features: { zigbee: false } }), false)
  assert.equal(zigbeeSupported({ features: { zigbee: true } }), true)
})

test('zigbeeStatusMeta covers all states', () => {
  assert.equal(zigbeeStatusMeta({ supported: false }).label, 'Unavailable')
  assert.equal(zigbeeStatusMeta({ supported: true, enabled: false }).label, 'Disabled')
  assert.equal(
    zigbeeStatusMeta({ supported: true, enabled: true, pairing_remaining_s: 42 }).label,
    'Pairing…',
  )
  assert.equal(
    zigbeeStatusMeta({ supported: true, enabled: true, pairing_remaining_s: 0, joined: true })
      .label,
    'Joined',
  )
  assert.equal(
    zigbeeStatusMeta({ supported: true, enabled: true, pairing_remaining_s: 0, joined: false })
      .label,
    'Not joined',
  )
})

test('formatPanId renders hex or dash', () => {
  assert.equal(formatPanId(0), '—')
  assert.equal(formatPanId(1234), '0x04D2')
})
