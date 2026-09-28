// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

function loadFactoryReset() {
  delete require.cache[require.resolve('./factory_reset.js')]
  return require('./factory_reset.js')
}

function deps(over = {}) {
  return {
    improv: {
      resolvePortPath: async (p) => p || '/dev/ttyUSB0',
      factoryReset: async () => {},
      ...over.improv,
    },
    prompt: { askQuestion: async () => 'ERASE', ...over.prompt },
  }
}

test('runFactoryReset aborts without a USB port', async () => {
  const { runFactoryReset } = loadFactoryReset()
  let called = false
  const d = deps({
    improv: { resolvePortPath: async () => null, factoryReset: async () => ((called = true), {}) },
  })
  assert.equal(await runFactoryReset({ serialPort: '/dev/x' }, d), false)
  assert.equal(called, false)
})

test('runFactoryReset requires typed ERASE without -y (RESET is not enough)', async () => {
  const { runFactoryReset } = loadFactoryReset()
  let called = false
  const d = deps({
    improv: { factoryReset: async () => ((called = true), {}) },
    prompt: { askQuestion: async () => 'RESET' },
  })
  assert.equal(await runFactoryReset({}, d), false)
  assert.equal(called, false)
})

test('runFactoryReset proceeds on ERASE and on -y without prompting', async () => {
  const { runFactoryReset } = loadFactoryReset()
  const ports = []
  const d = deps({ improv: { factoryReset: async (p) => void ports.push(p) } })
  assert.equal(await runFactoryReset({}, d), true)
  assert.deepEqual(ports, ['/dev/ttyUSB0'])

  let prompted = false
  const d2 = deps({
    improv: { factoryReset: async () => {} },
    prompt: { askQuestion: async () => ((prompted = true), 'ERASE') },
  })
  assert.equal(await runFactoryReset({ autoYes: true }, d2), true)
  assert.equal(prompted, false)
})

test('runFactoryReset returns false when the device refuses', async () => {
  const { runFactoryReset } = loadFactoryReset()
  const d = deps({
    improv: {
      factoryReset: async () => {
        throw new Error('timed out')
      },
    },
  })
  assert.equal(await runFactoryReset({ autoYes: true }, d), false)
})
