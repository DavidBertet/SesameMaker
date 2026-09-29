// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Browser firmware install (core, product-agnostic): list GitHub releases,
// fetch a flash manifest, burn the images over WebSerial via esptool-js.
// Product wiring (owner/repo, manifest names) is passed in by the caller.
// esptool-js is dynamically imported so node unit tests never touch it.

export const GITHUB_API = 'https://api.github.com'

// Releases, newest first, drafts dropped. Throws with the HTTP status.
export async function listFirmwareReleases({ owner, repo, fetchFn = fetch }) {
  const res = await fetchFn(`${GITHUB_API}/repos/${owner}/${repo}/releases?per_page=20`)
  if (!res.ok) throw new Error(`Could not list releases (HTTP ${res.status})`)
  const all = await res.json()
  return all
    .filter((r) => !r.draft)
    .map((r) => ({
      tag: r.tag_name,
      name: r.name || r.tag_name,
      prerelease: !!r.prerelease,
      publishedAt: (r.published_at || '').slice(0, 10),
    }))
}

// First stable, else first entry, else null.
export function pickLatestRelease(releases) {
  if (!releases || releases.length === 0) return null
  return releases.find((r) => !r.prerelease) || releases[0]
}

export function releaseDownloadBase({ owner, repo, tag }) {
  return `https://github.com/${owner}/${repo}/releases/download/${tag}`
}

// Manifest shape from the release workflow:
// {version, chip, files: [{name, offset ("0x..."), size, sha256}]}.
// Throws on anything unexpected — never flash a half-understood layout.
export function parseManifest(raw, { expectedChip = null } = {}) {
  const files = raw && raw.files
  if (!raw || typeof raw !== 'object' || !Array.isArray(files) || files.length === 0) {
    throw new Error('Flash manifest is empty or malformed')
  }
  if (expectedChip && raw.chip && raw.chip !== expectedChip) {
    throw new Error(`Manifest targets ${raw.chip}, expected ${expectedChip}`)
  }
  return files.map((f) => {
    const address = Number.parseInt(f.offset, 16)
    if (!f.name || !Number.isInteger(address) || address < 0) {
      throw new Error(`Bad manifest entry: ${JSON.stringify(f)}`)
    }
    return { name: f.name, address, size: f.size || 0 }
  })
}

async function downloadImage(url, fetchFn) {
  const res = await fetchFn(url)
  if (!res.ok) throw new Error(`Download failed (HTTP ${res.status}): ${url}`)
  return new Uint8Array(await res.arrayBuffer())
}

// Burn files ([{name, address, data}]) to a WebSerial port. Resolves the
// detected chip name. Progress: onProgress({file, written, total}).
// eraseAll wipes the whole flash first (fresh start, settings lost).
export async function flashDevice({
  port,
  files,
  expectedChip = null,
  eraseAll = false,
  baudrate = 921600,
  onProgress = () => {},
  esptool = null,
} = {}) {
  const { Transport, ESPLoader } = esptool || (await import('esptool-js'))
  const transport = new Transport(port, true)
  const quiet = { clean() {}, write() {}, writeLine() {} }
  const loader = new ESPLoader({ transport, baudrate, terminal: quiet })
  try {
    const chip = await loader.main()
    if (expectedChip && !chip.toUpperCase().includes(expectedChip.toUpperCase())) {
      throw new Error(`Detected ${chip}, but this firmware needs ${expectedChip}`)
    }
    if (eraseAll) {
      await loader.eraseFlash()
    }
    await loader.writeFlash({
      fileArray: files.map((f) => ({ data: f.data, address: f.address })),
      flashMode: 'dio',
      flashFreq: '80m',
      flashSize: 'keep',
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex, written, total) =>
        onProgress({ file: files[fileIndex].name, written, total }),
    })
    try {
      await loader.hardReset()
    } catch {
      // already rebooting into the new firmware
    }
    return chip
  } finally {
    await transport.disconnect()
  }
}

// Full install for one release tag: manifest + images, then burn.
// onProgress gets {phase: 'download'|'flash', ...}.
export async function installRelease({
  owner,
  repo,
  tag,
  manifestName,
  expectedChip = null,
  eraseAll = false,
  fetchFn = fetch,
  requestPort = () => navigator.serial.requestPort(),
  onProgress = () => {},
  esptool = null,
} = {}) {
  const base = releaseDownloadBase({ owner, repo, tag })
  const manifestRes = await fetchFn(`${base}/${manifestName}`)
  if (!manifestRes.ok) {
    throw new Error(`No install files for ${tag} (HTTP ${manifestRes.status})`)
  }
  const entries = parseManifest(await manifestRes.json(), { expectedChip })
  const port = await requestPort()
  const files = []
  for (const e of entries) {
    onProgress({ phase: 'download', file: e.name })
    files.push({ ...e, data: await downloadImage(`${base}/${e.name}`, fetchFn) })
  }
  const chip = await flashDevice({
    port,
    files,
    expectedChip,
    eraseAll,
    onProgress: ({ file, written, total }) => onProgress({ phase: 'flash', file, written, total }),
    esptool,
  })
  return { chip, port }
}
