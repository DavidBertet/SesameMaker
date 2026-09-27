// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Improv Wi-Fi serial client (matches backend/src/core/improv.h): frame
// codec plus a provisionWifi() session over a serial port. Speaks the same
// packets as ESP Web Tools, so anything provisionable there works here.

const MAGIC = Buffer.from('IMPROV', 'ascii')
const VERSION = 0x01

const TYPE = { STATE: 0x01, ERROR: 0x02, RPC: 0x03, RESULT: 0x04 }
const STATE = { STOPPED: 0x00, AUTHORIZED: 0x02, PROVISIONING: 0x03, PROVISIONED: 0x04 }
const ERROR = {
  NONE: 0x00,
  INVALID_RPC: 0x01,
  UNKNOWN_RPC: 0x02,
  UNABLE_TO_CONNECT: 0x03,
  BAD_HOSTNAME: 0x05,
  UNKNOWN: 0xff,
}
const RPC = {
  WIFI_SETTINGS: 0x01,
  GET_STATE: 0x02,
  GET_INFO: 0x03,
  GET_NETWORKS: 0x04,
  SET_OTA_PASSWORD: 0x10, // SesameMaker extension (see improv.h)
  GET_OTA_PASSWORD: 0x11, // SesameMaker extension (read-back for install.sh)
}

const ERROR_NAMES = {
  [ERROR.INVALID_RPC]: 'malformed packet',
  [ERROR.UNKNOWN_RPC]: 'unknown command',
  [ERROR.UNABLE_TO_CONNECT]: 'unable to connect (check SSID/password)',
  [ERROR.BAD_HOSTNAME]: 'bad hostname',
  [ERROR.UNKNOWN]: 'unknown error',
}

function checksum(bytes) {
  let sum = 0
  for (const b of bytes) sum = (sum + b) & 0xff
  return sum
}

// One frame: magic + version + type + len + data + checksum + '\n'.
function buildFrame(type, data) {
  const body = Buffer.from(data || [])
  if (body.length > 255) throw new Error('improv frame data too long')
  const head = Buffer.concat([MAGIC, Buffer.from([VERSION, type, body.length]), body])
  return Buffer.concat([head, Buffer.from([checksum(head), 0x0a])])
}

// Pull complete, checksum-valid frames out of a byte buffer (log noise and
// partial frames stay in `rest` for the next chunk). Returns {frames, rest}.
function parseFrames(buf) {
  const frames = []
  let rest = Buffer.from(buf)
  for (;;) {
    const at = rest.indexOf(MAGIC)
    if (at === -1) {
      rest = rest.length > MAGIC.length ? rest.slice(-MAGIC.length) : rest
      break
    }
    rest = rest.slice(at)
    if (rest.length < 9) break // header incomplete
    if (rest[6] !== VERSION) {
      rest = rest.slice(1)
      continue
    }
    const len = rest[8]
    if (rest.length < 9 + len + 1) break // wait for data + checksum
    const body = rest.slice(0, 9 + len)
    if (checksum(body) !== rest[9 + len]) {
      rest = rest.slice(1)
      continue
    }
    frames.push({ type: rest[7], data: rest.slice(9, 9 + len) })
    rest = rest.slice(9 + len + 1)
    // A trailing newline follows the checksum; drop one if present.
    if (rest.length > 0 && rest[0] === 0x0a) rest = rest.slice(1)
  }
  return { frames, rest }
}

function rpcCommand(cmd, payload) {
  const body = Buffer.from(payload || [])
  // RPC frame data consists of [Command Byte] + [Payload Length] + [Payload Data]
  const rpcData = Buffer.concat([Buffer.from([cmd, body.length]), body])
  return buildFrame(TYPE.RPC, rpcData)
}

function wifiSettingsPayload(ssid, password) {
  const s = Buffer.from(ssid || '', 'utf8')
  const p = Buffer.from(password || '', 'utf8')
  if (s.length === 0 || s.length > 32) throw new Error('SSID must be 1..32 bytes')
  if (p.length > 64) throw new Error('WiFi password must be at most 64 bytes')
  return Buffer.concat([Buffer.from([s.length]), s, Buffer.from([p.length]), p])
}

function otaPasswordPayload(password) {
  const p = Buffer.from(password || '', 'utf8')
  if (p.length > 64) throw new Error('OTA password must be at most 64 bytes')
  return Buffer.concat([Buffer.from([p.length]), p])
}

