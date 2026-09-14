// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import { shouldQueueMessage, drainQueue } from './wsqueue.js'

test('physical actuation commands are never queued for replay', () => {
  assert.equal(shouldQueueMessage({ type: 'door_command', action: 'toggle' }), false)
  assert.equal(shouldQueueMessage({ type: 'door_command', action: 'open' }), false)
  assert.equal(shouldQueueMessage({ type: 'light_command', action: 'on' }), false)
  assert.equal(shouldQueueMessage({ type: 'lock_command', action: 'lock' }), false)
})

test('wifi state changes are never queued for replay', () => {
  assert.equal(shouldQueueMessage({ type: 'wifi_connect', ssid: 'x' }), false)
  assert.equal(shouldQueueMessage({ type: 'wifi_disconnect' }), false)
})

test('reads and other messages queue by default', () => {
  assert.equal(shouldQueueMessage({ type: 'get_garage_status' }), true)
  assert.equal(shouldQueueMessage({ type: 'get_garage_raw' }), true)
  assert.equal(shouldQueueMessage({ type: 'get_settings' }), true)
  assert.equal(shouldQueueMessage({ type: 'get_mqtt_config' }), true)
  assert.equal(shouldQueueMessage({ type: 'get_system_info' }), true)
  assert.equal(shouldQueueMessage({ type: 'garage_sync' }), true)
  assert.equal(shouldQueueMessage({ type: 'set_mqtt_config' }), true)
  assert.equal(shouldQueueMessage({ type: 'get_zigbee_config' }), true)
  assert.equal(shouldQueueMessage({ type: 'set_zigbee_config' }), true)
  assert.equal(shouldQueueMessage({ type: 'zigbee_pair' }), true)
  assert.equal(shouldQueueMessage({ type: 'zigbee_leave' }), true)
  assert.equal(shouldQueueMessage({ type: 'zigbee_reset' }), true)
  assert.equal(shouldQueueMessage({ type: 'wifi_scan' }), true)
  assert.equal(shouldQueueMessage({ type: 'log_start' }), true)
  assert.equal(shouldQueueMessage({ type: 'ping', timestamp: 1 }), true)
})

test('unknown and malformed messages', () => {
  // Future/unknown message types queue by default (blacklist model).
  assert.equal(shouldQueueMessage({ type: 'some_new_fetch' }), true)
  assert.equal(shouldQueueMessage(null), false)
  assert.equal(shouldQueueMessage(undefined), false)
  assert.equal(shouldQueueMessage({}), false)
  assert.equal(shouldQueueMessage('door_command'), false)
})

test('drainQueue collapses a poller backlog to one read per type', () => {
  const backlog = Array.from({ length: 50 }, () => ({ type: 'get_garage_raw' }))
  const drained = drainQueue(backlog)
  assert.equal(drained.length, 1)
  assert.deepEqual(drained[0], { type: 'get_garage_raw' })
})

test('drainQueue keeps the newest payload of each type in order', () => {
  const drained = drainQueue([
    { type: 'get_settings' },
    { type: 'get_garage_raw' },
    { type: 'set_mqtt_config', mode: 'off' },
    { type: 'get_garage_raw' },
    { type: 'set_mqtt_config', mode: 'ha' },
  ])
  assert.deepEqual(drained, [
    { type: 'get_settings' },
    { type: 'get_garage_raw' },
    { type: 'set_mqtt_config', mode: 'ha' },
  ])
})

test('drainQueue on empty or malformed backlog', () => {
  assert.deepEqual(drainQueue([]), [])
  assert.deepEqual(drainQueue([null, undefined, {}]), [])
})
