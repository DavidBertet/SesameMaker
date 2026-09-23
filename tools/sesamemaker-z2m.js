// Zigbee2MQTT external converter for SesameMaker (DavidBertet).
// Gives Home Assistant a real garage-door cover (like the MQTT discovery
// does) instead of bare switches: door = cover, light/lock as named
// switches.
//
// Install: copy to the z2m external_converters dir (next to
// configuration.yaml) as sesamemaker.js then restart z2m and re-interview the device.
//
// Endpoint-aware: the opener only builds the endpoints its protocol drives
// (secplus1: door + light + lock_remotes; dry-contact: door only), and this
// converter mirrors exactly the interviewed endpoints - no stale entities
// after a protocol switch, just re-interview (or re-pair) in z2m.
const exposes = require('zigbee-herdsman-converters/lib/exposes')
const reporting = require('zigbee-herdsman-converters/lib/reporting')

const e = exposes.presets

// Endpoint IDs must match ZB_EP_* in backend/src/app/zigbee_garage.c.
const EP = { door: 10, light: 11, lock_remotes: 12 }

// Door cover driven by the genOnOff cluster on endpoint 10:
// ON = open, OFF = closed. STOP has no meaning on a binary door.
const fzDoorCover = {
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

const tzDoorCover = {
  key: ['state'],
  convertSet: async (entity, key, value, meta) => {
    if (meta.endpoint_name !== undefined && meta.endpoint_name !== 'door') return
    if (value === 'STOP') return // binary door: nothing to stop
    const endpoint = entity.getDevice().getEndpoint(EP.door)
    await endpoint.command('genOnOff', value === 'OPEN' ? 'on' : 'off', {}, {})
    return { state: value }
  },
}

// ON/OFF for light + lock_remotes, routed by endpoint name. Returns
// undefined for anything else so the door converter (or nothing) handles it.
const tzSwitch = {
  key: ['state'],
  convertSet: async (entity, key, value, meta) => {
    const epId = EP[meta.endpoint_name]
    if (epId === undefined || meta.endpoint_name === 'door') return
    const endpoint = entity.getDevice().getEndpoint(epId)
    await endpoint.command('genOnOff', value === 'ON' ? 'on' : 'off', {}, {})
    return { state: value }
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
      return endpoints
    },
    exposes: (device) => {
      const has = (id) => typeof device.getEndpoint === 'function' && device.getEndpoint(id)
      const list = [e.cover().withEndpoint('door').withDescription('Garage door')]
      if (has(EP.light)) list.push(e.switch().withEndpoint('light').withDescription('Opener lamp'))
      if (has(EP.lock_remotes))
        list.push(e.switch().withEndpoint('lock_remotes').withDescription('Lock remotes (ON = remotes disabled)'))
      return list
    },
    fromZigbee: [fzDoorCover, fzSwitch(EP.light), fzSwitch(EP.lock_remotes)],
    toZigbee: [tzDoorCover, tzSwitch],
    configure: [configure],
  },
]
