// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { strict as assert } from 'node:assert'
import test from 'node:test'
import {
  TYPE,
  STATE,
  RPC,
  buildFrame,
  parseFrames,
  rpcCommand,
  wifiSettingsPayload,
  ImprovSession,
} from './serialImprov.js'

// In-memory WebSerial port: script answers frames, chunks dribble out.
function fakePort(script) {
  const queue = []
  const waiters = []
  let cancelled = false
  const emit = (chunk) => {
    if (waiters.length) waiters.shift()(chunk)
    else queue.push(chunk)
  }
  return {
    readable: {
      getReader: () => ({
        read: () =>
          new Promise((resolve) => {
            if (cancelled) resolve({ value: undefined, done: true })
            else if (queue.length) resolve({ value: queue.shift(), done: false })
            else waiters.push((chunk) => resolve({ value: chunk, done: false }))
          }),
        releaseLock() {},
        cancel() {
          cancelled = true
          let w
          while ((w = waiters.pop())) w(undefined)
          return Promise.resolve()
        },
      }),
    },
    writable: {
      getWriter: () => ({
        write: async (chunk) => script(chunk, emit),
        releaseLock() {},
      }),
    },
    close: async () => {},
  }
}

function frameData(type, data) {
  return buildFrame(type, Uint8Array.from(data))
}

function resultFrame(cmd, strs) {
  const parts = strs.flatMap((s) => {
    const b = new TextEncoder().encode(s)
    return [b.length, ...b]
  })
  return buildFrame(TYPE.RESULT, Uint8Array.from([cmd, parts.length, ...parts]))
}

test('buildFrame wraps the known RequestState vector', () => {
  assert.deepEqual(
    [...buildFrame(TYPE.RPC, Uint8Array.of(RPC.GET_STATE, 0x00))],
    [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56, 0x01, 0x03, 0x02, 0x02, 0x00, 0xe5, 0x0a],
  )
})

test('parseFrames skips log noise and rejects bad checksums', () => {
  const good = buildFrame(TYPE.STATE, Uint8Array.of(0x02))
  const bad = Uint8Array.from(good)
  bad[bad.length - 2] ^= 0xff
  const { frames, rest } = parseFrames(
    Uint8Array.from([...new TextEncoder().encode('boot ok\n'), ...bad, ...good]),
  )
  assert.equal(frames.length, 1)
  assert.deepEqual([...frames[0].data], [0x02])
  assert.equal(rest.length, 0)
})

test('exchange round-trips device info through a fake port', async () => {
  const port = fakePort((chunk, emit) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type === TYPE.RPC && f.data[0] === RPC.GET_INFO) {
        setImmediate(() =>
          emit(
            (() => {
              const strs = ['SesameMaker', 'dev', 'esp32-c6', 'SesameMaker'].map((s) =>
                new TextEncoder().encode(s),
              )
              const body = strs.flatMap((b) => [b.length, ...b])
              return frameData(TYPE.RESULT, [RPC.GET_INFO, body.length, ...body])
            })(),
          ),
        )
      }
    }
  })
  const session = new ImprovSession(port)
  try {
    assert.deepEqual(await session.getInfo(), ['SesameMaker', 'dev', 'esp32-c6', 'SesameMaker'])
  } finally {
    await session.close()
  }
})

test('provisionWifi waits for the terminal outcome, then the URL', async () => {
  const port = fakePort((chunk, emit) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type !== TYPE.RPC) continue
      if (f.data[0] === RPC.WIFI_SETTINGS) {
        setImmediate(() => emit(frameData(TYPE.STATE, [STATE.PROVISIONING])))
        setImmediate(() => emit(frameData(TYPE.STATE, [STATE.PROVISIONED])))
        setImmediate(() => emit(resultFrame(RPC.WIFI_SETTINGS, ['http://192.168.1.10/'])))
      }
    }
  })
  const session = new ImprovSession(port)
  try {
    const res = await session.provisionWifi('home', 'secret', 2000)
    assert.deepEqual(res, { ssid: 'home', url: 'http://192.168.1.10/' })
  } finally {
    await session.close()
  }
})

test('session recovers when the reader dies mid-stream', async () => {
  let dead = false
  const port = fakePort(() => {})
  const origGetReader = port.readable.getReader
  let readers = 0
  port.readable.getReader = () => {
    readers++
    const r = origGetReader()
    if (!dead) {
      dead = true
      // First reader dies immediately: subsequent calls must re-acquire.
      return {
        read: async () => {
          throw new Error('USB glitch')
        },
        releaseLock() {},
        cancel: async () => {},
      }
    }
    return r
  }
  const session = new ImprovSession(port)
  try {
    await session._ensureReader()
    await new Promise((r) => setTimeout(r, 100))
    assert.equal(readers, 1)
    // Pump died silently; next call must build a fresh reader, not hang.
    await session._ensureReader()
    assert.equal(readers, 2)
  } finally {
    await session.close()
  }
})

