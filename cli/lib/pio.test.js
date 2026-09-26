// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

function loadPio() {
  delete require.cache[require.resolve('./pio.js')]
  return require('./pio.js')
}

test('formatDuration renders ms, seconds and minutes', async () => {
  const { formatDuration } = loadPio()
  assert.match(formatDuration(250), /^250ms$/)
  assert.match(formatDuration(1500), /^1\.5s$/)
  assert.match(formatDuration(125000), /^2m5s$/)
})

test('reportBuildFailure exits and hints fix for corrupted managed_components', async () => {
  const { reportBuildFailure } = loadPio()

  const logs = []
  const originalLog = console.log
  const originalExit = process.exit
  console.log = (...a) => logs.push(a.join(' '))
  process.exit = (code) => {
    throw new Error(`exit:${code}`)
  }

  try {
    reportBuildFailure(
      {
        stderr:
          "OSError: [Errno 66] Directory not empty: '/x/backend/managed_components/espressif__led_strip'",
      },
      '/x/backend',
      Date.now() - 60000,
    )
    assert.fail('should have exited')
  } catch (e) {
    assert.match(String(e), /exit:1/)
  } finally {
    console.log = originalLog
    process.exit = originalExit
  }

  const output = logs.join('\n')
  assert.match(output, /managed_components/)
  assert.match(output, /rm -rf/)
  assert.match(output, /Build failed after 1m0s/)
})

test('firmwareVersionEnv reads version and sha from git', async () => {
  const origExec = require('./system').execPromise
  const seen = []
  require('./system').execPromise = async (cmd) => {
    seen.push(cmd)
    if (cmd.startsWith('git describe')) return { stdout: 'v1.2.3-4-gabc1234-dirty\n' }
    if (cmd.startsWith('git rev-parse')) return { stdout: 'abc1234\n' }
    throw new Error(`unexpected: ${cmd}`)
  }
  try {
    const { firmwareVersionEnv } = loadPio()
    assert.deepEqual(await firmwareVersionEnv(), {
      FW_VERSION: 'v1.2.3-4-gabc1234-dirty',
      FW_GIT_SHA: 'abc1234',
    })
    assert.equal(seen.length, 2)
  } finally {
    require('./system').execPromise = origExec
  }
})

test('firmwareVersionEnv returns {} when git is unavailable', async () => {
  const origExec = require('./system').execPromise
  require('./system').execPromise = async () => {
    throw new Error('git failed')
  }
  try {
    const { firmwareVersionEnv } = loadPio()
    assert.deepEqual(await firmwareVersionEnv(), {})
  } finally {
    require('./system').execPromise = origExec
  }
})
