// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
import { strict as assert } from 'node:assert'
import test from 'node:test'
import { shouldQueueMessage } from './wsqueue.js'

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
