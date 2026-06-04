#!/usr/bin/env node

const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");

async function main() {
  if (process.argv.length !== 5) {
    console.error("usage: smoke_shader_webgl_server.js <server.js> <shader_source.glsl> <out_dir>");
    return 2;
  }

  const serverScript = process.argv[2];
  const shaderPath = process.argv[3];
  const outDir = process.argv[4];
  fs.mkdirSync(outDir, { recursive: true });

  const shaderSource = fs.readFileSync(shaderPath, "utf8");
  const server = spawn("node", [serverScript], {
    cwd: path.resolve(__dirname, "../../.."),
    stdio: ["pipe", "pipe", "pipe"],
  });
  const stderr = [];
  server.stderr.on("data", (chunk) => stderr.push(chunk.toString()));

  try {
    const ready = await readJsonLine(server.stdout);
    const started = performance.now();
    server.stdin.write(JSON.stringify({ id: "compile-1", shaderSource }) + "\n");
    const first = await readJsonLine(server.stdout);
    server.stdin.write(JSON.stringify({ id: "compile-2", shaderSource }) + "\n");
    const second = await readJsonLine(server.stdout);
    const totalMs = performance.now() - started;

    const payload = {
      ok: ready.type === "ready" && first.ok === true && second.ok === true,
      ready,
      totalRequestMs: totalMs,
      responses: [first, second],
    };
    fs.writeFileSync(path.join(outDir, "webgl_server_smoke.json"), JSON.stringify(payload, null, 2) + "\n", "utf8");
    server.stdin.write(JSON.stringify({ type: "shutdown" }) + "\n");
    await waitForExit(server);
    return payload.ok ? 0 : 1;
  } catch (error) {
    fs.writeFileSync(path.join(outDir, "webgl_server_smoke.json"), JSON.stringify({
      ok: false,
      error: error.message,
      stderr: stderr.join(""),
    }, null, 2) + "\n", "utf8");
    server.kill("SIGTERM");
    return 1;
  }
}

function readJsonLine(stream) {
  return new Promise((resolve, reject) => {
    let buffer = "";
    const timeout = setTimeout(() => {
      cleanup();
      reject(new Error("Timed out waiting for JSON line."));
    }, 20000);

    const onData = (chunk) => {
      buffer += chunk.toString();
      const newline = buffer.indexOf("\n");
      if (newline === -1) return;
      const line = buffer.slice(0, newline);
      cleanup();
      try {
        resolve(JSON.parse(line));
      } catch (error) {
        reject(error);
      }
    };

    function cleanup() {
      clearTimeout(timeout);
      stream.off("data", onData);
    }

    stream.on("data", onData);
  });
}

function waitForExit(child) {
  return new Promise((resolve) => {
    child.once("exit", resolve);
  });
}

main().then((code) => {
  process.exitCode = code;
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
