#!/usr/bin/env node
// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Tail the device's ESP-IDF logs over the debug websocket, no dependencies.
// The device IP changes, so it is asked when not given on the command line.
//
//   node tools/ws-logs.cjs 192.168.1.43
//   node tools/ws-logs.cjs 192.168.1.43 --tag GARAGE_CTRL
//   node tools/ws-logs.cjs 192.168.1.43 --all    # also show status/raw frames
//   node tools/ws-logs.cjs 192.168.1.43 --timeout 30
//
// Sends log_start on connect, log_stop on Ctrl+C. Prints each line with the
// reception time on this machine.

const http = require("http");
const crypto = require("crypto");
const readline = require("readline");

const ARGV = process.argv.slice(2);
const parse = (flag, fallback) => {
  const i = ARGV.indexOf(flag);
  return i >= 0 && ARGV[i + 1] ? ARGV[i + 1] : fallback;
};
const HOST = parse("-H", ARGV[0] && !ARGV[0].startsWith("-") ? ARGV[0] : null);
const SHOW_ALL = ARGV.includes("--all");
const FILTER = parse("--tag", null) || parse("--filter", null);
const TIMEOUT_S = parseInt(parse("--timeout", "0"), 10) || 0;

const key = crypto.randomBytes(16).toString("base64");
let req = null;

function connect(host) {
  const targetIp = host.includes(":") ? host.split(":")[0] : host;
  req = http.request({
    host: targetIp,
    port: 80,
    path: "/ws",
    headers: {
      Connection: "Upgrade",
      Upgrade: "websocket",
      "Sec-WebSocket-Key": key,
      "Sec-WebSocket-Version": 13,
    },
  });
  req.targetIp = targetIp;
  req.on("upgrade", handleUpgrade);
  req.on("error", (e) => {
    console.log(`Could not connect to ${targetIp}: ${e.message}`);
    process.exit(1);
  });
  req.end();
}

const now = () => new Date().toTimeString().slice(0, 8);
let buf = Buffer.alloc(0);
let startClock = Date.now();
let elapsed = 0;

function sendFrame(opcode, payload) {
  payload = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  const mask = crypto.randomBytes(4);
  const masked = Buffer.from(payload.map((b, i) => b ^ mask[i % 4]));
  const len = payload.length;
  let header;
  if (len < 126) header = Buffer.from([0x80 | opcode, 0x80 | len]);
  else if (len < 65536)
    header = Buffer.from([0x80 | opcode, 0x80 | 126, len >> 8, len & 0xff]);
  else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 0x80 | 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  req.socket.write(Buffer.concat([header, mask, masked]));
}

function handleUpgrade(res, socket) {
  if (res.statusCode !== 101) {
    console.log(`Upgrade failed: HTTP ${res.statusCode}`);
    process.exit(1);
  }
  console.log(`${now()} connected to ws://${req.targetIp}/ws (Ctrl+C to stop)`);
  sendFrame(1, JSON.stringify({ type: "log_start" }));

  const tick = () => {
    elapsed = (Date.now() - startClock) / 1000;
    if (TIMEOUT_S && elapsed >= TIMEOUT_S) cleanup();
  };
  setInterval(tick, 200);

  socket.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    while (true) {
      if (buf.length < 2) break;
      const opcode = buf[0] & 0x0f;
      const masked = (buf[1] & 0x80) !== 0;
      const frameLen = buf[1] & 0x7f;
      let len = frameLen;
      let off = 2;
      if (frameLen === 126) {
        if (buf.length < 4) break;
        len = buf.readUInt16BE(2);
        off = 4;
      } else if (frameLen === 127) {
        if (buf.length < 10) break;
        len = Number(buf.readBigUInt64BE(2));
        off = 10;
      }
      const maskLen = masked ? 4 : 0;
      if (buf.length < off + maskLen + len) break;
      let payload = buf.slice(off + maskLen, off + maskLen + len);
      if (masked) {
        const m = buf.slice(off, off + 4);
        payload = Buffer.from(payload.map((b, i) => b ^ m[i % 4]));
      }
      buf = buf.slice(off + maskLen + len);

      if (opcode === 8) return process.exit(0); // close
      if (opcode === 9) return sendFrame(10, payload); // ping -> pong
      if (opcode !== 1) continue; // skip binary/continuation

      let json;
      try {
        json = JSON.parse(payload.toString("utf8"));
      } catch {
        continue;
      }
      if (json.type === "log" && typeof json.message === "string") {
        if (FILTER && !json.message.includes(FILTER)) continue;
        console.log(`${now()} ${json.message}`);
      } else if (SHOW_ALL) {
        console.log(`${now()} ${payload.toString("utf8")}`);
      }
    }
  });

  socket.on("close", () => process.exit(0));
  socket.on("error", (e) => {
    console.log(`socket error: ${e.message}`);
    process.exit(1);
  });
}

function cleanup() {
  try {
    sendFrame(1, JSON.stringify({ type: "log_stop" }));
    setTimeout(() => process.exit(0), 200);
  } catch {
    process.exit(0);
  }
}
process.on("SIGINT", cleanup);
process.on("SIGTERM", cleanup);

// The device IP can change - ask for it unless provided on the command line.
if (HOST) {
  connect(HOST);
} else {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  });
  rl.question("Device IP? ", (ans) => {
    rl.close();
    const ip = (ans || "").trim();
    if (!ip) {
      console.log("No IP given, exiting.");
      process.exit(1);
    }
    connect(ip);
  });
}
