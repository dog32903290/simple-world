#!/usr/bin/env node

const fs = require("node:fs");
const net = require("node:net");
const os = require("node:os");
const path = require("node:path");
const { spawn } = require("node:child_process");

const TIMEOUT_MS = 15000;

async function main() {
  if (process.argv.length !== 4) {
    console.error("usage: check_shader_webgl_batch.js <shader_source.glsl> <out_dir>");
    return 2;
  }

  const shaderPath = process.argv[2];
  const outDir = process.argv[3];
  fs.mkdirSync(outDir, { recursive: true });

  const shaderSource = fs.readFileSync(shaderPath, "utf8");
  const chrome = findChrome();
  if (!chrome) {
    writeResult(outDir, {
      ok: false,
      backend: "webgl2",
      error: "No Chrome/Chromium browser found for headless WebGL compile.",
      runs: [],
    });
    return 1;
  }

  const htmlPath = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "myworld-webgl-batch-html-")), "check.html");
  fs.writeFileSync(htmlPath, makeHtml(), "utf8");

  const port = await findFreePort();
  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), "myworld-chrome-profile-"));
  const chromeProcess = spawn(chrome, [
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
  ], { stdio: ["ignore", "pipe", "pipe"] });

  let stderr = "";
  chromeProcess.stderr.on("data", (chunk) => {
    stderr += chunk.toString();
  });

  try {
    const result = await withTimeout(runBatch(port, shaderSource), TIMEOUT_MS);
    writeResult(outDir, result);
    return result.ok ? 0 : 1;
  } catch (error) {
    writeResult(outDir, {
      ok: false,
      backend: "webgl2",
      error: error.message,
      stderr,
      runs: [],
    });
    return 1;
  } finally {
    chromeProcess.kill("SIGTERM");
  }
}

async function runBatch(port, shaderSource) {
  const target = await waitForTarget(port);
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.addEventListener("open", resolve, { once: true });
    ws.addEventListener("error", reject, { once: true });
  });

  try {
    await waitForPageReady(ws);
    const runs = [];
    for (let i = 0; i < 2; i += 1) {
      const started = performance.now();
      const response = await cdp(ws, "Runtime.evaluate", {
        expression: `compileGenerated(${JSON.stringify(shaderSource)})`,
        returnByValue: true,
        awaitPromise: false,
      });
      const durationMs = performance.now() - started;
      const value = response.result?.result?.value;
      runs.push({ ...value, durationMs });
    }

    return {
      ok: runs.every((run) => run.ok),
      backend: "webgl2",
      runs,
    };
  } finally {
    ws.close();
  }
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

async function waitForPageReady(ws) {
  const deadline = Date.now() + TIMEOUT_MS;
  while (Date.now() < deadline) {
    const response = await cdp(ws, "Runtime.evaluate", {
      expression: "typeof compileGenerated === 'function'",
      returnByValue: true,
      awaitPromise: false,
    });
    if (response.result?.result?.value === true) return;
    await sleep(100);
  }
  throw new Error("WebGL batch page did not become ready.");
}

function cdp(ws, method, params) {
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

function withTimeout(promise, ms) {
  return Promise.race([
    promise,
    new Promise((_, reject) => setTimeout(() => reject(new Error(`Timed out after ${ms}ms.`)), ms)),
  ]);
}

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
    return { ok: false, error: "WebGL2 context unavailable" };
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

  gl.deleteProgram(program);
  gl.deleteShader(vertex.shader);
  gl.deleteShader(fragment.shader);

  return {
    ok: vertex.ok && fragment.ok && linkOk,
    durationMs: performance.now() - started,
    vertexLog: vertex.log,
    fragmentLog: fragment.log,
    programLog: gl.getProgramInfoLog(program) || "",
    fragmentSourceLength: fragmentSource.length
  };
}
</script>`;
}

function writeResult(outDir, payload) {
  fs.writeFileSync(path.join(outDir, "webgl_compile_batch.json"), JSON.stringify(payload, null, 2) + "\n", "utf8");
}

main().then((code) => {
  process.exitCode = code;
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
