// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import fs from 'node:fs'
import path from 'node:path'

// Regression guard: register_callback() silently drops types past
// MAX_CALLBACKS (a backend ESP_LOGE now, but still dead handlers).
// This once killed set_zigbee_config + log_start with zero UI feedback.
test('registered callbacks fit the table', () => {
  const ws = fs.readFileSync(path.resolve('backend/src/core/websocket.c'), 'utf8')
  const max = parseInt(ws.match(/#define\s+MAX_CALLBACKS\s+(\d+)/)[1], 10)
  const countIn = (file) =>
    (fs.readFileSync(path.resolve(file), 'utf8').match(/register_callback\s*\(/g) || []).length
  const total = countIn('backend/src/core/websocket.c') + countIn('backend/src/app/main.c')
  assert.ok(total <= max, `${total} registrations exceed MAX_CALLBACKS=${max}`)
})
