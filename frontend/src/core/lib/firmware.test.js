// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import {
  listFirmwareReleases,
  pickLatestRelease,
  firmwareFileBase,
  parseManifest,
  flashDevice,
  installRelease,
} from './firmware.js'

test('listFirmwareReleases drops drafts and keeps newest first', async () => {
  const fetchFn = async () => ({
    ok: true,
    json: async () => [
      {
        tag_name: 'v1.2.0',
        name: 'v1.2.0',
        draft: false,
        prerelease: false,
        published_at: '2026-09-01T00:00:00Z',
      },
      {
        tag_name: 'v1.3.0-beta',
        name: '',
        draft: false,
        prerelease: true,
        published_at: '2026-09-20T00:00:00Z',
      },
      { tag_name: 'wip', name: 'wip', draft: true, prerelease: false, published_at: '' },
    ],
  })
  const releases = await listFirmwareReleases({ owner: 'o', repo: 'r', fetchFn })
  assert.deepEqual(
    releases.map((r) => r.tag),
    ['v1.2.0', 'v1.3.0-beta'],
  )
  assert.equal(releases[1].name, 'v1.3.0-beta')
  assert.equal(releases[0].publishedAt, '2026-09-01')
})

test('listFirmwareReleases throws with the HTTP status', async () => {
  const fetchFn = async () => ({ ok: false, status: 403 })
  await assert.rejects(listFirmwareReleases({ owner: 'o', repo: 'r', fetchFn }), /HTTP 403/)
})

test('pickLatestRelease prefers stable, falls back, handles empty', () => {
  assert.equal(pickLatestRelease([]), null)
  assert.equal(pickLatestRelease(null), null)
  const stable = { tag: 'v1.0.0', prerelease: false }
  const beta = { tag: 'v1.1.0-beta', prerelease: true }
  assert.equal(pickLatestRelease([beta, stable]), stable)
  assert.equal(pickLatestRelease([beta]), beta)
})

test('firmwareFileBase points at the same-origin fw bundle', () => {
  assert.equal(firmwareFileBase('v1.0.0', '/SesameMaker/'), '/SesameMaker/fw/v1.0.0')
  assert.equal(firmwareFileBase('v1.0.0', '/SesameMaker'), '/SesameMaker/fw/v1.0.0')
  assert.equal(firmwareFileBase('v1.0.0'), '/fw/v1.0.0')
})

test('flashDevice falls back to slower baud, then uncompressed', async () => {
  const seenBaud = []
  const seenCompress = []
  let calls = 0
  const esptool = {
    Transport: class {
      async setDTR() {}
      async setRTS() {}
      disconnect() {}
    },
    ESPLoader: class {
      constructor({ baudrate }) {
        seenBaud.push(baudrate)
      }
      async main() {
        return 'ESP32-C6'
      }
      async writeFlash({ compress }) {
        seenCompress.push(compress)
        calls += 1
        if (calls === 1)
          throw new Error('Failed to enter compressed flash mode failed with status 196,0')
      }
    },
  }
  const { chip, reset } = await flashDevice({ port: {}, files: [], esptool })
  assert.equal(chip, 'ESP32-C6')
  assert.equal(reset, true)
  assert.deepEqual(seenBaud, [921600, 460800])
  assert.deepEqual(seenCompress, [true, true])
})

test('flashDevice exits download mode with the EN pulse (DTR low, RTS pulse)', async () => {
  const signals = []
  const esptool = {
    Transport: class {
      async setDTR(state) {
        signals.push(['DTR', state])
      }
      async setRTS(state) {
        signals.push(['RTS', state])
      }
      disconnect() {}
    },
    ESPLoader: class {
      async main() {
        return 'ESP32-C6'
      }
      async writeFlash() {}
    },
  }
  const { chip, reset } = await flashDevice({ port: {}, files: [], esptool })
  assert.equal(chip, 'ESP32-C6')
  assert.equal(reset, true)
  assert.deepEqual(signals, [
    ['DTR', false],
    ['RTS', true],
    ['RTS', false],
  ])
})

test('flashDevice reports failure when the reset lines are un-drivable', async () => {
  const esptool = {
    Transport: class {
      async setDTR() {}
      async setRTS() {
        throw new Error('port gone')
      }
      disconnect() {}
    },
    ESPLoader: class {
      async main() {
        return 'ESP32-C6'
      }
      async writeFlash() {}
    },
  }
  const { reset } = await flashDevice({ port: {}, files: [], esptool })
  assert.equal(reset, false)
})

test('flashDevice last resort is slow and uncompressed, then gives up loudly', async () => {
  const esptool = {
    Transport: class {
      async setDTR() {}
      async setRTS() {}
      disconnect() {}
    },
    ESPLoader: class {
      async main() {
        return 'ESP32-C6'
      }
      async writeFlash() {
        throw new Error('boom')
      }
    },
  }
  await assert.rejects(
    flashDevice({ port: {}, files: [], esptool }),
    /Flash failed \(3 attempts, last: boom\)/,
  )
})

test('flashDevice with explicit baudrate tries once', async () => {
  let transports = 0
  const esptool = {
    Transport: class {
      constructor() {
        transports += 1
      }
      async setDTR() {}
      async setRTS() {}
      disconnect() {}
    },
    ESPLoader: class {
      async main() {
        return 'ESP32-C6'
      }
      async writeFlash() {
        throw new Error('boom')
      }
    },
  }
  await assert.rejects(
    flashDevice({ port: {}, files: [], baudrate: 115200, esptool }),
    /1 attempts/,
  )
  assert.equal(transports, 1)
})

