// Unit tests for the SesameMaker z2m external converter (endpoint-aware
// exposes/routing). Stubs the zigbee-herdsman-converters requires so the
// converter logic runs under plain node:test.
const { test } = require('node:test')
const assert = require('node:assert')
const Module = require('node:module')

const calls = { bind: [], onOff: [], commands: [] }

function fakeExpose(name) {
  return {
    _name: name,
    withEndpoint(ep) {
      this._ep = ep
      return this
    },
    withDescription(d) {
      this._desc = d
      return this
    },
  }
}

const stubs = {
  'zigbee-herdsman-converters/lib/exposes': {
    presets: {
      cover: () => fakeExpose('cover'),
      switch: () => fakeExpose('switch'),
    },
  },
  'zigbee-herdsman-converters/lib/reporting': {
    bind: async (ep) => {
      calls.bind.push(ep.ID)
    },
    onOff: async (ep) => {
      calls.onOff.push(ep.ID)
    },
  },
}

const origLoad = Module._load
Module._load = function (request, ...rest) {
  if (request in stubs) return stubs[request]
  return origLoad.call(this, request, ...rest)
}

const [def] = require('./sesamemaker-z2m.js')

const ep = (ID) => ({
  ID,
  command: async (cluster, cmd) => {
    calls.commands.push([ID, cluster, cmd])
  },
  getDevice: () => device,
})

let present = new Set([10, 11, 12])
const device = { getEndpoint: (id) => (present.has(id) ? ep(id) : undefined) }

const msg = (ID, onOff) => ({ endpoint: { ID }, data: { onOff } })

test('endpoint map follows interviewed endpoints', () => {
  present = new Set([10, 11, 12])
  assert.deepEqual(def.endpoint(device), { door: 10, light: 11, lock_remotes: 12 })
  present = new Set([10])
  assert.deepEqual(def.endpoint(device), { door: 10 })
})

test('exposes shrink to interviewed endpoints (dry-contact: cover only)', () => {
  present = new Set([10, 11, 12])
  assert.equal(def.exposes(device).length, 3)
  present = new Set([10])
  const list = def.exposes(device)
  assert.equal(list.length, 1)
  assert.equal(list[0]._name, 'cover')
  // dummy device without getEndpoint (converter validation path) must not throw
  assert.equal(def.exposes({}).length, 1)
})

test('fromZigbee routes by endpoint id', () => {
  const [door, light, lock] = def.fromZigbee
  assert.deepEqual(door.convert(null, msg(10, 1)), { state: 'OPEN' })
  assert.deepEqual(door.convert(null, msg(10, 0)), { state: 'CLOSE' })
  assert.equal(door.convert(null, msg(11, 1)), undefined)
  assert.deepEqual(light.convert(null, msg(11, 1)), { state: 'ON' })
  assert.equal(light.convert(null, msg(12, 1)), undefined)
  assert.deepEqual(lock.convert(null, msg(12, 0)), { state: 'OFF' })
  assert.equal(lock.convert(null, msg(10, 1)), undefined)
  assert.equal(door.convert(null, { endpoint: { ID: 10 }, data: {} }), undefined)
})

test('toZigbee routes by endpoint name, door keeps OPEN/CLOSE/STOP', async () => {
  present = new Set([10, 11, 12])
  const [door, sw] = def.toZigbee
  calls.commands.length = 0
  await door.convertSet({ getDevice: () => device }, 'state', 'OPEN', {})
  assert.deepEqual(calls.commands, [[10, 'genOnOff', 'on']])
  assert.equal(await door.convertSet({ getDevice: () => device }, 'state', 'ON', { endpoint_name: 'light' }), undefined)
  assert.equal(await door.convertSet({ getDevice: () => device }, 'state', 'STOP', {}), undefined)
  await sw.convertSet({ getDevice: () => device }, 'state', 'ON', { endpoint_name: 'lock_remotes' })
  assert.deepEqual(calls.commands.at(-1), [12, 'genOnOff', 'on'])
  assert.equal(await sw.convertSet({ getDevice: () => device }, 'state', 'ON', { endpoint_name: 'door' }), undefined)
  assert.equal(await sw.convertSet({ getDevice: () => device }, 'state', 'ON', {}), undefined)
})

test('configure binds + reports only present endpoints', async () => {
  present = new Set([10, 12])
  calls.bind.length = 0
  calls.onOff.length = 0
  await def.configure[0](device, {})
  assert.deepEqual(calls.bind, [10, 12])
  assert.deepEqual(calls.onOff, [10, 12])
})
