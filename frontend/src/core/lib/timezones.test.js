// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { TIMEZONES, TIMEZONE_NAMES } from './timezones.js'

test('timezone table covers the full IANA list with POSIX rules', () => {
  assert.ok(TIMEZONE_NAMES.length > 400, `only ${TIMEZONE_NAMES.length} zones`)
  assert.equal(TIMEZONES['Europe/Paris'], 'CET-1CEST,M3.5.0,M10.5.0/3')
  assert.equal(TIMEZONES['America/New_York'], 'EST5EDT,M3.2.0,M11.1.0')
  assert.equal(TIMEZONES['Etc/UTC'], 'UTC0')
  // BC permanent UTC-7 since 2026-11-01 (IANA tzdb 2026b); LA still changes.
  assert.equal(TIMEZONES['America/Vancouver'], 'MST7')
  assert.equal(TIMEZONES['America/Los_Angeles'], 'PST8PDT,M3.2.0,M11.1.0')
  for (const n of TIMEZONE_NAMES) {
    assert.ok(TIMEZONES[n].length > 0, `empty rule for ${n}`)
  }
})