test('networkReset sends 0x12 and resolves on the empty ack', async () => {
  assert.equal(RPC.NETWORK_RESET, 0x12)
  const seen = []
  const port = fakePort((chunk, emit) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type === TYPE.RPC && f.data[0] === RPC.NETWORK_RESET) {
        seen.push(f.data[0])
        setImmediate(() => emit(buildFrame(TYPE.RESULT, Uint8Array.of(RPC.NETWORK_RESET, 0x00))))
      }
    }
  })
  const session = new ImprovSession(port)
  try {
    await session.networkReset()
    assert.deepEqual(seen, [RPC.NETWORK_RESET])
  } finally {
    await session.close()
  }
})

test('factoryReset sends 0x13 and resolves on the empty ack', async () => {
  assert.equal(RPC.FACTORY_RESET, 0x13)
  const seen = []
  const port = fakePort((chunk, emit) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type === TYPE.RPC && f.data[0] === RPC.FACTORY_RESET) {
        seen.push(f.data[0])
        setImmediate(() => emit(buildFrame(TYPE.RESULT, Uint8Array.of(RPC.FACTORY_RESET, 0x00))))
      }
    }
  })
  const session = new ImprovSession(port)
  try {
    await session.factoryReset()
    assert.deepEqual(seen, [RPC.FACTORY_RESET])
  } finally {
    await session.close()
  }
})

test('getOtaPassword resolves null on silence (old firmware)', async () => {
  const port = fakePort(() => {})
  const session = new ImprovSession(port)
  // stub exchange to fail fast: no script answers, short timeout path
  session.exchange = async () => {
    throw new Error('timed out')
  }
  try {
    assert.equal(await session.getOtaPassword(), null)
  } finally {
    await session.close()
  }
})

test('getInfo skips stale scan rows replayed before the answer', async () => {
  const port = fakePort((chunk, emit) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type === TYPE.RPC && f.data[0] === RPC.GET_INFO) {
        // Stale GET_NETWORKS row from an aborted scan (OS buffer replay),
        // then the real GET_INFO answer.
        setImmediate(() => emit(resultFrame(RPC.GET_NETWORKS, ['TELUS0761', '-49', 'YES'])))
        setImmediate(() =>
          emit(resultFrame(RPC.GET_INFO, ['SesameMaker', 'dev', 'esp32c6', 'SesameMaker'])),
        )
      }
    }
  })
  const session = new ImprovSession(port)
  try {
    assert.deepEqual(await session.getInfo(), ['SesameMaker', 'dev', 'esp32c6', 'SesameMaker'])
  } finally {
    await session.close()
  }
})

test('parseFrames surfaces leading log noise separately', () => {
  const log = new TextEncoder().encode('I (12) WIFI: sta connected\n')
  const frame = buildFrame(TYPE.STATE, Uint8Array.of(STATE.PROVISIONED))
  const { frames, rest, noise } = parseFrames(Uint8Array.from([...log, ...frame]))
  assert.equal(frames.length, 1)
  assert.deepEqual([...noise], [...log])
  assert.equal(rest.length, 0)
})

test('session emits USB log lines interleaved with frames', async () => {
  const lines = []
  const port = fakePort((chunk, emit) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type === TYPE.RPC && f.data[0] === RPC.GET_INFO) {
        const log = new TextEncoder().encode('I (12) WIFI: sta connected\n')
        setImmediate(() =>
          emit(Uint8Array.from([...log, ...resultFrame(RPC.GET_INFO, ['SesameMaker'])])),
        )
      }
    }
  })
  const session = new ImprovSession(port)
  const unsub = session.onLog((msg) => lines.push(msg))
  try {
    assert.deepEqual(await session.getInfo(), ['SesameMaker'])
    assert.deepEqual(lines, ['I (12) WIFI: sta connected'])
  } finally {
    unsub()
    await session.close()
  }
})

test('scanNetworks skips stale foreign results', async () => {
  const port = fakePort((chunk, emit) => {
    const { frames } = parseFrames(chunk)
    for (const f of frames) {
      if (f.type === TYPE.RPC && f.data[0] === RPC.GET_NETWORKS) {
        setImmediate(() => emit(resultFrame(RPC.GET_INFO, ['SesameMaker', 'dev', 'x', 'y'])))
        setImmediate(() => emit(resultFrame(RPC.GET_NETWORKS, ['home', '-60', 'YES'])))
        setImmediate(() => emit(resultFrame(RPC.GET_NETWORKS, [])))
      }
    }
  })
  const session = new ImprovSession(port)
  try {
    assert.deepEqual(await session.scanNetworks(2000), [{ ssid: 'home', rssi: -60, auth: true }])
  } finally {
    await session.close()
  }
})
