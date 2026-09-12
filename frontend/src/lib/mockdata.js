// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { planDoorTransition } from './garage.js'

let isConnected = false
let logIntervals = new Set()

// ---- Garage mock state ----
let garage = {
  protocol: 'secplus1',
  caps: {
    light: true,
    lock: true,
    obstruction: true,
    motion: true,
    panel: true,
    sensors: false,
  },
  door: 'closed',
  light: 'off',
  locked: 'unlocked',
  obstruction: false,
  motion: false,
  panel: 'detected',
  sensors: { open: false, close: false, valid: false },
}

const rxLog = [
  { t: 10120, byte: 0x38 },
  { t: 10121, byte: 0x05 },
  { t: 10360, byte: 0x3a },
  { t: 10361, byte: 0x04 },
  { t: 10600, byte: 0x38 },
  { t: 10601, byte: 0x05 },
  { t: 10840, byte: 0x39 },
  { t: 10841, byte: 0x00 },
]

const txLog = [
  { t: 10000, byte: 0x30 },
  { t: 10500, byte: 0x31 },
  { t: 10720, byte: 0x38 },
]

function garageStatus(extra = {}) {
  const status = {
    type: 'garage_status',
    protocol: garage.protocol,
    caps: { ...garage.caps },
    door: garage.door,
    moving: garage.door === 'opening' || garage.door === 'closing',
    light: garage.light,
    locked: garage.locked,
    obstruction: garage.obstruction,
    motion: garage.motion,
    panel: garage.panel,
    sensors: { ...garage.sensors },
    ...extra,
  }
  // Reeds track the frame's door (transition frames carry a future door),
  // so the badges stay in sync through open/close animations.
  if (status.protocol === 'drycontact') status.sensors = drySensorsFor(status.door)
  return status
}

// Demo reed model: hits follow the effective door state, like the real
// firmware's report_snapshot. Open reed only exists in both-reeds mode;
// the close reed hits only when fully closed; in between both are clear.
function drySensorsFor(door) {
  if (garage.protocol !== 'drycontact') return { ...garage.sensors }
  if (dryCfg.sensor_mode === 0) return { open: false, close: false, valid: false }
  return {
    open: dryCfg.sensor_mode === 2 && door === 'open',
    close: door === 'closed',
    valid: true,
  }
}

function protocolMessage() {
  return {
    type: 'protocol',
    id: garage.protocol,
    supported: garage.protocol !== 'secplus2',
    caps: { ...garage.caps },
    dry: { ...dryCfg },
  }
}

const DRY_CAPS = {
  light: false,
  lock: false,
  obstruction: false,
  motion: false,
  panel: false,
  sensors: true,
}

const SECPLUS1_CAPS = {
  light: true,
  lock: true,
  obstruction: true,
  motion: true,
  panel: true,
  sensors: false,
}

export function cleanupMockLogs() {
  logIntervals.forEach(clearInterval)
  logIntervals.clear()
}

