// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Improv Wi-Fi serial client for browsers (WebSerial). Same packets as
// cli/lib/improv.js and backend/src/core/improv.h, reimplemented on
// Uint8Array (no Node Buffer). The port is injected, so everything below
// the transport is unit tested with fakes; only requestDevicePort touches
// navigator.serial (HTTPS/localhost + Chromium).

const MAGIC = [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56] // 'IMPROV'
const VERSION = 0x01

export const TYPE = { STATE: 0x01, ERROR: 0x02, RPC: 0x03, RESULT: 0x04 }
export const STATE = { STOPPED: 0x00, AUTHORIZED: 0x02, PROVISIONING: 0x03, PROVISIONED: 0x04 }
export const RPC = {
  WIFI_SETTINGS: 0x01,
  GET_STATE: 0x02,
  GET_INFO: 0x03,
  GET_NETWORKS: 0x04,
  GET_NETSTATE: 0x07,
  SET_OTA_PASSWORD: 0x10,
  GET_OTA_PASSWORD: 0x11,
  NETWORK_RESET: 0x12,
}

const ERROR_NAMES = {
  0x01: 'malformed packet',
  0x02: 'unknown command',
  0x03: 'unable to connect (check SSID/password)',
  0x05: 'bad hostname',
  0xff: 'unknown error',
}

const enc = new TextEncoder()
const dec = new TextDecoder()

function checksum(bytes) {
  let sum = 0
  for (const b of bytes) sum = (sum + b) & 0xff
  return sum
}

function concat(...parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0))
  let off = 0
  for (const p of parts) {
    out.set(p, off)
    off += p.length
  }
  return out
}

// One frame: magic + version + type + len + data + checksum + '\n'.
export function buildFrame(type, data) {
  const body = data || new Uint8Array(0)
  if (body.length > 255) throw new Error('improv frame data too long')
  const head = concat(Uint8Array.from(MAGIC), Uint8Array.of(VERSION, type, body.length), body)
  return concat(head, Uint8Array.of(checksum(head), 0x0a))
}

// Pull complete, checksum-valid frames out of a byte buffer (log noise and
// partial frames stay in `rest`). Returns {frames, rest, noise}: `noise`
// holds the bytes definitively skipped as non-frame data (console output on
// the shared USB line) so callers can surface them as log lines. Bytes kept
// in `rest` (partial magic/frame tails) are never reported as noise.
export function parseFrames(buf) {
  const bytes = buf instanceof Uint8Array ? buf : Uint8Array.from(buf)
  const frames = []
  const dropped = []
  let rest = bytes
  const drop = (n) => {
    if (n > 0) {
      dropped.push(rest.slice(0, n))
      rest = rest.slice(n)
    }
  }
  const indexOfMagic = () => {
    outer: for (let i = 0; i + MAGIC.length <= rest.length; i++) {
      for (let j = 0; j < MAGIC.length; j++) {
        if (rest[i + j] !== MAGIC[j]) continue outer
      }
      return i
    }
    return -1
  }
  for (;;) {
    const at = indexOfMagic()
    if (at === -1) {
      if (rest.length > MAGIC.length) drop(rest.length - MAGIC.length)
      break
    }
    drop(at)
    if (rest.length < 9) break
    if (rest[6] !== VERSION) {
      drop(1)
      continue
    }
    const len = rest[8]
    if (rest.length < 9 + len + 1) break
    const body = rest.slice(0, 9 + len)
    if (checksum(body) !== rest[9 + len]) {
      drop(1)
      continue
    }
    frames.push({ type: rest[7], data: rest.slice(9, 9 + len) })
    rest = rest.slice(9 + len + 1)
    if (rest.length > 0 && rest[0] === 0x0a) rest = rest.slice(1)
  }
  return { frames, rest, noise: concat(...dropped) }
}

export function rpcCommand(cmd, payload) {
  const body = payload || new Uint8Array(0)
  return buildFrame(TYPE.RPC, concat(Uint8Array.of(cmd, body.length), body))
}

function bytesOrThrow(str, min, max, what) {
  const b = enc.encode(str || '')
  if (b.length < min || b.length > max) throw new Error(`${what} must be ${min}..${max} bytes`)
  return b
}

export function wifiSettingsPayload(ssid, password) {
  const s = bytesOrThrow(ssid, 1, 32, 'SSID')
  const p = bytesOrThrow(password, 0, 64, 'WiFi password')
  return concat(Uint8Array.of(s.length), s, Uint8Array.of(p.length), p)
}

export function otaPasswordPayload(password) {
  const p = bytesOrThrow(password, 0, 64, 'OTA password')
  return concat(Uint8Array.of(p.length), p)
}

