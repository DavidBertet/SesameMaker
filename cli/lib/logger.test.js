// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

test('setDebug toggles debug logging', () => {
  // logger module is not an ES module; grab fresh state via require cache clear
  delete require.cache[require.resolve('./logger.js')]
  const { setDebug, isDebug } = require('./logger.js')

  assert.equal(isDebug(), false)
  setDebug(true)
  assert.equal(isDebug(), true)
  setDebug(false)
  assert.equal(isDebug(), false)
  setDebug('truthy-but-not-bool')
  assert.equal(isDebug(), true)
})

test('logger.debug writes only when enabled', () => {
  const logs = []
  const originalLog = console.log
  console.log = (msg) => logs.push(msg)

  try {
    delete require.cache[require.resolve('./logger.js')]
    const { logger, setDebug } = require('./logger.js')

    setDebug(false)
    logger.debug('hidden')
    setDebug(true)
    logger.debug('shown')
  } finally {
    console.log = originalLog
  }

  assert.equal(logs.length, 1)
  assert.match(logs[0], /shown/)
})
