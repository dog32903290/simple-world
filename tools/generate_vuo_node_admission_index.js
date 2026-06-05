#!/usr/bin/env node

const fs = require("node:fs");
const path = require("node:path");

const repoRoot = path.resolve(__dirname, "..");
const vuoNodeDir = path.join(repoRoot, "vuo-nodes");
const testsDir = path.join(repoRoot, "tests");
const outputPath = path.join(repoRoot, "docs/contracts/vuo_node_admission_index.json");

const requiredContext = [
  "graphId",
  "frameId",
  "commandId",
  "nodeId",
  "backendId",
  "artifactPath",
  "diagnosticCode"
];

const directTestCache = buildDirectTestCache();
const entries = fs.readdirSync(vuoNodeDir)
  .filter((name) => name.endsWith(".c"))
  .sort()
  .map((fileName) => buildEntry(fileName));

const index = {
  kind: "VuoNodeAdmissionIndex",
  version: "0.1",
  generatedAt: "2026-06-06",
  generator: "tools/generate_vuo_node_admission_index.js",
  nodeCount: entries.length,
  entries
};

fs.writeFileSync(outputPath, `${JSON.stringify(index, null, 2)}\n`);

function buildEntry(fileName) {
  const nodeId = fileName.replace(/\.c$/, "");
  const sourcePath = `vuo-nodes/${fileName}`;
  const source = fs.readFileSync(path.join(vuoNodeDir, fileName), "utf8");
  const sourceTitle = matchString(source, /"title"\s*:\s*"([^"]+)"/) || nodeIdToCreatorName(nodeId);
  const creatorName = sourceTitle.startsWith("my_") ? sourceTitle : nodeIdToCreatorName(nodeId);
  const description = matchString(source, /"description"\s*:\s*"([^"]+)"/) || "";
  const admission = inferAdmission(nodeId, creatorName, description);
  const state = inferState(nodeId);
  const family = inferFamily(nodeId);
  const parity = inferParity(nodeId, admission);
  const ports = parsePorts(source);
  const tests = directTestCache.get(nodeId) || [];

  return {
    nodeId,
    creatorName,
    sourceTitle,
    sourcePath,
    family,
    admission,
    ports,
    state,
    color: inferColor(nodeId, description),
    flow: {
      timeOwner: nodeId === "my.runtime.clock.mainClock" ? "my_MainClock" : "my_MainClock",
      frameOwner: "my_MainClock",
      dirtyOwner: "commandGraph",
      seedOwner: hasRandomMeaning(nodeId) ? "NodeSpec" : "none",
      eventOrdering: nodeId === "my.runtime.clock.mainClock"
        ? "hostDisplayRefresh-then-renderTick-then-nodeEvent"
        : "commandLogIndex-then-renderTick-then-nodeEvent",
      commandOwner: "commandGraph"
    },
    backendPolicy: {
      metal: "proof-only",
      vuo: admission === "proof-only" ? "proof-only" : parity.vuo,
      webgl: "disabled",
      software: "fallback",
      missingCapability: "diagnostic-visible"
    },
    parity,
    failureCodes: inferFailureCodes(nodeId),
    observability: { requiredContext },
    evidence: {
      source: sourcePath,
      tests: tests.length > 0 ? tests : ["tests/tixl_vuo_port_status_board.test.js"],
      proofLevel: admission === "proof-only" ? "batch-or-adapter-proof" : "source-and-vuo-test-surface"
    }
  };
}

function parsePorts(source) {
  const ports = [];
  const lines = source.split(/\r?\n/);
  for (const line of lines) {
    const input = line.match(/VuoInputData\(([^,\s)]+)(?:,\s*(\{.*\}))?\)\s+([A-Za-z_][A-Za-z0-9_]*)/);
    if (input) {
      ports.push(buildPort("input", input[3], input[1], input[2]));
      continue;
    }
    const output = line.match(/VuoOutputData\(([^,\s)]+)(?:,\s*(\{.*\}))?\)\s+([A-Za-z_][A-Za-z0-9_]*)/);
    if (output) {
      ports.push(buildPort("output", output[3], output[1], output[2]));
    }
  }
  return ports.length > 0 ? ports : [{
    id: "event",
    direction: "input",
    type: "Event",
    eventFlow: "event",
    cardinality: "single",
    default: null,
    range: null
  }];
}

