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
  // No overlaps, contiguous layout.
  const csv = fs.readFileSync(path.resolve('backend', flashed), 'utf8')
  let end = 0
  for (const line of csv.split('\n')) {
    const t = line.trim()
    if (!t || t.startsWith('#')) continue
    const [, , , off, size] = t.split(',').map((s) => s.trim())
    const o = parseInt(off, 16)
    const s = parseInt(size, 16)
    assert.ok(o >= end, 'overlapping partitions')
    end = o + s
  }
  assert.equal(end, 0x400000)
})
