#!/usr/bin/env node

import { mkdir } from "node:fs/promises";
import { spawn } from "node:child_process";
import process from "node:process";

const url = process.argv[2] ?? "http://127.0.0.1:8765/";
const profile = process.argv[3] ?? "/tmp/playground-web-engine-chromium";
const timeoutSeconds = Number(process.env.PLAYGROUND_WEB_TEST_TIMEOUT ?? "600");
const port = Number(process.env.PLAYGROUND_WEB_DEBUG_PORT ?? "9338");
await mkdir(profile, { recursive: true });

const chromiumArguments = [
  "--headless",
  "--no-sandbox",
  "--enable-unsafe-swiftshader",
  "--disable-dev-shm-usage",
  `--remote-debugging-port=${port}`,
  `--user-data-dir=${profile}`,
];
if (process.env.PLAYGROUND_CHROMIUM_IGNORE_CERTIFICATE_ERRORS === "1")
  chromiumArguments.push("--ignore-certificate-errors");
chromiumArguments.push(url);

const chromium = spawn(process.env.PLAYGROUND_CHROMIUM ?? "chromium-browser",
  chromiumArguments, { stdio: ["ignore", "ignore", "inherit"] });

const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

async function target() {
  for (let attempt = 0; attempt !== 200; ++attempt) {
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

async function evaluate(socket, expression) {
  const response = await command(socket, "Runtime.evaluate", {
    expression,
    returnByValue: true,
  });
  return response.result.value;
}

async function waitForEngine(socket) {
  const startedAt = Date.now();
  let previous = "";
  while (Date.now() - startedAt < timeoutSeconds * 1000) {
    const value = await evaluate(socket, `JSON.stringify((() => {
      const status = document.getElementById('status');
      const canvas = document.getElementById('canvas');
      return {
        phase: document.getElementById('phase')?.textContent ?? '',
        detail: document.getElementById('detail')?.textContent ?? '',
        fatal: document.getElementById('fatal')?.textContent ?? '',
        progress: Number(document.getElementById('progress')?.value ?? 0),
        hidden: status?.classList.contains('hidden') ?? false,
        width: canvas?.width ?? 0,
        height: canvas?.height ?? 0,
        isolated: crossOriginIsolated,
        sharedArrayBuffer: typeof SharedArrayBuffer !== 'undefined',
        engine: globalThis.playgroundEngineReady ?? null
      };
    })())`);
    const state = JSON.parse(value ?? "{}");
    const progress = Math.floor(state.progress * 20) * 5;
    const summary = state.phase === "Installing game data"
      ? `${state.phase}: ${progress}%`
      : `${state.phase}: ${state.detail}`;
    if (summary !== previous) {
      console.log(summary);
      previous = summary;
    }
    if (state.fatal)
      throw new Error(state.fatal);
    if (state.hidden && state.engine?.frames >= 1 &&
        state.engine.width > 0 && state.engine.height > 0 &&
        state.isolated && state.sharedArrayBuffer)
      return state;
    await sleep(250);
  }
  throw new Error(`web engine timed out after ${timeoutSeconds} seconds`);
}

try {
  const page = await target();
  const socket = await connect(page.webSocketDebuggerUrl);
  socket.addEventListener("message", event => {
    const message = JSON.parse(event.data);
    if (message.method === "Runtime.exceptionThrown")
      console.error("browser exception:", message.params.exceptionDetails.text);
    if (message.method === "Runtime.consoleAPICalled") {
      const output = message.params.args.map(argument =>
        argument.value ?? argument.description).join(" ");
      console.error(`browser ${message.params.type}: ${output}`);
    }
  });
  await command(socket, "Runtime.enable");
  const state = await waitForEngine(socket);
  console.log(`web engine reached the frame loop at ` +
              `${state.engine.width}x${state.engine.height}`);
  if (process.env.PLAYGROUND_WEB_TEST_CLICK === "1") {
    const x = Math.floor(state.engine.width / 2);
    const y = Math.floor(state.engine.height / 2);
    await command(socket, "Input.dispatchMouseEvent", {
      type: "mousePressed", x, y, button: "left", clickCount: 1,
    });
    await command(socket, "Input.dispatchMouseEvent", {
      type: "mouseReleased", x, y, button: "left", clickCount: 1,
    });
    const holdSeconds = Number(process.env.PLAYGROUND_WEB_TEST_HOLD ?? "30");
    console.log(`dispatched title-screen click; observing for ${holdSeconds}s`);
    await sleep(holdSeconds * 1000);
    const fatal = await evaluate(socket,
      "document.getElementById('fatal')?.textContent ?? ''");
    if (fatal)
      throw new Error(fatal);
  }
  socket.close();
} finally {
  chromium.kill("SIGTERM");
}