export function generateMockResponse(data) {
  switch (data.type) {
    case 'ping':
      return [{ type: 'pong' }]

    case 'time_update':
      return [
        {
          type: 'time_update_response',
          success: true,
          current_time: Math.floor(Date.now() / 1000),
          formatted_time: new Date().toISOString(),
        },
      ]

    case 'wifi_scan': {
      // Generate 3-6 random networks
      const numNetworks = Math.floor(Math.random() * 4) + 3 // 3 to 6
      const baseNames = ['Mock_Network', 'Test_AP', 'ESP32', 'OfficeNet', 'CafeWiFi', 'IoTNet']
      const networks = Array.from({ length: numNetworks }, (_, i) => ({
        ssid: `${baseNames[i % baseNames.length]}`,
        rssi: -30 - Math.floor(Math.random() * 60), // -30 to -89
        secure: i < 3,
      }))
      return [{ type: 'wifi_list', networks }]
    }

    case 'wifi_status':
      return [
        {
          type: 'wifi_status',
          status: isConnected ? wifiConnected : wifiDisconnected,
        },
      ]

    case 'wifi_connect':
      if (data.password === 'password') {
        isConnected = true
        settings.wifi.setup = true
        settings.wifi.connected = true
        return [
          { type: 'wifi_status', status: wifiConnected },
          { type: 'settings', ...settings },
        ]
      } else {
        return [
          {
            type: 'error',
            message: 'Password incorrect! Can you guess what my "password" is?',
          },
        ]
      }

    case 'wifi_disconnect':
      isConnected = false
      settings.wifi.setup = false
      settings.wifi.connected = false
      return [
        { type: 'wifi_status', status: wifiDisconnected },
        { type: 'settings', ...settings },
      ]

    case 'get_system_info':
      return [{ type: 'system_info', settings: systemInfo }]

    case 'get_settings':
      return [{ type: 'settings', ...settings }]

    case 'get_garage_status':
      return [garageStatus()]

    case 'door_command': {
      const frames = planDoorTransition(garage.door, data.action || 'toggle')
      if (frames.length === 0) return [garageStatus()]
      const responses = frames.map((frame) => ({
        ...garageStatus({ door: frame.door, moving: frame.moving }),
        delay: frame.delay,
      }))
      garage.door = frames[frames.length - 1].door
      garage.motion = true
      return responses
    }

    case 'light_command': {
      if (!garage.caps.light) return [{ type: 'error', message: 'Light not supported' }]
      const action = data.action || 'toggle'
      const turnOn = action === 'on' || (action === 'toggle' && garage.light !== 'on')
      garage.light = turnOn ? 'on' : 'off'
      return [garageStatus()]
    }

    case 'lock_command': {
      if (!garage.caps.lock) return [{ type: 'error', message: 'Lock not supported' }]
      const action = data.action || 'toggle'
      const lock = action === 'lock' || (action === 'toggle' && garage.locked !== 'locked')
      garage.locked = lock ? 'locked' : 'unlocked'
      return [garageStatus()]
    }

    case 'garage_sync':
      return [garageStatus()]

    case 'get_protocol':
      return [protocolMessage()]

    case 'set_protocol': {
      const id = data.id
      // Dry settings ride along on the same call (single save path).
      if (data.dry) {
        for (const k of Object.keys(dryCfg)) {
          if (data.dry[k] !== undefined) dryCfg[k] = data.dry[k]
        }
      }
      if (id === 'drycontact') {
        garage.protocol = 'drycontact'
        garage.caps = { ...DRY_CAPS }
        garage.panel = 'none'
        garage.light = 'unknown'
        garage.locked = 'unknown'
        garage.obstruction = false
        garage.motion = false
        // Mirror the firmware: sensor validity follows the reed mode, not
        // just the protocol. Relay-only reports no sensors at all. Hits are
        // derived from the door in garageStatus, so only validity is seeded.
        garage.sensors = { open: false, close: false, valid: dryCfg.sensor_mode !== 0 }
      } else if (id === 'secplus1') {
        garage.protocol = 'secplus1'
        garage.caps = { ...SECPLUS1_CAPS }
        garage.light = 'off'
        garage.locked = 'unlocked'
        garage.panel = 'detected'
        garage.sensors = { open: false, close: false, valid: false }
      } else {
        return [{ type: 'error', message: 'Protocol not supported yet' }]
      }
      return [protocolMessage(), garageStatus()]
    }

    case 'get_mqtt_config':
      return [{ type: 'mqtt_config', ...mqtt }]

    case 'set_mqtt_config':
      mqtt.mode = data.mode || 'off'
      mqtt.uri = data.uri || ''
      mqtt.username = data.username || ''
      mqtt.topic_prefix = data.topic_prefix || 'home/sesame'
      mqtt.password_set = !!data.password
      return [
        { type: 'mqtt_config_saved', success: true },
        { type: 'mqtt_config', ...mqtt },
      ]

    case 'get_zigbee_config':
      return [{ type: 'zigbee_config', ...zigbee }]

    case 'set_zigbee_config':
      zigbee.enabled = !!data.enabled
      if (!zigbee.enabled) zigbee.pairing_remaining_s = 0
      return [
        { type: 'zigbee_saved', success: true },
        { type: 'zigbee_config', ...zigbee },
      ]

    case 'zigbee_pair':
      zigbee.enabled = true
      zigbee.pairing_remaining_s = data.duration_s || 60
      return [
        { type: 'zigbee_pair', success: true },
        { type: 'zigbee_config', ...zigbee },
      ]

    case 'zigbee_leave':
      zigbee.joined = false
      zigbee.commissioned = false
      zigbee.channel = 0
      zigbee.pan_id = 0
      zigbee.pairing_remaining_s = 0
      return [
        { type: 'zigbee_leave', success: true },
        { type: 'zigbee_config', ...zigbee },
      ]

    case 'zigbee_reset':
      zigbee.enabled = false
      zigbee.joined = false
      zigbee.commissioned = false
      zigbee.channel = 0
      zigbee.pan_id = 0
      zigbee.pairing_remaining_s = 0
      return [
        { type: 'zigbee_reset', success: true },
        { type: 'zigbee_config', ...zigbee },
      ]

    case 'get_garage_raw':
      return [
        {
          type: 'garage_raw',
          rx: rxLog,
          tx: txLog,
          rx_total: 582,
          tx_total: 113,
        },
      ]

    case 'log_start': {
      const mockLogs = [
        'I (1000) main: IDF version: 5.4.0',
        'I (1001) main: Starting SesameMaker',
        'D (1002) websocket: Start websocket',
        'I (1003) wifi: WiFi initialized',
        'I (1004) spiffs: SPIFFS mounted',
        'W (1005) garage_controller: Waiting for first status poll',
        'I (1006) webserver: HTTP server started on port 80',
        'D (1007) ws_log: Log forwarding initialized',
        'I (1008) mqtt: MQTT bridge disabled',
        'E (1009) garage_uart: UART timeout, retrying',
      ]
      const id = setInterval(() => {
        if (mockLogs.length > 0) {
          const msg = mockLogs.shift()
          window.__mockLogSend?.({ type: 'log', message: msg })
        } else {
          clearInterval(id)
          logIntervals.delete(id)
        }
      }, 300)
      logIntervals.add(id)
      return [{ type: 'log_started' }]
    }

    case 'log_stop':
      return [{ type: 'log_stopped' }]
  }
}

