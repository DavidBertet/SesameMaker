// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import {
  listFirmwareReleases,
  pickLatestRelease,
  getReleaseByTag,
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

test('getReleaseByTag returns the release JSON, throws with status', async () => {
  const release = { tag_name: 'v1.0.0', assets: [] }
  const ok = await getReleaseByTag({
    owner: 'o',
    repo: 'r',
    tag: 'v1.0.0',
    fetchFn: async () => ({ ok: true, json: async () => release }),
  })
  assert.equal(ok, release)
  await assert.rejects(
    getReleaseByTag({
      owner: 'o',
      repo: 'r',
      tag: 'nope',
      fetchFn: async () => ({ ok: false, status: 404 }),
    }),
    /No release nope \(HTTP 404\)/,
  )
})

test('installRelease downloads manifest+images via the API asset URLs', async () => {
  const seen = []
  const manifestJson = JSON.stringify(manifest)
  const bytes = (s) => new TextEncoder().encode(s)
  const fetchFn = async (url, init) => {
    seen.push({ url, init })
    if (url.endsWith('/releases/tags/v1.0.0')) {
      return {
        ok: true,
        json: async () => ({
          tag_name: 'v1.0.0',
          assets: [
            { name: 'manifest.json', url: 'https://api.github.com/asset/1' },
            { name: 'fw-factory.bin', url: 'https://api.github.com/asset/2' },
            { name: 'fw-spiffs.bin', url: 'https://api.github.com/asset/3' },
          ],
        }),
      }
    }
    if (url === 'https://api.github.com/asset/1') {
      return { ok: true, arrayBuffer: async () => bytes(manifestJson).buffer }
    }
    return { ok: true, arrayBuffer: async () => bytes(`data:${url}`).buffer }
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
    owner: 'o',
    repo: 'r',
    tag: 'v1.0.0',
    manifestName: 'manifest.json',
    expectedChip: 'ESP32-C6',
    fetchFn,
    requestPort: async () => ({}),
    esptool,
  })
  assert.equal(chip, 'ESP32-C6')
  // every asset fetch asked for octet-stream (the CORS-safe path)
  for (const { url, init } of seen.slice(1)) {
    assert.match(url, /^https:\/\/api\.github\.com\//)
    assert.equal(init.headers.Accept, 'application/octet-stream')
  }
  assert.deepEqual(
    written.map((f) => f.address),
    [0x0, 0x37c000],
  )
})

test('installRelease throws when a manifest file has no release asset', async () => {
  const fetchFn = async (url) => {
    if (url.endsWith('/releases/tags/v1.0.0')) {
      return { ok: true, json: async () => ({ tag_name: 'v1.0.0', assets: [] }) }
    }
    throw new Error(`unexpected fetch ${url}`)
  }
  await assert.rejects(
    installRelease({
      owner: 'o',
      repo: 'r',
      tag: 'v1.0.0',
      manifestName: 'manifest.json',
      fetchFn,
      requestPort: async () => ({}),
    }),
    /no file named manifest\.json/,
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
