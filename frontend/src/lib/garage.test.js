// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import {
  doorMeta,
  isSettled,
  parseGarageStatus,
  decodeByte,
  decodeDoorResp,
  planDoorTransition,
  optimisticLight,
  optimisticLock,
} from './garage.js'

test('doorMeta covers every state', () => {
  for (const state of ['unknown', 'open', 'closed', 'opening', 'closing', 'stopped']) {
    const meta = doorMeta(state)
    assert.equal(typeof meta.label, 'string')
    assert.ok(meta.label.length > 0)
  }
})

test('isSettled distinguishes moving from settled doors', () => {
  assert.equal(isSettled('open'), true)
  assert.equal(isSettled('closed'), true)
  assert.equal(isSettled('stopped'), true)
  assert.equal(isSettled('opening'), false)
  assert.equal(isSettled('closing'), false)
  assert.equal(isSettled('unknown'), false)
})

test('parseGarageStatus fills defaults for missing fields', () => {
  assert.deepEqual(parseGarageStatus({}), {
    protocol: 'secplus1',
    caps: {
      light: true,
      lock: true,
      obstruction: true,
      motion: true,
      panel: true,
      sensors: false,
    },
    door: 'unknown',
    moving: false,
    light: 'unknown',
    locked: 'unknown',
    obstruction: false,
    motion: false,
    panel: 'waiting',
    sensors: { open: false, close: false, valid: false },
  })
  const secplus = parseGarageStatus({
    door: 'open',
    light: 'on',
    locked: 'locked',
    panel: 'detected',
  })
  assert.equal(secplus.protocol, 'secplus1')
  assert.equal(secplus.caps.light, true)
  assert.equal(secplus.panel, 'detected')
  // Garbage values fall back to unknown instead of poisoning the store.
  const parsed = parseGarageStatus({ door: 'warp', light: 42 })
  assert.equal(parsed.door, 'unknown')
  assert.equal(parsed.light, 'unknown')
})

test('parseGarageStatus derives dry-contact caps by protocol', () => {
  const dry = parseGarageStatus({ protocol: 'drycontact', panel: 'none' })
  assert.equal(dry.protocol, 'drycontact')
  assert.equal(dry.caps.light, false)
  assert.equal(dry.caps.lock, false)
  assert.equal(dry.caps.sensors, true)
  assert.equal(dry.panel, 'none')
  // Explicit caps from a new backend always win over the default.
  const explicit = parseGarageStatus({
    protocol: 'drycontact',
    caps: {
      light: false,
      lock: false,
      obstruction: false,
      motion: false,
      panel: false,
      sensors: true,
    },
    sensors: { open: false, close: true, valid: true },
  })
  assert.deepEqual(explicit.sensors, { open: false, close: true, valid: true })
})

test('decodeByte names known secplus1 command bytes', () => {
  assert.equal(decodeByte(0x30), 'TOGGLE_DOOR_PRESS')
  assert.equal(decodeByte(0x38), 'QUERY_DOOR_STATUS')
  assert.equal(decodeByte(0x3a), 'QUERY_OTHER_STATUS')
  assert.equal(decodeByte(0x00), null)
  assert.equal(decodeByte(0xff), null)
})

test('decodeDoorResp matches the secplus1 table', () => {
  assert.equal(decodeDoorResp(0x05), 'closed')
  assert.equal(decodeDoorResp(0x02), 'open')
  assert.equal(decodeDoorResp(0x01), 'opening')
  assert.equal(decodeDoorResp(0x04), 'closing')
  assert.equal(decodeDoorResp(0x00), 'stopped')
  assert.equal(decodeDoorResp(0x06), 'stopped')
  assert.equal(decodeDoorResp(0x03), 'unknown')
})

test('planDoorTransition animates toggle from each state', () => {
  const open = planDoorTransition('closed', 'toggle')
  assert.equal(open.length, 2)
  assert.equal(open[0].door, 'opening')
  assert.equal(open[open.length - 1].door, 'open')
  assert.ok(open[1].delay > open[0].delay)

  const close = planDoorTransition('open', 'toggle')
  assert.equal(close[close.length - 1].door, 'closed')

  // Toggling mid-travel stops or reverses immediately.
  assert.deepEqual(planDoorTransition('opening', 'toggle'), [
    { delay: 0, door: 'stopped', moving: false },
  ])
})

test('planDoorTransition is a no-op when target already reached', () => {
  assert.deepEqual(planDoorTransition('open', 'open'), [])
  assert.deepEqual(planDoorTransition('closed', 'close'), [])
  assert.deepEqual(planDoorTransition('open', 'stop'), [])
})

test('optimisticLight resolves to the requested state', () => {
  assert.equal(optimisticLight('on', 'off'), 'on')
  assert.equal(optimisticLight('off', 'on'), 'off')
  assert.equal(optimisticLight('on', 'unknown'), 'on')
})

test('optimisticLight ignores unknown actions', () => {
  assert.equal(optimisticLight('toggle', 'off'), 'off')
  assert.equal(optimisticLight(undefined, 'on'), 'on')
})

test('optimisticLock maps lock/unlock', () => {
  assert.equal(optimisticLock('lock', 'unlocked'), 'locked')
  assert.equal(optimisticLock('unlock', 'locked'), 'unlocked')
})
