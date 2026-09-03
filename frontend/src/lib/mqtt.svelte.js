import { onMessageType, sendMessage } from 'src/lib/ws.svelte.js'

// MQTT bridge config state
export const mqttState = $state({
  config: {
    mode: 'off', // 'off' | 'master' | 'ha'
    uri: '',
    username: '',
    password_set: false,
    topic_prefix: 'home/sesame',
  },
})

function applyConfig(data) {
  if (typeof data.mode === 'string') mqttState.config.mode = data.mode
  if (typeof data.uri === 'string') mqttState.config.uri = data.uri
  if (typeof data.username === 'string') mqttState.config.username = data.username
  if (typeof data.password_set === 'boolean') mqttState.config.password_set = data.password_set
  if (typeof data.topic_prefix === 'string') mqttState.config.topic_prefix = data.topic_prefix
}

export function initializeMqtt() {
  sendMessage({ type: 'get_mqtt_config' })

  return onMessageType('mqtt_config', (data) => {
    applyConfig(data)
  })
}

export function saveMqttConfig(config) {
  sendMessage({
    type: 'set_mqtt_config',
    mode: config.mode,
    uri: config.uri,
    username: config.username,
    password: config.password || '',
    topic_prefix: config.topic_prefix,
  })
}
