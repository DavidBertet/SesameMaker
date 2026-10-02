// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import {
  listFirmwareReleases,
  pickLatestRelease,
  firmwareFileBase,
  parseManifest,
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
      disconnect() {}
    },
    ESPLoader: class {
      async main() {
        return 'ESP32-C6'
      }
      async writeFlash({ fileArray }) {
        written = fileArray
      }
      async hardReset() {}
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

test('parseManifest rejects malformed manifests and chip mismatch', () => {
  assert.throws(() => parseManifest(null), /malformed/)
  assert.throws(() => parseManifest({ files: [] }), /malformed/)
  assert.throws(() => parseManifest({ files: [{ name: 'x', offset: 'nope' }] }), /Bad manifest/)
  assert.throws(() => parseManifest(manifest, { expectedChip: 'ESP32-S3' }), /targets ESP32-C6/)
})
