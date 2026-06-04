#!/usr/bin/env node

const fs = require("node:fs");
const net = require("node:net");
const os = require("node:os");
const path = require("node:path");
const readline = require("node:readline");
const { spawn } = require("node:child_process");

const TIMEOUT_MS = 15000;

let chromeProcess = null;
let ws = null;

async function main() {
  const chrome = findChrome();
  if (!chrome) {
    writeLine({
      type: "error",
      backend: "webgl2",
      error: "No Chrome/Chromium browser found for headless WebGL compile.",
    });
    return 1;
  }

  await startChrome(chrome);
  writeLine({ type: "ready", backend: "webgl2" });

  const rl = readline.createInterface({
    input: process.stdin,
    crlfDelay: Infinity,
  });

  for await (const line of rl) {
    if (!line.trim()) continue;
    let message;
    try {
      message = JSON.parse(line);
    } catch (error) {
      writeLine({ ok: false, error: `Invalid JSON: ${error.message}` });
      continue;
    }

    if (message.type === "shutdown") {
      cleanup();
      process.exit(0);
      break;
    }

    if (!message.id || typeof message.shaderSource !== "string") {
      writeLine({
        id: message.id ?? null,
        ok: false,
        error: "Expected { id, shaderSource } or { type: 'shutdown' }.",
      });
      continue;
    }

    try {
      const result = await compileShader(message.shaderSource);
      writeLine({ id: message.id, ...result });
    } catch (error) {
      writeLine({ id: message.id, ok: false, error: error.message });
    }
  }

  cleanup();
  return 0;
}

async function startChrome(chrome) {
  const htmlPath = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "myworld-webgl-server-html-")), "server.html");
  fs.writeFileSync(htmlPath, makeHtml(), "utf8");

  const port = await findFreePort();
  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), "myworld-chrome-profile-"));
  chromeProcess = spawn(chrome, [
    "--headless=new",
    "--enable-webgl",
    "--ignore-gpu-blocklist",
    "--use-angle=swiftshader",
    "--enable-unsafe-swiftshader",
    "--disable-background-networking",
    "--disable-dev-shm-usage",
    "--no-first-run",
    "--no-default-browser-check",
    `--user-data-dir=${profileDir}`,
    `--remote-debugging-port=${port}`,
    `file://${htmlPath}`,
  ], { stdio: ["ignore", "ignore", "pipe"] });

  chromeProcess.stderr.on("data", (chunk) => {
    process.stderr.write(chunk);
  });

  const target = await waitForTarget(port);
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.addEventListener("open", resolve, { once: true });
    ws.addEventListener("error", reject, { once: true });
  });
  await waitForPageReady();
}

async function compileShader(shaderSource) {
  const response = await cdp("Runtime.evaluate", {
    expression: `compileGenerated(${JSON.stringify(shaderSource)})`,
    returnByValue: true,
    awaitPromise: false,
  });
  const value = response.result?.result?.value;
  if (!value) {
    throw new Error("WebGL page returned no compile result.");
  }
  return value;
}

function findChrome() {
  const candidates = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
  ];
  return candidates.find((candidate) => fs.existsSync(candidate));
}

async function findFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      const port = address.port;
      server.close(() => resolve(port));
    });
    server.on("error", reject);
  });
}

async function waitForTarget(port) {
  const deadline = Date.now() + TIMEOUT_MS;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      const targets = await response.json();
      const target = targets.find((entry) => entry.type === "page" && entry.webSocketDebuggerUrl);
      if (target) return target;
    } catch {
      // Chrome is still starting.
    }
    await sleep(100);
  }
  throw new Error("Timed out waiting for Chrome DevTools target.");
}

async function waitForPageReady() {
  const deadline = Date.now() + TIMEOUT_MS;
  while (Date.now() < deadline) {
    const response = await cdp("Runtime.evaluate", {
      expression: "typeof compileGenerated === 'function'",
      returnByValue: true,
      awaitPromise: false,
    });
    if (response.result?.result?.value === true) return;
    await sleep(100);
  }
  throw new Error("WebGL server page did not become ready.");
}

function cdp(method, params) {
  const id = cdp.nextId++;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => {
    const onMessage = (event) => {
      const message = JSON.parse(event.data);
      if (message.id !== id) return;
      ws.removeEventListener("message", onMessage);
      if (message.error) {
        reject(new Error(message.error.message));
      } else {
        resolve(message);
      }
    };
    ws.addEventListener("message", onMessage);
  });
}
cdp.nextId = 1;

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function makeHtml() {
  return `<!doctype html>
<meta charset="utf-8">
<canvas id="c" width="1" height="1"></canvas>
<script>
const canvas = document.getElementById("c");
const gl = canvas.getContext("webgl2");

const vertexSource = \`#version 300 es
precision highp float;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
\`;

function compileShader(type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  return {
    shader,
    ok: gl.getShaderParameter(shader, gl.COMPILE_STATUS),
    log: gl.getShaderInfoLog(shader) || ""
  };
}

function compileGenerated(generatedSource) {
  if (!gl) {
    return { ok: false, backend: "webgl2", error: "WebGL2 context unavailable" };
  }

  const started = performance.now();
  const fragmentSource = \`#version 300 es
precision highp float;
\${generatedSource}
out vec4 outColor;
void main() {
  float hit = raymarch_field_1(vec3(0.0, 0.0, -3.0), normalize(vec3(0.0, 0.0, 1.0)));
  outColor = vec4(hit > 0.0 ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
\`;

  const vertex = compileShader(gl.VERTEX_SHADER, vertexSource);
  const fragment = compileShader(gl.FRAGMENT_SHADER, fragmentSource);
  const program = gl.createProgram();
  gl.attachShader(program, vertex.shader);
  gl.attachShader(program, fragment.shader);
  gl.linkProgram(program);
  const linkOk = gl.getProgramParameter(program, gl.LINK_STATUS);
  const programLog = gl.getProgramInfoLog(program) || "";

  gl.deleteProgram(program);
  gl.deleteShader(vertex.shader);
  gl.deleteShader(fragment.shader);

  return {
    ok: vertex.ok && fragment.ok && linkOk,
    backend: "webgl2",
    durationMs: performance.now() - started,
    vertexLog: vertex.log,
    fragmentLog: fragment.log,
    programLog,
    fragmentSourceLength: fragmentSource.length
  };
}
</script>`;
}

function writeLine(payload) {
  process.stdout.write(JSON.stringify(payload) + "\n");
}

function cleanup() {
  if (ws) {
    ws.close();
    ws = null;
  }
  if (chromeProcess) {
    chromeProcess.kill("SIGTERM");
    chromeProcess = null;
  }
}

process.on("SIGINT", () => {
  cleanup();
  process.exit(130);
});

process.on("SIGTERM", () => {
  cleanup();
  process.exit(143);
});

main().then((code) => {
  process.exitCode = code;
}).catch((error) => {
  writeLine({ type: "error", backend: "webgl2", error: error.message });
  cleanup();
  process.exitCode = 1;
});