let settings = {
  ota: {
    requiresPassword: true,
  },
  wifi: {
    connected: false,
    setup: false,
  },
  features: {
    zigbee: true,
  },
}

let mqtt = {
  mode: 'off',
  uri: 'mqtt://192.168.1.50:1883',
  username: 'user',
  password_set: false,
  topic_prefix: 'home/sesame',
}

let zigbee = {
  supported: true,
  enabled: false,
  joined: false,
  commissioned: false,
  channel: 0,
  pan_id: 0,
  pairing_remaining_s: 0,
  lqi: 0,
  lqi_valid: false,
  parent_addr: 0,
  parent_depth: 0,
}

let dryCfg = {
  relay_gpio: 5,
  open_gpio: 17,
  close_gpio: 18,
  sensor_mode: 2,
  active_low: true,
  pulse_ms: 500,
  debounce_ms: 200,
  travel_s: 15,
}

let wifiDisconnected = {
  mode: 'AP+STA',
  mac: '12:34:56:78:90:ab',
  sta: { connected: false, configured_ssid: '' },
  ap: {
    ssid: 'SesameMaker',
    channel: 10,
    auth_mode: 'WPA_WPA2_PSK',
    ip: '192.168.4.1',
    mac: 'ab:cd:ef:12:34:56',
    connected_stations: 1,
    max_connections: 2,
  },
}

let wifiConnected = {
  mode: 'STA',
  mac: '12:34:56:78:90:ab',
  sta: {
    connected: true,
    ssid: 'Wi Believe I Can Fi',
    rssi: -48,
    channel: 8,
    auth_mode: 'WPA2_PSK',
    ip: '192.168.1.10',
    gateway: '192.168.1.1',
    netmask: '255.255.255.0',
  },
}

let systemInfo = {
  device: {
    status: 'Online and operational',
    reset_reason: 'Power-on reset',
    uptime: '5 minutes, 42seconds',
    time: new Date().toLocaleString('en-US'),
  },
  system: { idf_version: '5.4.0', freertos_tasks: 12 },
  hardware: {
    chip_model: 'ESP32',
    chip_revision: 301,
    cpu_cores: 2,
    flash_size: '4.0 MB',
  },
  memory: {
    heap_total: '269.8 KB',
    heap_free: '163.2 KB',
    heap_used: '106.6 KB',
    heap_usage: '39%',
    heap_largest_free_block: '108.0 KB',
    heap_min_free_ever: '145.4 KB',
    internal_total: '302.0 KB',
    internal_free: '194.6 KB',
    internal_usage: '35%',
  },
  psram: { psram_total: '0 bytes', psram_free: '0 bytes', psram_usage: '0%' },
  spiffs: {
    status: 'Mounted and operational',
    partition_size: '960.0 KB',
    partition_label: 'storage',
    partition_address: '0x310000',
    total_space: '875.3 KB',
    used_space: '171.3 KB',
    free_space: '704.0 KB',
    usage: '19%',
    files_count: 4,
    total_size: '168.8 KB',
  },
}
