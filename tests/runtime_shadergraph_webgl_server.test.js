const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { spawn, spawnSync } = require("node:child_process");

const repoRoot = path.resolve(__dirname, "..");
const shellCompiler = path.join(repoRoot, "docs/runtime/scripts/compile_shadergraph_shell.py");
const serverScript = path.join(repoRoot, "docs/runtime/scripts/serve_shader_webgl.js");
const fixture = path.join(repoRoot, "docs/runtime/fixtures/sphere_sdf_raymarch.graph.json");

test("persistent WebGL shader server compiles repeated shader requests without restarting Chrome", async () => {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "myworld-webgl-server-"));
  const shell = spawnSync("python3", [shellCompiler, fixture, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(shell.status, 0, shell.stderr || shell.stdout);

  const shaderSource = fs.readFileSync(path.join(outDir, "shader_source.glsl"), "utf8");
  const server = spawn("node", [serverScript], {
    cwd: repoRoot,
    stdio: ["pipe", "pipe", "pipe"],
  });

  const stderrChunks = [];
  server.stderr.on("data", (chunk) => stderrChunks.push(chunk.toString()));

  try {
    const ready = await readJsonLine(server.stdout);
    assert.equal(ready.type, "ready", JSON.stringify(ready));
    assert.equal(ready.backend, "webgl2");

    server.stdin.write(JSON.stringify({ id: "compile-1", shaderSource }) + "\n");
    const first = await readJsonLine(server.stdout);

    server.stdin.write(JSON.stringify({ id: "compile-2", shaderSource }) + "\n");
    const second = await readJsonLine(server.stdout);

    assert.equal(first.id, "compile-1");
    assert.equal(second.id, "compile-2");
    assert.equal(first.ok, true, JSON.stringify(first, null, 2));
    assert.equal(second.ok, true, JSON.stringify(second, null, 2));
    assert.ok(second.durationMs < 1000, `warm server compile took ${second.durationMs}ms`);
  } finally {
    server.stdin.write(JSON.stringify({ type: "shutdown" }) + "\n");
    await waitForExit(server, stderrChunks);
  }
});

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

    const onError = (error) => {
      cleanup();
      reject(error);
    };

    function cleanup() {
      clearTimeout(timeout);
      stream.off("data", onData);
      stream.off("error", onError);
    }

    stream.on("data", onData);
    stream.on("error", onError);
  });
}

function waitForExit(child, stderrChunks) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      child.kill("SIGTERM");
      reject(new Error(`Server did not exit. stderr:\n${stderrChunks.join("")}`));
    }, 5000);

    child.once("exit", () => {
      clearTimeout(timeout);
      resolve();
    });
  });
}