function resultStrings(frame) {
  const strs = []
  let pos = 2
  while (pos < frame.data.length) {
    const slen = frame.data[pos++]
    strs.push(dec.decode(frame.data.slice(pos, pos + slen)))
    pos += slen
  }
  return strs
}

function throwIfError(frame, what) {
  if (frame.type === TYPE.ERROR) {
    const code = frame.data[0]
    throw new Error(`${what} refused: ${ERROR_NAMES[code] || `code ${code}`}`)
  }
  return frame
}

// Open port picker (user gesture required). Throws when WebSerial is
// unavailable (needs HTTPS or localhost, Chromium-based browser).
export async function requestDevicePort() {
  if (!('serial' in navigator)) {
    throw new Error('WebSerial is not available (needs HTTPS/localhost on a Chromium browser)')
  }
  const port = await navigator.serial.requestPort()
  await port.open({ baudRate: 115200 })
  return port
}

// One live session over an open WebSerial port. Calls are sequential
// (the tab disables its buttons while busy).
export class ImprovSession {
  constructor(port) {
    this.port = port
    this.buf = new Uint8Array(0)
    this.seen = []
    this.reader = null
    this.pump = null
    this.logListeners = new Set()
    this._logText = ''
    this._logDec = new TextDecoder()
  }

  // Console lines from the shared USB line (ESP logs interleaved with
  // frames). Returns an unsubscribe function.
  onLog(cb) {
    this.logListeners.add(cb)
    return () => this.logListeners.delete(cb)
  }

  _emitNoise(chunk) {
    if (!chunk.length || this.logListeners.size === 0) return
    this._logText += this._logDec.decode(chunk, { stream: true })
    const parts = this._logText.split('\n')
    this._logText = parts.pop()
    for (const line of parts) {
      const msg = line.replace(/\r$/, '')
      if (!msg) continue
      for (const cb of this.logListeners) cb(msg)
    }
  }

  _flushLogs() {
    if (!this._logText || this.logListeners.size === 0) return
    const msg = this._logDec.decode() + this._logText
    this._logText = ''
    if (!msg) return
    for (const cb of this.logListeners) cb(msg)
  }

  async _ensureReader() {
    if (!this.reader) {
      this.reader = this.port.readable.getReader()
      const pump = async () => {
        try {
          for (;;) {
            const { value, done } = await this.reader.read()
            if (done) break
            if (value && value.length) {
              this.buf = concat(this.buf, value)
              const { frames, rest, noise } = parseFrames(this.buf)
              this.buf = rest
              this.seen.push(...frames)
              this._emitNoise(noise)
            }
          }
        } catch {
          // Reader died mid-session (USB glitch, device reset): drop it so
          // the next call re-acquires a fresh one instead of hanging on a
          // dead pump forever. In-flight waits time out normally.
          try {
            this.reader?.releaseLock?.()
          } catch {
            // already gone
          }
          this.reader = null
        }
      }
      this.pump = pump()
    }
  }

  _releaseReader() {
    if (this.reader) {
      this.reader.cancel().catch(() => {})
      this.reader.releaseLock()
      this.reader = null
    }
  }

  async close() {
    this._releaseReader()
    try {
      await this.pump
    } catch {
      // pump already gone
    }
    this._flushLogs()
    await this.port.close()
  }

  async _write(data) {
    const writer = this.port.writable.getWriter()
    try {
      await writer.write(data)
    } finally {
      writer.releaseLock()
    }
  }

  _take(pred) {
    const i = this.seen.findIndex(pred)
    if (i === -1) return null
    return this.seen.splice(i, 1)[0]
  }

  // Drop queued frames of the given types. Calls are sequential, so
  // anything queued before a new request is stale (e.g. scan rows from
  // an aborted scan replayed from OS buffers on reconnect) and must not
  // satisfy the next wait. Partial bytes in `buf` are left alone.
  _discard(types) {
    this.seen = this.seen.filter((f) => !types.includes(f.type))
  }

  static _isResultFor(frame, ...cmds) {
    return frame.type === TYPE.RESULT && frame.data.length >= 2 && cmds.includes(frame.data[0])
  }

  async _waitFor(pred, timeoutMs, what) {
    const deadline = Date.now() + timeoutMs
    for (;;) {
      const hit = this._take(pred)
      if (hit) return hit
      if (Date.now() >= deadline) throw new Error(`timed out waiting for ${what}`)
      await new Promise((r) => setTimeout(r, 50))
    }
  }

