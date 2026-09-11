// Zigbee2MQTT external converter for SesameMaker (DavidBertet).
// Gives Home Assistant a real garage-door cover (like the MQTT discovery
// does) instead of three bare switches: door = cover, light/lock as named
// switches.
//
// Install: copy to the z2m external_converters dir (next to
// configuration.yaml) as sesamemaker.js then restart z2m and re-interview the device.
//
// NOTE: light/lock_remotes exist only when the opener protocol provides
// them (secplus1: all three; dry-contact: door only). On smaller builds
// delete the missing entries below.
const { onOff } = require('zigbee-herdsman-converters/lib/modernExtend')
const exposes = require('zigbee-herdsman-converters/lib/exposes')
const reporting = require('zigbee-herdsman-converters/lib/reporting')

const e = exposes.presets
const ea = exposes.access

// Door cover driven by the genOnOff cluster on endpoint 10:
// ON = open, OFF = closed. STOP has no meaning on a binary door.
const fzDoorCover = {
  cluster: 'genOnOff',
  type: ['attributeReport', 'readResponse'],
  convert: (model, msg, publish, options, meta) => {
    if (msg.endpoint.ID !== 10 || !msg.data.hasOwnProperty('onOff')) return
    return { state: msg.data.onOff === 1 ? 'OPEN' : 'CLOSE' }
  },
}

const tzDoorCover = {
  key: ['state'],
  convertSet: async (entity, key, value, meta) => {
    const endpoint = entity.getDevice().getEndpoint(10)
    if (value === 'STOP') return // binary door: nothing to stop
    await endpoint.command('genOnOff', value === 'OPEN' ? 'on' : 'off', {}, {})
    return { state: value }
  },
}

const configureDoorCover = async (device, coordinatorEndpoint) => {
  const endpoint = device.getEndpoint(10)
  await reporting.bind(endpoint, coordinatorEndpoint, ['genOnOff'])
  await reporting.onOff(endpoint)
}

module.exports = [
  {
    zigbeeModel: ['SesameMaker'],
    model: 'SesameMaker',
    vendor: 'DavidBertet',
    description: 'SesameMaker garage door opener',
    endpoint: (device) => {
      const endpoints = { door: 10 }
      if (device.getEndpoint(11)) endpoints.light = 11
      if (device.getEndpoint(12)) endpoints.lock_remotes = 12
      return endpoints
    },
    extend: [
      onOff({
        endpointNames: ['light'],
        powerOnBehavior: false,
        description: 'Opener lamp',
      }),
      onOff({
        endpointNames: ['lock_remotes'],
        powerOnBehavior: false,
        description: 'Lock remotes (ON = remotes disabled)',
      }),
    ],
    exposes: [e.cover().withEndpoint('door').withDescription('Garage door')],
    fromZigbee: [fzDoorCover],
    toZigbee: [tzDoorCover],
    configure: [configureDoorCover],
  },
]
