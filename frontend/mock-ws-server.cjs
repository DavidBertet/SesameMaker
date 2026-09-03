// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Simple mock WebSocket server for frontend development.
// mockdata.js is ESM (package type: module) so it must be dynamically imported.
const WebSocket = require("ws");

import("./src/lib/mockdata.js").then(({ generateMockResponse }) => {
  const wss = new WebSocket.Server({ port: 8080 });
  const mockLogMessages = [
    "I (1000) main: IDF version: 5.4.0",
    "I (1001) main: Starting SesameMaker",
    "D (1002) websocket: Start websocket",
    "I (1003) wifi: WiFi initialized",
    "I (1004) spiffs: SPIFFS mounted",
    "W (1005) garage_controller: Waiting for first status poll",
    "I (1006) webserver: HTTP server started on port 80",
    "D (1007) ws_log: Log forwarding initialized",
    "I (1008) mqtt: MQTT bridge disabled",
    "E (1009) garage_uart: UART timeout, retrying",
  ];

  wss.on("connection", (ws) => {
    let logInterval = null;
    let logQueue = [...mockLogMessages];

    ws.on("message", (msg) => {
      let data;
      try {
        data = JSON.parse(msg);
      } catch (e) {
        return;
      }

      if (data.type === "log_start") {
        logQueue = [...mockLogMessages];
        logInterval = setInterval(() => {
          if (logQueue.length > 0 && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: "log", message: logQueue.shift() }));
          } else if (logQueue.length === 0) {
            clearInterval(logInterval);
            logInterval = null;
          }
        }, 300);
        ws.send(JSON.stringify({ type: "log_started" }));
        return;
      }

      if (data.type === "log_stop") {
        if (logInterval) {
          clearInterval(logInterval);
          logInterval = null;
        }
        ws.send(JSON.stringify({ type: "log_stopped" }));
        return;
      }

      const mockResponses = generateMockResponse(data);

      mockResponses.forEach((response) => {
        setTimeout(
          () => {
            ws.send(JSON.stringify(response));
          },
          500 + (response.delay || 0),
        );
      });
    });

    ws.on("close", () => {
      if (logInterval) clearInterval(logInterval);
    });
  });

  console.log("Mock WebSocket server running on ws://localhost:8080");
});