  // One request/response exchange: RESULT (matching cmd) or ERROR
  // (throws on error). Stale frames queued before the request are dropped.
  async exchange(cmd, payload, timeoutMs = 5000) {
    await this._ensureReader()
    this._discard([TYPE.RESULT, TYPE.ERROR])
    await this._write(rpcCommand(cmd, payload))
    const frame = await this._waitFor(
      (f) => f.type === TYPE.ERROR || ImprovSession._isResultFor(f, cmd),
      timeoutMs,
      'RPC result',
    )
    return throwIfError(frame, 'device')
  }

  async getState(timeoutMs = 5000) {
    await this._ensureReader()
    this._discard([TYPE.STATE, TYPE.RESULT, TYPE.ERROR])
    await this._write(rpcCommand(RPC.GET_STATE, new Uint8Array(0)))
    return this._waitFor((f) => f.type === TYPE.STATE, timeoutMs, 'device state')
  }

  async getInfo() {
    return resultStrings(await this.exchange(RPC.GET_INFO, new Uint8Array(0)))
  }

  async getOtaPassword() {
    try {
      const frame = await this.exchange(RPC.GET_OTA_PASSWORD, new Uint8Array(0))
      const strs = resultStrings(frame)
      return strs.length ? strs[0] : ''
    } catch {
      return null // old firmware without Improv
    }
  }

  async setOtaPassword(password) {
    await this.exchange(RPC.SET_OTA_PASSWORD, otaPasswordPayload(password))
  }

  // Forget WiFi, clear the OTA password, reboot into the setup AP.
  // The device acks before rebooting; the session drops right after.
  async networkReset() {
    await this.exchange(RPC.NETWORK_RESET, new Uint8Array(0))
  }

  async getNetworkState() {
    const frame = await this.exchange(RPC.GET_NETSTATE, new Uint8Array(0))
    const strs = resultStrings(frame)
    return { flags: parseInt(strs[0] || '0', 10), urls: strs.slice(1) }
  }

  async scanNetworks(timeoutMs = 30000) {
    await this._ensureReader()
    this._discard([TYPE.RESULT, TYPE.ERROR])
    await this._write(rpcCommand(RPC.GET_NETWORKS, new Uint8Array(0)))
    const networks = []
    const deadline = Date.now() + timeoutMs
    for (;;) {
      const remaining = deadline - Date.now()
      if (remaining <= 0) throw new Error('timed out waiting for network scan')
      const frame = await this._waitFor(
        (f) => f.type === TYPE.ERROR || ImprovSession._isResultFor(f, RPC.GET_NETWORKS),
        remaining,
        'network scan',
      )
      throwIfError(frame, 'network scan')
      if (frame.data.length <= 2) break // empty trailer: end of list
      let pos = 2
      const strs = []
      while (pos < frame.data.length) {
        const slen = frame.data[pos++]
        strs.push(dec.decode(frame.data.slice(pos, pos + slen)))
        pos += slen
      }
      networks.push({
        ssid: strs[0] || '',
        rssi: parseInt(strs[1] || '-100', 10) || -100,
        auth: strs[2] === 'YES',
      })
    }
    return networks
  }

  // Full provisioning: send credentials, wait for the terminal outcome
  // (intermediate PROVISIONING states are ignored), then the URL result.
  async provisionWifi(ssid, password, connectTimeoutMs = 45000) {
    await this._ensureReader()
    this._discard([TYPE.STATE, TYPE.RESULT, TYPE.ERROR])
    await this._write(rpcCommand(RPC.WIFI_SETTINGS, wifiSettingsPayload(ssid, password)))
    const done = await this._waitFor(
      (f) => f.type === TYPE.ERROR || (f.type === TYPE.STATE && f.data[0] === STATE.PROVISIONED),
      connectTimeoutMs,
      'provisioning result',
    )
    throwIfError(done, 'provisioning')
    let url = null
    for (let attempt = 0; attempt < 3 && !url; attempt++) {
      if (attempt > 0) {
        await this._write(rpcCommand(RPC.GET_STATE, new Uint8Array(0)))
      }
      // URL result trails PROVISIONED with cmd WIFI_SETTINGS (or GET_STATE
      // on re-request). Other RESULTs (e.g. stale scan rows) are ignored.
      const result = await this._waitFor(
        (f) => ImprovSession._isResultFor(f, RPC.WIFI_SETTINGS, RPC.GET_STATE),
        4000,
        'device URL',
      ).catch(() => null)
      if (result && result.data.length > 2) {
        const slen = result.data[2]
        url = dec.decode(result.data.slice(3, 3 + slen)) || null
      }
    }
    return { ssid, url }
  }
}
