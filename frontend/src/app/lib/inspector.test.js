// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import { pinTypeLabel, pinLevelText, pinMeaning, fmtAge, doorSummary } from './inspector.js'

import { busStatusMeta, formatI2cAddr, busMeaning } from './inspector.js'
test('pinTypeLabel covers every inspector type', () => {
  assert.equal(pinTypeLabel('digital'), 'DIGITAL')
  assert.equal(pinTypeLabel('analog'), 'ANALOG')
  assert.equal(pinTypeLabel('uart'), 'UART')
  assert.equal(pinTypeLabel('i2c'), 'I2C')
  assert.equal(pinTypeLabel('spi'), 'SPI')
  assert.equal(pinTypeLabel('bogus'), 'DIGITAL')
  assert.equal(pinTypeLabel(undefined), 'DIGITAL')
})

test('pinLevelText dashes analog, words otherwise', () => {
  assert.equal(pinLevelText({ type: 'analog', raw: 1, mv: 2 }), '—')
  assert.equal(pinLevelText({ type: 'digital', level: 1 }), 'HIGH')
  assert.equal(pinLevelText({ type: 'uart', level: 0 }), 'LOW')
})

test('pinMeaning formats analog as mV + raw', () => {
  assert.equal(pinMeaning({ type: 'analog', raw: 2345, mv: 1820 }), '1820 mV (raw 2345)')
  assert.equal(pinMeaning({ type: 'analog' }), 'read error')
  assert.equal(pinMeaning({ type: 'analog', raw: 100 }), 'read error')
})

test('pinMeaning prefers hit, then detail, then mode', () => {
  assert.equal(
    pinMeaning({ type: 'digital', mode: 'input', level: 0, hit: true, edges: 0 }),
    'hit · LOW · no edges yet',
  )
  assert.equal(
    pinMeaning({ type: 'digital', mode: 'input', level: 1, hit: false, edges: 0 }),
    'clear · HIGH · no edges yet',
  )
  assert.equal(
    pinMeaning({ type: 'uart', mode: 'output', level: 0, detail: '1200 baud 8E1', edges: 0 }),
    '1200 baud 8E1 · LOW · no edges yet',
  )
  assert.equal(pinMeaning({ type: 'digital', mode: 'output', level: 1 }), 'driving HIGH')
  assert.equal(pinMeaning({ type: 'digital', mode: 'input', level: 1 }), 'idle HIGH')
})

test('fmtAge renders ms compactly', () => {
  assert.equal(fmtAge(340), '0.3s')
  assert.equal(fmtAge(0), '0.0s')
  assert.equal(fmtAge(12000), '12s')
  assert.equal(fmtAge(90000), '2m')
  assert.equal(fmtAge(undefined), '—')
})

test('pinMeaning appends edge activity when present', () => {
  assert.equal(
    pinMeaning({ type: 'uart', mode: 'input', level: 1, edges: 47, idle_ms: 340 }),
    'idle HIGH · 47 edges (idle 0.3s)',
  )
  assert.equal(
    pinMeaning({ type: 'digital', mode: 'input', level: 0, hit: true, edges: 3 }),
    'hit · LOW · 3 edges',
  )
  // Analog never carries edges.
  assert.equal(pinMeaning({ type: 'analog', raw: 2345, mv: 1820 }), '1820 mV (raw 2345)')
})

test('busStatusMeta labels every bus state', () => {
  assert.equal(busStatusMeta('valid').label, 'VALID')
  assert.equal(busStatusMeta('stale').label, 'STALE')
  assert.equal(busStatusMeta('error').label, 'ERROR')
  assert.equal(busStatusMeta('error').variant, 'destructive')
  assert.equal(busStatusMeta('no-traffic').label, 'NO TRAFFIC')
  assert.equal(busStatusMeta('bogus').label, 'NO TRAFFIC')
})

test('formatI2cAddr renders hex', () => {
  assert.equal(formatI2cAddr(0x48), '0x48')
  assert.equal(formatI2cAddr(7), '0x07')
})

test('busMeaning summarizes frames, age and addresses', () => {
  assert.equal(busMeaning({ status: 'no-traffic' }), 'listening — no frames yet')
  assert.equal(
    busMeaning({ status: 'valid', frames_ok: 128, frame_errors: 0, idle_ms: 340 }),
    '128 frames, 0 errors (idle 0.3s)',
  )
  assert.equal(
    busMeaning({
      status: 'valid',
      frames_ok: 14,
      frame_errors: 0,
      idle_ms: 120,
      addresses: [{ addr: 0x48, reads: 5, writes: 9 }],
    }),
    '14 frames, 0 errors (idle 0.1s) · 0x48 R5/W9',
  )
  assert.equal(
    busMeaning({ status: 'error', frames_ok: 0, frame_errors: 3 }),
    '0 frames, 3 errors (3 framing)',
  )
})

test('busMeaning breaks errors down by kind and age', () => {
  assert.equal(
    busMeaning({ status: 'valid', frames_ok: 22, frame_errors: 44, idle_ms: 50 }),
    '22 frames, 44 errors (44 framing) (idle 0.1s)',
  )
  assert.equal(
    busMeaning({
      status: 'error',
      frames_ok: 22,
      frame_errors: 44,
      parity_errors: 4,
      idle_ms: 50,
      error_idle_ms: 100,
    }),
    '22 frames, 44 errors (40 framing, 4 parity), last 0.1s ago (idle 0.1s)',
  )
})

test('doorSummary builds name/value rows from store lookalikes', () => {
  const garage = {
    protocol: 'dry',
    door: 'closed',
    moving: false,
    light: 'off',
    locked: false,
    obstruction: false,
    motion: true,
    panel: 'idle',
    sensors: { valid: true, open: false, close: true },
  }
  const rows = Object.fromEntries(
    doorSummary(garage, { loaded: true, id: 'secplus1' }, {
      relay_gpio: 4,
      open_gpio: 5,
      close_gpio: 6,
      sensor_mode: 'both',
    }),
  )
  assert.equal(rows.Protocol, 'secplus1')
  assert.equal(rows.Door, 'closed')
  assert.equal(rows.Obstruction, 'clear')
  assert.equal(rows.Motion, 'yes')
  assert.equal(rows.Sensors, 'open=clear close=hit')
  assert.equal(rows['Dry config'], 'relay=GPIO4 open=GPIO5 close=GPIO6 mode=both')
})

test('doorSummary falls back when protocol unloaded and door moving', () => {
  const rows = Object.fromEntries(
    doorSummary(
      {
        protocol: 'secplus1',
        door: 'unknown',
        moving: true,
        light: 'on',
        locked: true,
        obstruction: true,
        motion: false,
        panel: 'busy',
        sensors: { valid: false },
      },
      { loaded: false },
      { relay_gpio: 4, open_gpio: -1, close_gpio: 6, sensor_mode: 'close' },
    ),
  )
  assert.equal(rows.Protocol, 'secplus1')
  assert.equal(rows.Door, 'unknown (moving)')
  assert.equal(rows.Obstruction, 'obstructed')
  assert.equal(rows.Sensors, 'no sensors')
})
