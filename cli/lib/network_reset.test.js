// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

function loadNetworkReset() {
  delete require.cache[require.resolve('./network_reset.js')]
  return require('./network_reset.js')
}

function deps(over = {}) {
  return {
    improv: {
      resolvePortPath: async (p) => p || '/dev/ttyUSB0',
      networkReset: async () => {},
      ...over.improv,
    },
    prompt: { askQuestion: async () => 'RESET', ...over.prompt },
  }
}

test('runNetworkReset aborts without a USB port', async () => {
  const { runNetworkReset } = loadNetworkReset()
  let called = false
  const d = deps({
    improv: { resolvePortPath: async () => null, networkReset: async () => ((called = true), {}) },
  })
  assert.equal(await runNetworkReset({ serialPort: '/dev/x' }, d), false)
  assert.equal(called, false)
})

test('runNetworkReset requires typed RESET without -y', async () => {
  const { runNetworkReset } = loadNetworkReset()
  let called = false
  const d = deps({
    improv: { networkReset: async () => ((called = true), {}) },
    prompt: { askQuestion: async () => 'nope' },
  })
  assert.equal(await runNetworkReset({}, d), false)
  assert.equal(called, false)
})

test('runNetworkReset proceeds on RESET and on -y without prompting', async () => {
  const { runNetworkReset } = loadNetworkReset()
  const ports = []
  const d = deps({ improv: { networkReset: async (p) => void ports.push(p) } })
  assert.equal(await runNetworkReset({}, d), true)
  assert.deepEqual(ports, ['/dev/ttyUSB0'])

  let prompted = false
  const d2 = deps({
    improv: { networkReset: async () => {} },
    prompt: { askQuestion: async () => ((prompted = true), 'RESET') },
  })
  assert.equal(await runNetworkReset({ autoYes: true }, d2), true)
  assert.equal(prompted, false)
})

test('runNetworkReset returns false when the device refuses', async () => {
  const { runNetworkReset } = loadNetworkReset()
  const d = deps({
    improv: {
      networkReset: async () => {
        throw new Error('timed out')
      },
    },
  })
  assert.equal(await runNetworkReset({ autoYes: true }, d), false)
})
