// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import http from 'node:http'

const require = createRequire(import.meta.url)

function loadOta() {
  delete require.cache[require.resolve('./ota.js')]
  return require('./ota.js')
}

async function startTestServer(onUpload) {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => {
      const chunks = []
      req.on('data', (c) => chunks.push(c))
      req.on('end', () => {
        if (onUpload) onUpload(req, Buffer.concat(chunks))
        res.writeHead(200)
        res.end('ok')
      })
    })
    server.listen(0, () => resolve(server))
  })
}

test('falls back to byte progress when WebSocket fails', async () => {
  const { uploadFilesHTTP } = loadOta()

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ota-test-'))
  const filePath = path.join(dir, 'firmware.bin')
  const payload = Buffer.alloc(128 * 1024, 0xab)
  fs.writeFileSync(filePath, payload)

  let received = 0
  const server = await startTestServer((req, body) => {
    received += body.length
  })
  const port = server.address().port

  const httpMod = require('http')
  const origRequest = httpMod.request
  httpMod.request = (opts, cb) => {
    return origRequest({ ...opts, port }, cb)
  }

  const out = []
  const origStdout = process.stdout.write
  process.stdout.write = (s) => {
    out.push(s.toString())
    return true
  }

  try {
    const results = await uploadFilesHTTP('127.0.0.1', 'pass', [filePath])
    assert.strictEqual(results[0].success, true)
    assert.strictEqual(received, payload.length)
  } finally {
    process.stdout.write = origStdout
    httpMod.request = origRequest
    server.close()
    fs.rmSync(dir, { recursive: true, force: true })
  }

  const joined = out.join('')
  // WS unreachable → fallback bar advanced by bytes and reached 100%
  assert.ok(joined.includes('continuing without progress tracking'), 'expected fallback message')
  assert.ok(
    joined.includes('0%') && joined.includes('50%') && joined.includes('100%'),
    'expected fallback bar to advance across percentages',
  )
})
