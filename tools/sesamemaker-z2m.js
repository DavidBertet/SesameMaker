// Zigbee2MQTT external converter for SesameMaker (DavidBertet).
// The door is a plain switch with OPEN/CLOSE values (not a cover: both the
// exposes Cover and HA discovery hardcode a STOP button, and the on/off-only
// Zigbee model has no mid-travel halt to back it — a dead button is worse
// than a toggle). Light/lock are named switches as usual.
//
// Install: copy to the z2m external_converters dir (next to
// configuration.yaml) as sesamemaker.js then restart z2m and re-interview the device.
//
// Endpoint-aware: the opener only builds the endpoints its protocol drives
// (secplus1: door + light + lock_remotes + obstruction; dry-contact: door
// only), and this converter mirrors exactly the interviewed endpoints - no
// stale entities after a protocol switch, just re-interview (or re-pair)
// in z2m.
const exposes = require('zigbee-herdsman-converters/lib/exposes')
const reporting = require('zigbee-herdsman-converters/lib/reporting')

const e = exposes.presets

// Endpoint IDs must match ZB_EP_* in backend/src/app/zigbee_garage.c.
// obstruction is report-only (the device ignores writes to it).
const EP = { door: 10, light: 11, lock_remotes: 12, obstruction: 13 }

// Door state driven by the genOnOff cluster on endpoint 10:
// ON = open, OFF = closed. Reports OPEN/CLOSE like the switch exposes below.
const fzDoorSwitch = {
  cluster: 'genOnOff',
  type: ['attributeReport', 'readResponse'],
  convert: (model, msg, publish, options, meta) => {
    if (msg.endpoint.ID !== EP.door || !msg.data.hasOwnProperty('onOff')) return
    return { state: msg.data.onOff === 1 ? 'OPEN' : 'CLOSE' }
  },
}

// Plain-switch reports for the optional endpoints (herdsman maps the
// returned state onto the endpoint-named property via msg.endpoint).
const fzSwitch = (epId) => ({
  cluster: 'genOnOff',
  type: ['attributeReport', 'readResponse'],
  convert: (model, msg, publish, options, meta) => {
    if (msg.endpoint.ID !== epId || !msg.data.hasOwnProperty('onOff')) return
    return { state: msg.data.onOff === 1 ? 'ON' : 'OFF' }
  },
})

// Single state setter for every settable endpoint, switched by endpoint
// name: the door speaks OPEN/CLOSE, light + lock_remotes speak ON/OFF.
// Anything else (obstruction reports, unknown names, manual STOP publishes)
// returns undefined — there is no halt to back it and nothing else to drive.
const tzState = {
  key: ['state'],
  convertSet: async (entity, key, value, meta) => {
    const name = meta.endpoint_name
    if (name === undefined || name === 'door') {
      if (value !== 'OPEN' && value !== 'CLOSE') return
      const endpoint = entity.getDevice().getEndpoint(EP.door)
      await endpoint.command('genOnOff', value === 'OPEN' ? 'on' : 'off', {}, {})
      return { state: value }
    }
    if (name === 'light' || name === 'lock_remotes') {
      if (value !== 'ON' && value !== 'OFF') return
      const endpoint = entity.getDevice().getEndpoint(EP[name])
      await endpoint.command('genOnOff', value === 'ON' ? 'on' : 'off', {}, {})
      return { state: value }
    }
    return
  },
}

const configure = async (device, coordinatorEndpoint) => {
  for (const epId of Object.values(EP)) {
    const endpoint = device.getEndpoint(epId)
    if (!endpoint) continue
    await reporting.bind(endpoint, coordinatorEndpoint, ['genOnOff'])
    await reporting.onOff(endpoint)
  }
}

module.exports = [
  {
    zigbeeModel: ['SesameMaker'],
    model: 'SesameMaker',
    vendor: 'DavidBertet',
    description: 'SesameMaker garage door opener',
    endpoint: (device) => {
      const endpoints = { door: EP.door }
      if (device.getEndpoint(EP.light)) endpoints.light = EP.light
      if (device.getEndpoint(EP.lock_remotes)) endpoints.lock_remotes = EP.lock_remotes
      if (device.getEndpoint(EP.obstruction)) endpoints.obstruction = EP.obstruction
      return endpoints
    },
    exposes: (device) => {
      const has = (id) => typeof device.getEndpoint === 'function' && device.getEndpoint(id)
      const list = [
        e
          .switch_()
          .withState('state', false, 'Garage door', undefined, 'OPEN', 'CLOSE')
          .withEndpoint('door'),
      ]
      if (has(EP.light)) list.push(e.switch().withEndpoint('light').withDescription('Opener lamp'))
      if (has(EP.lock_remotes))
        list.push(e.switch().withEndpoint('lock_remotes').withDescription('Lock remotes (ON = remotes disabled)'))
      if (has(EP.obstruction))
        list.push(e.switch().withEndpoint('obstruction').withDescription('Obstruction sensor (ON = obstructed, read-only)'))
      return list
    },
    fromZigbee: [fzDoorSwitch, fzSwitch(EP.light), fzSwitch(EP.lock_remotes), fzSwitch(EP.obstruction)],
    toZigbee: [tzState],
    configure: [configure],
  },
]
