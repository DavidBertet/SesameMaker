// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import { zigbeeSupported, zigbeeStatusMeta, formatPanId, zigbeeLqiLabel, zigbeeParentLabel } from './zigbee.js'

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
  assert.equal(
    zigbeeStatusMeta({
      supported: true,
      enabled: true,
      pairing_remaining_s: 0,
      joined: false,
      commissioned: true,
    }).label,
    'Reconnecting…',
  )
})

test('formatPanId renders hex or dash', () => {
  assert.equal(formatPanId(0), '—')
  assert.equal(formatPanId(1234), '0x04D2')
})

test('zigbeeLqiLabel grades the 0-255 scale', () => {
  assert.equal(zigbeeLqiLabel(255), 'Excellent')
  assert.equal(zigbeeLqiLabel(200), 'Excellent')
  assert.equal(zigbeeLqiLabel(199), 'Good')
  assert.equal(zigbeeLqiLabel(150), 'Good')
  assert.equal(zigbeeLqiLabel(149), 'Fair')
  assert.equal(zigbeeLqiLabel(100), 'Fair')
  assert.equal(zigbeeLqiLabel(99), 'Weak')
  assert.equal(zigbeeLqiLabel(0), 'Weak')
})

test('zigbeeParentLabel names the parent', () => {
  assert.equal(zigbeeParentLabel(null), '')
  assert.equal(zigbeeParentLabel({ lqi_valid: false }), '')
  assert.equal(
    zigbeeParentLabel({ lqi_valid: true, parent_depth: 0, parent_addr: 0 }),
    'via coordinator',
  )
  assert.equal(
    zigbeeParentLabel({ lqi_valid: true, parent_depth: 1, parent_addr: 0x1234 }),
    'via 0x1234',
  )
})