function waitFor(predicate, timeoutMs, what) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      clearInterval(poll)
      reject(new Error(`timed out waiting for ${what}`))
    }, timeoutMs)
    const poll = setInterval(() => {
      let hit
      try {
        hit = predicate()
      } catch (e) {
        clearTimeout(timer)
        clearInterval(poll)
        reject(e)
        return
      }
      if (hit) {
        clearTimeout(timer)
        clearInterval(poll)
        resolve(hit)
      }
    }, 50)
  })
}

// Provision WiFi (+ optional OTA password) over a serial port speaking
// Improv. serialLib is injectable for tests ({SerialPort} like `serialport`).
// Resolves { ssid, url } on success, throws Error (device error or timeout).
async function provisionWifi(
  portPath,
  { ssid, password, otaPassword, stateTimeoutMs = 5000, connectTimeoutMs = 45000 } = {},
  serialLib = null,
) {
  const { SerialPort } = serialLib || require('serialport')
  // hupcl:false — dropping DTR on close must not reboot the device behind us.
  const port = new SerialPort({
    path: portPath,
    baudRate: 115200,
    autoOpen: false,
    hupcl: false,
    dtr: false, // Prevents toggling CHIP_PU / EN pin on open
    rts: false, // Prevents toggling GPIO0 on open
  })
  let buf = Buffer.alloc(0)
  const seen = []
  const onData = (chunk) => {
    buf = Buffer.concat([buf, Buffer.from(chunk)])
    const { frames, rest } = parseFrames(buf)
    buf = rest
    seen.push(...frames)
  }
  const take = (pred) => {
    const i = seen.findIndex(pred)
    if (i === -1) return null
    return seen.splice(i, 1)[0]
  }

  await new Promise((resolve, reject) => port.open((err) => (err ? reject(err) : resolve())))
  try {
    port.on('data', onData)

    // Brief delay to let CDC USB settle
    await new Promise((resolve) => setTimeout(resolve, 500))

    const write = (data) =>
      new Promise((resolve, reject) => port.write(data, (err) => (err ? reject(err) : resolve())))

    // Say hello until the device answers (it shares the line with boot logs).
    let state = null
    for (let attempt = 0; attempt < 3 && !state; attempt++) {
      await write(rpcCommand(RPC.GET_STATE, []))
      state = await waitFor(
        () => take((f) => f.type === TYPE.STATE),
        stateTimeoutMs,
        'device state',
      ).catch(() => null)
    }
    if (!state) throw new Error('device did not answer Improv requests (is the firmware running?)')

    await write(rpcCommand(RPC.WIFI_SETTINGS, wifiSettingsPayload(ssid, password)))
    // Wait for the terminal outcome only. Intermediate PROVISIONING states
    // just mean "still trying" — resolving on them returns before the
    // device is connected and before the URL result arrives.
    const done = await waitFor(
      () =>
        take((f) => f.type === TYPE.ERROR) ||
        take((f) => f.type === TYPE.STATE && f.data[0] === STATE.PROVISIONED),
      connectTimeoutMs,
      'provisioning result',
    )
    if (done.type === TYPE.ERROR) {
      const code = done.data[0]
      throw new Error(`device refused: ${ERROR_NAMES[code] || `code ${code}`}`)
    }

    // The URL result trails the PROVISIONED state - but it can lose a
    // checksum race against log output on the shared line, so re-request
    // it (GET_STATE re-triggers the settings response while provisioned)
    // instead of accepting a single miss.
    let url = null
    for (let attempt = 0; attempt < 3 && !url; attempt++) {
      if (attempt > 0) {
        await write(rpcCommand(RPC.GET_STATE, []))
      }
      const result = await waitFor(
        () => take((f) => f.type === TYPE.RESULT),
        4000,
        'device URL',
      ).catch(() => null)
      if (result && result.data.length > 2) {
        const pos = 2
        const slen = result.data[pos]
        url = result.data.slice(pos + 1, pos + 1 + slen).toString('utf8') || null
      }
    }

    if (otaPassword !== undefined) {
      await write(rpcCommand(RPC.SET_OTA_PASSWORD, otaPasswordPayload(otaPassword)))
      const otaDone = await waitFor(
        () => take((f) => f.type === TYPE.RESULT) || take((f) => f.type === TYPE.ERROR),
        stateTimeoutMs,
        'OTA password result',
      )
      if (otaDone.type === TYPE.ERROR) {
        throw new Error(`OTA password refused: ${ERROR_NAMES[otaDone.data[0]] || 'unknown'}`)
      }
    }

    return { ssid, url }
  } finally {
    port.removeListener('data', onData)
    await new Promise((resolve) => port.close(() => resolve()))
  }
}