function buildPort(direction, id, type, metadataText) {
  const metadata = parseMetadata(metadataText);
  const port = {
    id,
    direction,
    type,
    eventFlow: direction === "input" ? "data+event" : "data",
    cardinality: "single",
    default: Object.hasOwn(metadata, "default") ? metadata.default : null,
    range: null
  };
  const min = metadata.suggestedMin ?? metadata.min;
  const max = metadata.suggestedMax ?? metadata.max;
  if (typeof min === "number" && typeof max === "number") {
    port.range = { min, max };
  }
  if (metadata.name) port.label = metadata.name;
  return port;
}

function parseMetadata(metadataText) {
  if (!metadataText) return {};
  try {
    return JSON.parse(metadataText.replace(/,\s*\)\s*$/, ""));
  } catch {
    return {};
  }
}

function inferAdmission(nodeId, creatorName, description) {
  if (nodeId.includes(".batch.") || /proof/i.test(creatorName) || /proof/i.test(description)) return "proof-only";
  return "vuo";
}

function inferState(nodeId) {
  if (nodeId.includes("renderTarget") || nodeId.includes(".load.") || nodeId.includes("setMaterial")) return "external-state";
  if (/(clock|time|cache|delay|keep|previous|history|anim|oscillate|stopWatch|playback)/i.test(nodeId)) return "stateful";
  return "stateless";
}

function inferFamily(nodeId) {
  if (nodeId.includes(".clock.") || nodeId.includes(".anim.time.")) return "clock";
  if (nodeId.includes(".render.") || nodeId.includes("clearRenderTarget")) return "command";
  if (nodeId.includes(".field.")) return "shader-field";
  if (nodeId.includes(".mesh.")) return "mesh";
  if (nodeId.includes(".string.")) return "string";
  if (nodeId.includes(".numbers.")) return "value";
  if (nodeId.includes(".image.analyze.")) return "debug";
  if (nodeId.includes(".image.generate.")) return "texture-source";
  if (nodeId.includes(".image.use.keepPreviousFrame")) return "texture-state";
  if (nodeId.includes("renderTarget")) return "texture-output";
  if (nodeId.includes(".image.")) return "texture-filter";
  return "proof-only";
}

function inferParity(nodeId, admission) {
  if (nodeId.includes("renderTarget") || nodeId === "my.runtime.clock.mainClock") {
    return { tixl: "host-layer-proof", vuo: "host-layer-proof" };
  }
  if (admission === "proof-only") return { tixl: "visual-proof", vuo: "visual-proof" };
  if (nodeId.startsWith("myworld.tixl.")) return { tixl: "visual-proof", vuo: "body-layer-adapter" };
  return { tixl: "body-layer-adapter", vuo: "body-layer-adapter" };
}

function inferFailureCodes(nodeId) {
  const codes = ["backend.capability.missing", "node.port.missing", "graph.edge.type_mismatch"];
  if (!nodeId.includes(".batch.")) codes.push("node.param.out_of_range");
  return codes;
}

function inferColor(nodeId, description) {
  const token = matchString(description, /(ColorFor[A-Za-z]+|#[0-9A-Fa-f]{6})/) || "ColorForProof";
  return { role: inferFamily(nodeId), token };
}

function hasRandomMeaning(nodeId) {
  return /(random|noise|hash|perlin|worley|grain)/i.test(nodeId);
}

function nodeIdToCreatorName(nodeId) {
  const last = nodeId.split(".").at(-1) || nodeId;
  return `my_${last.replace(/(^|[^A-Za-z0-9])([A-Za-z0-9])/g, (_, __, char) => char.toUpperCase()).replace(/^[a-z]/, (char) => char.toUpperCase())}`;
}

function matchString(text, regex) {
  const match = text.match(regex);
  return match ? match[1] : null;
}

function buildDirectTestCache() {
  const cache = new Map();
  const testFiles = fs.readdirSync(testsDir)
    .filter((name) => name.endsWith(".test.js"))
    .map((name) => `tests/${name}`);
  const testText = testFiles.map((relativePath) => ({
    relativePath,
    text: fs.readFileSync(path.join(repoRoot, relativePath), "utf8")
  }));

  for (const fileName of fs.readdirSync(vuoNodeDir).filter((name) => name.endsWith(".c"))) {
    const nodeId = fileName.replace(/\.c$/, "");
    const matches = testText
      .filter(({ text }) => text.includes(fileName) || text.includes(nodeId))
      .map(({ relativePath }) => relativePath);
    cache.set(nodeId, matches);
  }
  return cache;
}
