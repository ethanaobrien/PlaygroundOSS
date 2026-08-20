#!/usr/bin/env node

import { spawn } from "node:child_process";
import process from "node:process";

const url = process.argv[2] ?? "http://127.0.0.1:8765/";
const port = 9337;
const profile = `/tmp/playground-web-probe-${process.pid}`;
const chromium = spawn(process.env.PLAYGROUND_CHROMIUM ?? "chromium-browser", [
  "--headless",
  "--no-sandbox",
  "--disable-gpu",
  `--remote-debugging-port=${port}`,
  `--user-data-dir=${profile}`,
  url,
], { stdio: ["ignore", "ignore", "inherit"] });

const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

async function target() {
  for (let attempt = 0; attempt !== 100; ++attempt) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      const targets = await response.json();
      const page = targets.find(candidate => candidate.type === "page");
      if (page)
        return page;
    } catch (_) {
      // Chromium has not opened its debugging endpoint yet.
    }
    await sleep(100);
  }
  throw new Error("Chromium debugging endpoint did not become ready");
}

function connect(webSocketDebuggerUrl) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(webSocketDebuggerUrl);
    socket.addEventListener("open", () => resolve(socket), { once: true });
    socket.addEventListener("error", reject, { once: true });
  });
}

let commandId = 0;
function command(socket, method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++commandId;
    const listener = event => {
      const message = JSON.parse(event.data);
      if (message.id !== id)
        return;
      socket.removeEventListener("message", listener);
      if (message.error)
        reject(new Error(`${method}: ${message.error.message}`));
      else
        resolve(message.result);
    };
    socket.addEventListener("message", listener);
    socket.send(JSON.stringify({ id, method, params }));
  });
}

async function waitForPass(socket, previous = undefined) {
  for (let attempt = 0; attempt !== 300; ++attempt) {
    const response = await command(socket, "Runtime.evaluate", {
      expression: "JSON.stringify(document.body.dataset)",
      returnByValue: true,
    });
    const data = JSON.parse(response.result.value ?? "{}");
    if (data.probe === "fail")
      throw new Error(data.message ?? "probe failed without a message");
    if (data.probe === "pass" && Number(data.count) !== previous)
      return Number(data.count);
    await sleep(100);
  }
  throw new Error("web platform probe timed out");
}

try {
  const page = await target();
  const socket = await connect(page.webSocketDebuggerUrl);
  socket.addEventListener("message", event => {
    const message = JSON.parse(event.data);
    if (message.method === "Runtime.exceptionThrown")
      console.error(JSON.stringify(message.params.exceptionDetails));
    if (message.method === "Runtime.consoleAPICalled")
      console.error(...message.params.args.map(argument => argument.value ?? argument.description));
  });
  await command(socket, "Runtime.enable");
  const first = await waitForPass(socket);
  await command(socket, "Page.reload", { ignoreCache: true });
  const second = await waitForPass(socket, first);
  if (second !== first + 1)
    throw new Error(`OPFS persistence mismatch: first=${first}, second=${second}`);
  console.log(`web platform probe passed: OPFS runs ${first} -> ${second}`);
  socket.close();
} finally {
  chromium.kill("SIGTERM");
}
