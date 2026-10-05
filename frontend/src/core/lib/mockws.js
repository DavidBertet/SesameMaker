// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { generateMockResponse, cleanupMockLogs, mockPushStreams } from 'src/lib/mockdata.js'

window.WebSocket = class MockWebSocket extends EventTarget {
  constructor(url) {
    super()
    this.url = url
    this.readyState = WebSocket.CONNECTING
    this.onopen = null
    this.onmessage = null
    this.onclose = null
    this.onerror = null
    // Periodic pushes declared by the app's mockdata (empty = none). Core
    // owns the timers so a closed socket can never leave one behind.
    this.streams = []

    // Simulate connection opening
    setTimeout(() => {
      this.readyState = WebSocket.OPEN
      const event = new Event('open')
      this.dispatchEvent(event)

      if (this.onopen) {
        this.onopen(event)
      }

      for (const stream of mockPushStreams || []) {
        this.streams.push(
          setInterval(() => {
            const message = typeof stream.message === 'function' ? stream.message() : stream.message
            if (!message) return
            const tick = new MessageEvent('message', { data: JSON.stringify(message) })
            this.dispatchEvent(tick)
            if (this.onmessage) this.onmessage(tick)
          }, stream.intervalMs),
        )
      }
    }, 100)
  }

  send(msg) {
    let data
    try {
      data = JSON.parse(msg)
    } catch (e) {
      return
    }

    if (data.type === 'log_start') {
      window.__mockLogSend = (obj) => {
        const event = new MessageEvent('message', {
          data: JSON.stringify(obj),
        })
        this.dispatchEvent(event)
        if (this.onmessage) this.onmessage(event)
      }
    }

    const mockResponses = generateMockResponse(data)

    mockResponses.forEach((response) => {
      setTimeout(
        () => {
          const event = new MessageEvent('message', {
            data: JSON.stringify(response),
          })
          this.dispatchEvent(event)

          if (this.onmessage) {
            this.onmessage(event)
          }
        },
        500 + (response.delay || 0),
      )
    })
  }

  close() {
    this.streams.forEach(clearInterval)
    this.streams = []
    cleanupMockLogs()
    window.__mockLogSend = null
    this.readyState = WebSocket.CLOSED
    const event = new Event('close')
    this.dispatchEvent(event)

    if (this.onclose) {
      this.onclose(event)
    }
  }
}