test('installRelease downloads manifest+images same-origin, then burns', async () => {
  const seen = []
  const bytes = (s) => new TextEncoder().encode(s).buffer
  const fetchFn = async (url) => {
    seen.push(url)
    if (url === 'https://site/fw/v1.0.0/manifest.json') {
      return { ok: true, json: async () => manifest }
    }
    if (url.startsWith('https://site/fw/v1.0.0/fw-')) {
      return { ok: true, arrayBuffer: async () => bytes(`data:${url}`) }
    }
    throw new Error(`unexpected fetch ${url}`)
  }
  let written = null
  const esptool = {
    Transport: class {
      async setDTR() {}
      async setRTS() {}
      disconnect() {}
    },
    ESPLoader: class {
      async main() {
        return 'ESP32-C6'
      }
      async writeFlash({ fileArray }) {
        written = fileArray
      }
    },
  }
  const { chip } = await installRelease({
    fwBase: 'https://site/fw/v1.0.0',
    manifestName: 'manifest.json',
    expectedChip: 'ESP32-C6',
    fetchFn,
    requestPort: async () => ({}),
    esptool,
  })
  assert.equal(chip, 'ESP32-C6')
  assert.deepEqual(seen, [
    'https://site/fw/v1.0.0/manifest.json',
    'https://site/fw/v1.0.0/fw-factory.bin',
    'https://site/fw/v1.0.0/fw-spiffs.bin',
  ])
  assert.deepEqual(
    written.map((f) => f.address),
    [0x0, 0x37c000],
  )
})

test('installRelease throws when the version has no fw bundle', async () => {
  const fetchFn = async () => ({ ok: false, status: 404 })
  await assert.rejects(
    installRelease({
      fwBase: 'https://site/fw/v9.9.9',
      manifestName: 'manifest.json',
      fetchFn,
      requestPort: async () => ({}),
    }),
    /No install files for this version \(HTTP 404\)/,
  )
})

const manifest = {
  version: 'v1.0.0',
  chip: 'ESP32-C6',
  files: [
    { name: 'fw-factory.bin', offset: '0x0', size: 100 },
    { name: 'fw-spiffs.bin', offset: '0x37C000', size: 200 },
  ],
}

test('parseManifest validates entries and parses hex offsets', () => {
  assert.deepEqual(parseManifest(manifest, { expectedChip: 'ESP32-C6' }), [
    { name: 'fw-factory.bin', address: 0x0, size: 100 },
    { name: 'fw-spiffs.bin', address: 0x37c000, size: 200 },
  ])
})

test('parseManifest accepts decimal offsets (shipped manifests)', () => {
  assert.deepEqual(
    parseManifest(
      {
        version: 'v0.0.4',
        chip: 'ESP32-C6',
        files: [
          { name: 'factory.bin', offset: '0x0', size: 1632784 },
          { name: 'spiffs.bin', offset: '3653632', size: 540672 },
        ],
      },
      { expectedChip: 'ESP32-C6' },
    ),
    [
      { name: 'factory.bin', address: 0x0, size: 1632784 },
      { name: 'spiffs.bin', address: 0x37c000, size: 540672 },
    ],
  )
})

test('parseManifest rejects absurd addresses instead of bricking the flash', () => {
  assert.throws(
    () =>
      parseManifest({
        files: [{ name: 'spiffs.bin', offset: '0x3653632', size: 540672 }],
      }),
    /Bad manifest/,
  )
})

test('parseManifest handles the discrete-parts layout (no NVS coverage)', () => {
  const parts = parseManifest(
    {
      version: 'v9.9.9',
      chip: 'ESP32-C6',
      files: [
        { name: 'x-bootloader.bin', offset: '0x0', size: 20000 },
        { name: 'x-partitions.bin', offset: '0x8000', size: 3072 },
        { name: 'x-otadata-initial.bin', offset: '0xd000', size: 8192 },
        { name: 'x-app.bin', offset: '0x10000', size: 1500000 },
        { name: 'x-spiffs.bin', offset: '0x37c000', size: 540672 },
      ],
    },
    { expectedChip: 'ESP32-C6' },
  )
  assert.deepEqual(
    parts.map((p) => p.address),
    [0x0, 0x8000, 0xd000, 0x10000, 0x37c000],
  )
  // NVS (0x9000), phy (0xf000) and the data stores are never written.
  for (const covered of [0x9000, 0xf000, 0x370000, 0x374000, 0x378000]) {
    assert.ok(
      parts.every((p) => covered < p.address || covered >= p.address + p.size),
      `0x${covered.toString(16)} must not be covered`,
    )
  }
})

test('parseManifest rejects malformed manifests and chip mismatch', () => {
  assert.throws(() => parseManifest(null), /malformed/)
  assert.throws(() => parseManifest({ files: [] }), /malformed/)
  assert.throws(() => parseManifest({ files: [{ name: 'x', offset: 'nope' }] }), /Bad manifest/)
  assert.throws(() => parseManifest(manifest, { expectedChip: 'ESP32-S3' }), /targets ESP32-C6/)
})
