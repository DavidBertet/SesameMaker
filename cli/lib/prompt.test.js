// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import { EventEmitter } from 'node:events'

const require = createRequire(import.meta.url)

function loadPrompt() {
  delete require.cache[require.resolve('./prompt.js')]
  return require('./prompt.js')
}

const OPTIONS = [
  { label: 'Network — 192.168.1.10', value: { kind: 'ota', ip: '192.168.1.10' } },
  { label: 'USB — /dev/cu.X', value: { kind: 'serial', port: '/dev/cu.X' } },
]

test('selectFromList returns null when there are no options', async () => {
  const p = loadPrompt()
  assert.equal(await p.selectFromList('pick', []), null)
})

test('selectFromList falls back to askQuestion in non-TTY and returns chosen value', async () => {
  const p = loadPrompt()
  // Force the non-TTY path regardless of the real environment.
  const oi = process.stdin.isTTY
  const oiOut = process.stdout.isTTY
  process.stdin.isTTY = false
  process.stdout.isTTY = false
  let asked = null
  try {
    const value = await p.selectFromList('which?', OPTIONS, async (q) => {
      asked = q
      return '2'
    })
    assert.deepEqual(value, { kind: 'serial', port: '/dev/cu.X' })
    assert.equal(asked, 'which?')
  } finally {
    process.stdin.isTTY = oi
    process.stdout.isTTY = oiOut
  }
})

test('selectFromList non-TTY returns null for an out-of-range answer', async () => {
  const p = loadPrompt()
  const oi = process.stdin.isTTY
  const oiOut = process.stdout.isTTY
  process.stdin.isTTY = false
  process.stdout.isTTY = false
  try {
    const value = await p.selectFromList('which?', OPTIONS, async () => '99')
    assert.equal(value, null)
  } finally {
    process.stdin.isTTY = oi
    process.stdout.isTTY = oiOut
  }
})

test('selectFromList interactive: arrow keys move and enter confirms', async () => {
  const origInTTY = process.stdin.isTTY
  const origOutTTY = Object.getOwnPropertyDescriptor(process.stdout, 'isTTY')

  process.stdin.isTTY = true
  Object.defineProperty(process.stdout, 'isTTY', { value: true, configurable: true })
  const written = []
  const origWrite = process.stdout.write
  process.stdout.write = (s) => {
    written.push(String(s))
    return true
  }

  const p = loadPrompt()
  try {
    const promise = p.selectFromList('which?', OPTIONS)
    const keyCb = process.stdin.listeners('keypress')[0]
    assert.ok(keyCb, 'expected a keypress listener')

    // Move down to option 2, then confirm.
    keyCb(null, { name: 'down' })
    keyCb(null, { name: 'enter' })

    const value = await promise
    assert.deepEqual(value, { kind: 'serial', port: '/dev/cu.X' })

    const joined = written.join('')
    assert.ok(joined.includes('Network — 192.168.1.10'))
    assert.ok(joined.includes('USB — /dev/cu.X'))
    assert.ok(joined.includes('\u001b[2A'), 'redraw moved up over 2 option lines')
    assert.ok(joined.includes('\u001b[38;5;208m'), 'selected item uses orange highlight')
    // On confirm the whole list collapses: cursor moves up over prompt+options
    // (3 lines) and clears to end of screen. The selection is NOT reprinted —
    // the caller reports it right after (e.g. "✓ Selected ...").
    assert.ok(joined.includes('\u001b[3A'), 'finish collapses the list')
    assert.ok(joined.includes('\u001b[0J'), 'finish erases the option list')
    assert.ok(joined.endsWith('\u001b[?25h'), 'cursor is re-shown after selection')
    assert.ok(!/\n❯/.test(joined), 'selection is not reprinted into the log')
  } finally {
    process.stdin.isTTY = origInTTY
    process.stdout.write = origWrite
    if (origOutTTY) {
      Object.defineProperty(process.stdout, 'isTTY', origOutTTY)
    } else {
      delete process.stdout.isTTY
    }
  }
})

test('selectFromList interactive: ESC aborts with null', async () => {
  const origInTTY = process.stdin.isTTY
  const origOutTTY = Object.getOwnPropertyDescriptor(process.stdout, 'isTTY')

  process.stdin.isTTY = true
  Object.defineProperty(process.stdout, 'isTTY', { value: true, configurable: true })
  const origWrite = process.stdout.write
  process.stdout.write = () => true

  const p = loadPrompt()
  try {
    const promise = p.selectFromList('which?', OPTIONS)
    const keyCb = process.stdin.listeners('keypress')[0]
    keyCb(null, { name: 'escape' })
    assert.equal(await promise, null)
  } finally {
    process.stdin.isTTY = origInTTY
    process.stdout.write = origWrite
    if (origOutTTY) {
      Object.defineProperty(process.stdout, 'isTTY', origOutTTY)
    } else {
      delete process.stdout.isTTY
    }
  }
})

test('selectFromList interactive: stray characters are ignored and do not change selection', async () => {
  const origInTTY = process.stdin.isTTY
  const origOutTTY = Object.getOwnPropertyDescriptor(process.stdout, 'isTTY')

  process.stdin.isTTY = true
  Object.defineProperty(process.stdout, 'isTTY', { value: true, configurable: true })
  const written = []
  const origWrite = process.stdout.write
  process.stdout.write = (s) => {
    written.push(String(s))
    return true
  }

  const p = loadPrompt()
  try {
    const promise = p.selectFromList('which?', OPTIONS)
    const keyCb = process.stdin.listeners('keypress')[0]

    // Type 'a', 'b', '1', then confirm. Selection should stay on option 1.
    keyCb('a', { name: 'a' })
    keyCb('b', { name: 'b' })
    keyCb('1', { name: '1' })
    keyCb(null, { name: 'enter' })

    const value = await promise
    assert.deepEqual(value, { kind: 'ota', ip: '192.168.1.10' })

    const joined = written.join('')
    const highlightCount = (joined.match(/\u001b\[38;5;208m/g) || []).length
    // Only the initial render highlights (1). Stray letters must not trigger
    // extra redraws, which would add more markers.
    assert.equal(highlightCount, 1, 'stray keys caused an unexpected redraw')
  } finally {
    process.stdin.isTTY = origInTTY
    process.stdout.write = origWrite
    if (origOutTTY) {
      Object.defineProperty(process.stdout, 'isTTY', origOutTTY)
    } else {
      delete process.stdout.isTTY
    }
  }
})
