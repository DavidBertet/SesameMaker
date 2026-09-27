// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import { createRequire } from 'node:module'
import { EventEmitter } from 'node:events'

const require = createRequire(import.meta.url)

function loadImprov() {
  delete require.cache[require.resolve('./improv.js')]
  return require('./improv.js')
}

test('buildFrame wraps the known RequestState vector', () => {
  const { buildFrame, TYPE, RPC } = loadImprov()
  const frame = buildFrame(TYPE.RPC, [RPC.GET_STATE, 0x00])
  assert.deepEqual(
    [...frame],
    [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56, 0x01, 0x03, 0x02, 0x02, 0x00, 0xe5, 0x0a],
  )
})

test('parseFrames skips log noise and rejects bad checksums', () => {
  const { buildFrame, parseFrames, TYPE } = loadImprov()
  const good = buildFrame(TYPE.STATE, [0x02])
  const bad = Buffer.from(good)
  bad[bad.length - 2] ^= 0xff
  const { frames, rest } = parseFrames(
    Buffer.concat([Buffer.from('boot ok\n'), bad, Buffer.from('IMPR'), good]),
  )
  assert.equal(frames.length, 1)
  assert.equal(frames[0].type, TYPE.STATE)
  assert.deepEqual([...frames[0].data], [0x02])
  assert.equal(rest.length, 0)
})

test('wifiSettingsPayload bounds SSID and password', () => {
  const { wifiSettingsPayload } = loadImprov()
  assert.deepEqual([...wifiSettingsPayload('ab', 'xy')], [0x02, 0x61, 0x62, 0x02, 0x78, 0x79])
  assert.throws(() => wifiSettingsPayload('', 'x'), /SSID/)
  assert.throws(() => wifiSettingsPayload('x'.repeat(33), ''), /SSID/)
})

// Minimal in-memory device: answers state queries, provisions, acks OTA.
function fakeSerialLib(script) {
  class FakePort extends EventEmitter {
    constructor() {
      super()
      this.written = []
    }
    open(cb) {
      setImmediate(cb)
    }
    write(data, cb) {
      this.written.push(Buffer.from(data))
      setImmediate(cb)
      script(Buffer.from(data), (reply) => setImmediate(() => this.emit('data', reply)))
    }
    close(cb) {
      setImmediate(cb)
    }
    removeListener(name, fn) {
      return super.removeListener(name, fn)
    }
  }
  return { SerialPort: FakePort }
}

function deviceScript(impl, respond) {
  const { parseFrames, buildFrame, TYPE, RPC, STATE } = impl
  let buf = Buffer.alloc(0)
  return (chunk, reply) => {
    buf = Buffer.concat([buf, chunk])
    const { frames, rest } = parseFrames(buf)
    buf = rest
    for (const f of frames) {
      if (f.type !== TYPE.RPC) continue
      const [cmd, len, ...payload] = f.data
      if (cmd === RPC.GET_STATE) {
        reply(buildFrame(TYPE.STATE, [STATE.AUTHORIZED]))
      } else if (cmd === RPC.WIFI_SETTINGS) {
        reply(buildFrame(TYPE.STATE, [STATE.PROVISIONING]))
        reply(buildFrame(TYPE.STATE, [STATE.PROVISIONED]))
        const url = Buffer.from('http://192.168.1.10/', 'utf8')
        const body = Buffer.concat([Buffer.from([cmd, url.length + 1, url.length]), url])
        reply(buildFrame(TYPE.RESULT, body))
      } else if (cmd === RPC.SET_OTA_PASSWORD) {
        reply(buildFrame(TYPE.RESULT, [cmd, 0]))
      }
    }
  }
}

test('provisionWifi completes wifi + OTA against a fake device', async () => {
  const impl = loadImprov()
  const lib = fakeSerialLib(deviceScript(impl, null))
  const res = await impl.provisionWifi(
    '/dev/ttyUSB0',
    { ssid: 'home', password: 'secret', otaPassword: 'ota-secret' },
    lib,
  )
  assert.equal(res.ssid, 'home')
  assert.equal(res.url, 'http://192.168.1.10/')
})

test('provisionWifi throws the device error text', async () => {
  const impl = loadImprov()
  const { buildFrame, parseFrames, TYPE, RPC, STATE } = impl
  const lib = fakeSerialLib((chunk, reply) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type !== TYPE.RPC) continue
      if (f.data[0] === RPC.GET_STATE) reply(buildFrame(TYPE.STATE, [STATE.AUTHORIZED]))
      else reply(buildFrame(TYPE.ERROR, [0x03]))
    }
  })
  await assert.rejects(
    impl.provisionWifi('/dev/ttyUSB0', { ssid: 'home', password: 'x' }, lib),
    /unable to connect/,
  )
})

test('resolvePortPath prefers the known path when present', async () => {
  const { resolvePortPath } = loadImprov()
  const lib = {
    SerialPort: {
      list: async () => [
        { path: '/dev/cu.usbmodem1101' },
        { path: '/dev/cu.Bluetooth-Incoming-Port' },
      ],
    },
  }
  assert.equal(await resolvePortPath('/dev/cu.usbmodem1101', lib), '/dev/cu.usbmodem1101')
})

test('resolvePortPath follows a single USB candidate when known path is gone', async () => {
  const { resolvePortPath } = loadImprov()
  const lib = {
    SerialPort: {
      list: async () => [
        { path: '/dev/cu.usbmodem1101' },
        { path: '/dev/cu.Bluetooth-Incoming-Port' },
      ],
    },
  }
  assert.equal(await resolvePortPath('/dev/cu.usbmodem2101', lib), '/dev/cu.usbmodem1101')
})

test('resolvePortPath keeps the known path when ambiguous or list fails', async () => {
  const { resolvePortPath } = loadImprov()
  const two = {
    SerialPort: { list: async () => [{ path: '/dev/ttyUSB0' }, { path: '/dev/ttyUSB1' }] },
  }
  assert.equal(await resolvePortPath('/dev/gone', two), '/dev/gone')
  const failing = {
    SerialPort: {
      list: async () => {
        throw new Error('nope')
      },
    },
  }
  assert.equal(await resolvePortPath('/dev/gone', failing), '/dev/gone')
})
