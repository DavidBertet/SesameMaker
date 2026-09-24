// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Seam test for the discover.js split: the facade must re-export every owned
// function by identity (no wrappers, no drift) and preserve the full surface.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)

test('discover.js re-exports net_scan/usb_list/select by identity', () => {
  const d = require('./discover.js')
  const net = require('./net_scan.js')
  const usb = require('./usb_list.js')
  const sel = require('./select.js')

  for (const mod of [net, usb, sel]) {
    for (const [key, fn] of Object.entries(mod)) {
      assert.strictEqual(d[key], fn, `discover.${key} must be the split module's function`)
    }
  }

  const expected = [...Object.keys(net), ...Object.keys(usb), ...Object.keys(sel)].sort()
  assert.deepEqual(Object.keys(d).sort(), expected)
})
