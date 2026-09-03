// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { generateMockResponse, cleanupMockLogs } from 'src/lib/mockdata.js'

window.WebSocket = class MockWebSocket extends EventTarget {
  constructor(url) {
    super()
    this.url = url
    this.readyState = WebSocket.CONNECTING
    this.onopen = null
    this.onmessage = null
    this.onclose = null
    this.onerror = null

    // Simulate connection opening
    setTimeout(() => {
      this.readyState = WebSocket.OPEN
      const event = new Event('open')
      this.dispatchEvent(event)

      if (this.onopen) {
        this.onopen(event)
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
