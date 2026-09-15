// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import fs from 'node:fs'
import path from 'node:path'

const require = createRequire(import.meta.url)
const { parsePioEnv, setEnvBoard } = require('./backend.js')

const INI = `
[env:sesame]
platform = espressif32 @ 6.13.0
board = esp32doit-devkit-v1
framework = espidf
`

test('parsePioEnv reads the env and board', () => {
  assert.deepEqual(parsePioEnv(INI), { env: 'sesame', board: 'esp32doit-devkit-v1' })
  assert.equal(parsePioEnv('[env:sesame]\nboard =\n').board, null)
  assert.equal(parsePioEnv('no sections here'), null)
})

test('setEnvBoard rewrites the board line', () => {
  const out = setEnvBoard(INI, 'sesame', 'esp32-c6-devkitc-1')
  assert.ok(out.includes('[env:sesame]'))
  assert.ok(out.includes('board = esp32-c6-devkitc-1'))
  assert.ok(!out.includes('board = esp32doit-devkit-v1'))
})

test('setEnvBoard with unknown env leaves ini unchanged', () => {
  assert.equal(setEnvBoard(INI, 'nope', 'x'), INI)
})

test('setEnvBoard keeps the preamble and ignores [env:] in comments', () => {
  const ini = `; header comment\n; mentions [env:other] in passing\n${INI}`
  const out = setEnvBoard(ini, 'sesame', 'esp32-c6-devkitc-1')
  assert.ok(out.startsWith('; header comment\n; mentions [env:other] in passing\n'))
  assert.ok(out.includes('board = esp32-c6-devkitc-1'))
})

test('PIO-flashed and IDF-assumed partition tables agree', () => {
  // PIO flashes board_build.partitions; the IDF build assumes
  // CONFIG_PARTITION_TABLE_CUSTOM_FILENAME from sdkconfig.defaults.
  // A mismatch bricks OTA or breaks SPIFFS offsets.
  const ini = fs.readFileSync(path.resolve('backend/platformio.ini'), 'utf8')
  const defaults = fs.readFileSync(path.resolve('backend/sdkconfig.defaults'), 'utf8')
  const assumed = defaults.match(/CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="(.+?)"/)[1]
  const flashed = ini.match(/^\s*board_build\.partitions\s*=\s*(.+?)\s*$/m)[1]
  assert.equal(assumed, flashed)
  // No overlaps, 4K-aligned, fills flash exactly. NOTE: zb_storage is
  // subtype `nvs` per the ESP Zigbee SDK v2.x migration (validated on
  // hardware: the stack joins and persists on it). PIO buildfs sizes the
  // SPIFFS image from the LAST data spiffs/fat/littlefs row, so `spiffs`
  // must stay last.
  const csv = fs.readFileSync(path.resolve('backend', flashed), 'utf8')
  const rows = []
  for (const line of csv.split('\n')) {
    const t = line.trim()
    if (!t || t.startsWith('#')) continue
    const [name, type, subtype, off, size] = t.split(',').map((s) => s.trim())
    rows.push({ name, type, subtype, o: parseInt(off, 16), s: parseInt(size, 16) })
  }
  rows.sort((a, b) => a.o - b.o)
  let end = 0
  for (const r of rows) {
    assert.ok(r.o >= end, `overlapping partition ${r.name}`)
    assert.equal(r.o % 0x1000, 0, `unaligned ${r.name}`)
    assert.equal(r.s % 0x1000, 0, `unaligned ${r.name}`)
    end = r.o + r.s
  }
  assert.equal(end, 0x400000)
  // FS-subtype rows, in layout order: factory/reserved first, spiffs last
  // (buildfs sizes from the last one). NVS rows (nvs, zb_storage,
  // hk_storage) are storage partitions, excluded by design.
  assert.deepEqual(
    rows
      .filter((r) => r.type === 'data' && ['spiffs', 'fat', 'littlefs'].includes(r.subtype))
      .map((r) => r.name),
    ['zb_fct', 'spiffs'],
  )
})