// After flashing + reset the USB device often re-enumerates under a new
// /dev node (seen: usbmodem2101 -> usbmodem1101). If the preferred path is
// gone, follow a single unambiguous USB-serial candidate; otherwise keep
// the preferred path and let open fail loudly.
async function resolvePortPath(preferredPath, serialLib = null) {
  const { SerialPort } = serialLib || require('serialport')
  let ports = []
  try {
    ports = await SerialPort.list()
  } catch {
    return preferredPath
  }
  const paths = ports.map((p) => p.path)
  if (paths.includes(preferredPath)) {
    return preferredPath
  }
  const cands = paths.filter((p) => /usbmodem|usbserial|slab|ttyusb|ttyacm|^com\d+$/i.test(p))
  if (cands.length === 1) {
    return cands[0]
  }
  return preferredPath
}

// One request/response exchange: open, send one RPC, await its RESULT
// (or ERROR), close. Returns the RESULT frame. Tolerant of log noise.
async function exchangeRpc(portPath, cmd, payload, { timeoutMs = 5000 } = {}, serialLib = null) {
  const { SerialPort } = serialLib || require('serialport')
  const port = new SerialPort({ path: portPath, baudRate: 115200, autoOpen: false, hupcl: false })
  let buf = Buffer.alloc(0)
  const seen = []
  const onData = (chunk) => {
    buf = Buffer.concat([buf, Buffer.from(chunk)])
    const { frames, rest } = parseFrames(buf)
    buf = rest
    seen.push(...frames)
  }
  await new Promise((resolve, reject) => port.open((err) => (err ? reject(err) : resolve())))
  try {
    port.on('data', onData)

    // Brief delay to let CDC USB settle
    await new Promise((resolve) => setTimeout(resolve, 500))

    await new Promise((resolve, reject) =>
      port.write(rpcCommand(cmd, payload), (err) => (err ? reject(err) : resolve())),
    )
    return await waitFor(
      () => {
        const i = seen.findIndex((f) => f.type === TYPE.RESULT || f.type === TYPE.ERROR)
        return i === -1 ? null : seen.splice(i, 1)[0]
      },
      timeoutMs,
      'RPC result',
    )
  } finally {
    port.removeListener('data', onData)
    await new Promise((resolve) => port.close(() => resolve()))
  }
}

function expectResult(frame, what) {
  if (frame.type === TYPE.ERROR) {
    const code = frame.data[0]
    throw new Error(`${what} refused: ${ERROR_NAMES[code] || `code ${code}`}`)
  }
  return frame
}

// First string of a RESULT payload ([cmd][len][slen str]...), or ''.
function firstResultString(frame) {
  if (frame.data.length < 3) return ''
  const slen = frame.data[2]
  return frame.data.slice(3, 3 + slen).toString('utf8')
}

// Read the device's OTA password ('' = open). Null when the device doesn't
// answer (old firmware without Improv).
async function getOtaPassword(portPath, opts = {}, serialLib = null) {
  try {
    const frame = await exchangeRpc(portPath, RPC.GET_OTA_PASSWORD, [], opts, serialLib)
    return firstResultString(expectResult(frame, 'read OTA password'))
  } catch {
    return null
  }
}

// Set (or clear, with '') the device's OTA password. Throws on refusal.
async function setOtaPassword(portPath, password, opts = {}, serialLib = null) {
  const frame = await exchangeRpc(
    portPath,
    RPC.SET_OTA_PASSWORD,
    otaPasswordPayload(password),
    opts,
    serialLib,
  )
  expectResult(frame, 'set OTA password')
}

module.exports = {
  TYPE,
  STATE,
  ERROR,
  ERROR_NAMES,
  RPC,
  checksum,
  buildFrame,
  parseFrames,
  rpcCommand,
  wifiSettingsPayload,
  otaPasswordPayload,
  provisionWifi,
  resolvePortPath,
  exchangeRpc,
  firstResultString,
  getOtaPassword,
  setOtaPassword,
}
