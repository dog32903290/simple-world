const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractsDir = path.join(repoRoot, "docs/contracts");
const schemaPath = path.join(contractsDir, "node_admission.schema.json");
const levelsPath = path.join(contractsDir, "NODE_ADMISSION_LEVELS.md");
const failureTaxonomyPath = path.join(contractsDir, "failure_taxonomy.json");
const proofManifestSchemaPath = path.join(contractsDir, "proof_manifest.schema.json");
const artifactObservabilitySchemaPath = path.join(contractsDir, "artifact_observability.schema.json");
const vuoAdmissionIndexPath = path.join(contractsDir, "vuo_node_admission_index.json");
const manifestsDir = path.join(contractsDir, "node_manifests");
const proofManifestsDir = path.join(contractsDir, "proof_manifests");
const closureIndexPath = path.join(repoRoot, "docs/runtime/NATIVE_RUNTIME_CLOSURE_INDEX.md");

test("node admission schema centralizes creator-facing node contract fields", () => {
  const schema = readJson(schemaPath);
  const required = new Set(schema.required);

  [
    "nodeId",
    "creatorName",
    "family",
    "admission",
    "ports",
    "params",
    "state",
    "color",
    "flow",
    "backendPolicy",
    "parity",
    "failureCodes",
    "observability",
    "maturity",
    "proof"
  ].forEach((field) => assert.ok(required.has(field), `missing required field ${field}`));

  assert.deepEqual(schema.properties.admission.enum, ["runtime", "vuo", "proof-only", "blocked"]);
  assert.deepEqual(schema.properties.state.enum, ["stateless", "stateful", "external-state"]);
  assert.deepEqual(schema.properties.parity.properties.tixl.enum, [
    "semantic-parity",
    "visual-proof",
    "body-layer-adapter",
    "host-layer-proof",
    "not-parity"
  ]);
  assert.ok(schema.properties.flow.required.includes("timeOwner"));
  assert.ok(schema.properties.flow.required.includes("eventOrdering"));
  assert.ok(schema.properties.backendPolicy.required.includes("missingCapability"));
  assert.deepEqual(schema.properties.maturity.required, ["level", "evidence", "nativeUse"]);
  assert.deepEqual(schema.properties.maturity.properties.level.enum, [
    "admissionReady",
    "interactionReady",
    "runtimeReady",
    "nativeExecutable"
  ]);
  assert.deepEqual(schema.properties.maturity.properties.nativeUse.enum, [
    "known-only",
    "editor-graph",
    "runtime-graph",
    "execute-native"
  ]);
  assert.equal(schema.properties.maturity.properties.evidence.minLength, 1);
});

test("proof manifest schema makes claims and nonclaims machine-readable", () => {
  const schema = readJson(proofManifestSchemaPath);
  const required = new Set(schema.required);

  ["id", "ownerLane", "claims", "nonclaims", "fixture", "script", "artifacts", "tests", "freshness", "observability"].forEach((field) => {
    assert.ok(required.has(field), `missing required field ${field}`);
  });
  assert.equal(schema.properties.claims.items.type, "string");
  assert.equal(schema.properties.nonclaims.items.type, "string");
  assert.ok(schema.properties.freshness.required.includes("generatedAt"));
  assert.ok(schema.properties.freshness.required.includes("verificationCommand"));
  assert.ok(schema.properties.observability.required.includes("requiredContext"));
});

test("artifact observability schema names the common black-frame triage envelope", () => {
  const schema = readJson(artifactObservabilitySchemaPath);

  assert.equal(schema.title, "Simple World Artifact Observability Envelope");
  [
    "graphId",
    "frameId",
    "commandId",
    "nodeId",
    "backendId",
    "artifactPath",
    "diagnosticCode",
    "layer"
  ].forEach((field) => assert.ok(schema.required.includes(field), `missing ${field}`));
  assert.deepEqual(schema.properties.layer.enum, [
    "commandGraph",
    "runtimeGraph",
    "scheduler",
    "resource",
    "shader",
    "backend",
    "renderer",
    "aiWorker",
    "importer",
    "canvas"
  ]);
});

test("failure taxonomy defines global creator-facing handling classes", () => {
  const taxonomy = readJson(failureTaxonomyPath);

  assert.equal(taxonomy.kind, "FailureTaxonomy");
  assert.deepEqual(taxonomy.handlingClasses.map((entry) => entry.id), [
    "blocked",
    "fallback",
    "repairable",
    "creator-visible"
  ]);
  assert.ok(taxonomy.requiredDiagnosticContext.includes("graphId"));
  assert.ok(taxonomy.requiredDiagnosticContext.includes("backendId"));
  assert.ok(taxonomy.requiredDiagnosticContext.includes("diagnosticCode"));
  assert.ok(taxonomy.codes.some((entry) => entry.code === "backend.capability.missing" && entry.handling === "fallback"));
  assert.ok(taxonomy.codes.some((entry) => entry.code === "shader.compile.failed" && entry.handling === "creator-visible"));
  assert.ok(taxonomy.codes.some((entry) => entry.code === "render.black_frame" && entry.handling === "repairable"));
});

test("contract docs declare manifests as the creator-facing admission gate", () => {
  const levels = fs.readFileSync(levelsPath, "utf8");
  const closureIndex = fs.readFileSync(closureIndexPath, "utf8");

  assert.match(levels, /No admission record means no creator-facing admission/);
  assert.match(levels, /vuo_node_admission_index\.json/);
  assert.match(levels, /node_admission\.schema\.json/);
  assert.match(levels, /artifact_observability\.schema\.json/);
  assert.match(levels, /failure_taxonomy\.json/);
  assert.match(closureIndex, /Creator-facing node admission is now gated by:/);
  assert.match(closureIndex, /A Vuo node without an entry\s+in `vuo_node_admission_index\.json` is not admitted/);
  assert.match(closureIndex, /runtime or high-risk node without a full manifest is not promoted/);
});

test("Blob node admission manifest admits a runtime node with full proof context", () => {
  const manifest = readManifest("image.generate.basic.blob.json");

  assert.equal(manifest.nodeId, "image.generate.basic.blob");
  assert.equal(manifest.creatorName, "Blob");
  assert.equal(manifest.admission, "runtime");
  assert.equal(manifest.state, "stateless");
  assert.equal(manifest.color.role, "texture-source");
  assert.equal(manifest.flow.timeOwner, "FrameScheduler");
  assert.equal(manifest.flow.eventOrdering, "commandLogIndex-then-frameIndex");
  assert.equal(manifest.backendPolicy.metal, "enabled");
  assert.equal(manifest.backendPolicy.vuo, "body-layer-adapter");
  assert.equal(manifest.backendPolicy.missingCapability, "diagnostic-visible");
  assert.equal(manifest.parity.tixl, "visual-proof");
  assert.equal(manifest.parity.vuo, "body-layer-adapter");
  assert.ok(manifest.params.some((param) => param.id === "scale" && param.range.min === 0 && param.range.max === 1));
  assert.ok(manifest.failureCodes.includes("node.param.out_of_range"));
  assert.ok(manifest.proof.claims.includes("blobTextureGenerated"));
  assert.ok(manifest.proof.nonclaims.includes("semanticParityWithTiXLDX11Blob"));
  assert.equal(manifest.proof.fixture, "docs/runtime/fixtures/native_gpu_patch_runtime_slice.graph.json");
  assert.ok(manifest.proof.artifacts.includes("docs/runtime/artifacts/native_gpu_patch_runtime_slice/frame_stats.json"));
  assert.ok(manifest.proof.tests.includes("tests/native_gpu_patch_runtime_slice.test.js"));
  assertRequiredContext(manifest);
});

test("RenderTarget Vuo manifest cannot overclaim TiXL/DX11 parity", () => {
  const manifest = readManifest("image.output.renderTarget.json");

  assert.equal(manifest.nodeId, "image.output.renderTarget");
  assert.equal(manifest.admission, "runtime");
  assert.equal(manifest.state, "external-state");
  assert.equal(manifest.backendPolicy.vuo, "host-layer-proof");
  assert.equal(manifest.parity.tixl, "host-layer-proof");
  assert.equal(manifest.parity.vuo, "host-layer-proof");
  assert.ok(manifest.proof.nonclaims.includes("tixlDx11RenderTargetParity"));
  assert.ok(manifest.failureCodes.includes("backend.capability.missing"));
  assertRequiredContext(manifest);
});

test("AI worker node manifest binds repairable failures to commandGraph output", () => {
  const manifest = readManifest("ai.worker.authoringAssist.json");

  assert.equal(manifest.nodeId, "ai.worker.authoringAssist");
  assert.equal(manifest.family, "ai-worker");
  assert.equal(manifest.admission, "runtime");
  assert.equal(manifest.flow.commandOwner, "commandGraph");
  assert.equal(manifest.flow.timeOwner, "FrameScheduler");
  assert.ok(manifest.failureCodes.includes("render.black_frame"));
  assert.ok(manifest.failureCodes.includes("ai.proposal.direct_graph_mutation"));
  assert.equal(manifest.backendPolicy.missingCapability, "disabled");
  assert.ok(manifest.proof.claims.includes("commandPlanValidated"));
  assert.ok(manifest.proof.nonclaims.includes("broadNaturalLanguagePatchAuthoring"));
  assertRequiredContext(manifest);
});

test("high-risk Vuo proof nodes have explicit admission and parity levels", () => {
  const expected = [
    {
      file: "my.runtime.clock.mainClock.json",
      nodeId: "my.runtime.clock.mainClock",
      admission: "vuo",
      state: "stateful",
      family: "clock",
      tixl: "not-parity",
      vuo: "host-layer-proof",
      missingCapability: "diagnostic-visible"
    },
    {
      file: "my.image.generate.basic.constantImage.json",
      nodeId: "my.image.generate.basic.constantImage",
      admission: "vuo",
      state: "stateless",
      family: "texture-source",
      tixl: "body-layer-adapter",
      vuo: "body-layer-adapter",
      missingCapability: "diagnostic-visible"
    },
    {
      file: "my.image.use.blend.json",
      nodeId: "my.image.use.blend",
      admission: "vuo",
      state: "stateless",
      family: "texture-filter",
      tixl: "body-layer-adapter",
      vuo: "body-layer-adapter",
      missingCapability: "diagnostic-visible"
    },
    {
      file: "my.image.use.keepPreviousFrame.json",
      nodeId: "my.image.use.keepPreviousFrame",
      admission: "vuo",
      state: "stateful",
      family: "texture-state",
      tixl: "body-layer-adapter",
      vuo: "body-layer-adapter",
      missingCapability: "diagnostic-visible"
    },
    {
      file: "my.image.generate.basic.renderTarget.json",
      nodeId: "my.image.generate.basic.renderTarget",
      admission: "vuo",
      state: "external-state",
      family: "texture-output",
      tixl: "host-layer-proof",
      vuo: "host-layer-proof",
      missingCapability: "diagnostic-visible"
    },
    {
      file: "my.render.dx11.api.clearRenderTarget.json",
      nodeId: "my.render.dx11.api.clearRenderTarget",
      admission: "vuo",
      state: "stateless",
      family: "command",
      tixl: "visual-proof",
      vuo: "body-layer-adapter",
      missingCapability: "diagnostic-visible"
    }
  ];

  for (const spec of expected) {
    const manifest = readManifest(spec.file);
    assert.equal(manifest.nodeId, spec.nodeId);
    assert.equal(manifest.admission, spec.admission);
    assert.equal(manifest.state, spec.state);
    assert.equal(manifest.family, spec.family);
    assert.equal(manifest.parity.tixl, spec.tixl);
    assert.equal(manifest.parity.vuo, spec.vuo);
    assert.equal(manifest.backendPolicy.missingCapability, spec.missingCapability);
    assert.equal(manifest.flow.timeOwner, "my_MainClock");
    assert.equal(manifest.flow.commandOwner, "commandGraph");
    assert.ok(manifest.proof.tests.includes("tests/vuo_high_risk_nodes.test.js"));
    assertRequiredContext(manifest);
  }

  const renderTarget = readManifest("my.image.generate.basic.renderTarget.json");
  assert.ok(renderTarget.proof.nonclaims.includes("tixlDx11RenderTargetParity"));
  const clear = readManifest("my.render.dx11.api.clearRenderTarget.json");
  assert.ok(clear.proof.nonclaims.includes("dx11RtvDsvIdentity"));
});

test("all checked-in admission manifests satisfy local schema invariants", () => {
  const schema = readJson(schemaPath);
  const taxonomy = readJson(failureTaxonomyPath);
  const knownFailureCodes = new Set(taxonomy.codes.map((entry) => entry.code));
  const requiredContext = taxonomy.requiredDiagnosticContext;
  const files = fs.readdirSync(manifestsDir).filter((name) => name.endsWith(".json"));

  assert.ok(files.length >= 3);
  for (const file of files) {
    const manifest = readManifest(file);
    for (const field of schema.required) {
      assert.ok(Object.hasOwn(manifest, field), `${file} missing ${field}`);
    }
    for (const code of manifest.failureCodes) {
      assert.ok(knownFailureCodes.has(code), `${file} uses unknown failure code ${code}`);
    }
    for (const field of requiredContext) {
      assert.ok(manifest.observability.requiredContext.includes(field), `${file} missing observability ${field}`);
    }
    assert.ok(manifest.proof.claims.length > 0, `${file} has no claims`);
    assert.ok(manifest.proof.nonclaims.length > 0, `${file} has no nonclaims`);
    assertMaturityShape(manifest.maturity, file);
    assertMaturityEvidence(manifest.maturity, file);
  }
});

test("checked-in proof manifests expose machine-readable claims and existing evidence", () => {
  const schema = readJson(proofManifestSchemaPath);
  const files = fs.readdirSync(proofManifestsDir).filter((name) => name.endsWith(".json"));

  assert.ok(files.length >= 2);
  for (const file of files) {
    const manifest = readJson(path.join(proofManifestsDir, file));
    for (const field of schema.required) {
      assert.ok(Object.hasOwn(manifest, field), `${file} missing ${field}`);
    }
    assert.ok(manifest.claims.length > 0, `${file} has no claims`);
    assert.ok(manifest.nonclaims.length > 0, `${file} has no nonclaims`);
    for (const field of ["graphId", "frameId", "commandId", "nodeId", "backendId", "artifactPath", "diagnosticCode"]) {
      assert.ok(manifest.observability.requiredContext.includes(field), `${file} missing proof observability ${field}`);
    }
    assertPathExists(manifest.fixture, `${file} fixture`);
    assertPathExists(manifest.script, `${file} script`);
    for (const artifact of manifest.artifacts) {
      assertPathExists(artifact, `${file} artifact`);
    }
    for (const testPath of manifest.tests) {
      assertPathExists(testPath, `${file} test`);
    }
    assert.match(manifest.freshness.verificationCommand, /node --test/);
  }
});

test("Vuo high-risk proof bundle records parity boundaries as machine-readable claims", () => {
  const manifest = readJson(path.join(proofManifestsDir, "vuo_high_risk_nodes.json"));

  assert.equal(manifest.id, "vuo_high_risk_nodes");
  assert.equal(manifest.ownerLane, "vuo-high-risk-proof");
  assert.ok(manifest.claims.includes("mainClockOwnsRenderTick"));
  assert.ok(manifest.claims.includes("renderTargetHostLayerProof"));
  assert.ok(manifest.nonclaims.includes("tixlDx11RenderTargetParity"));
  assert.ok(manifest.nonclaims.includes("directDisplayRefreshPerNode"));
  assert.equal(manifest.fixture, "docs/runtime/fixtures/creator_constant_blend_no_clock.graph.json");
  assert.ok(manifest.artifacts.includes("vuo-compositions/myworld-high-risk-vuo-node-lab.vuo"));
  assert.ok(manifest.artifacts.includes("vuo-compositions/myworld-constant-image-pipeline-proof.vuo"));
  assert.ok(manifest.tests.includes("tests/vuo_high_risk_nodes.test.js"));
  for (const field of ["graphId", "frameId", "commandId", "nodeId", "backendId", "artifactPath", "diagnosticCode"]) {
    assert.ok(manifest.observability.requiredContext.includes(field), `vuo bundle missing ${field}`);
  }
});

test("every checked-in Vuo node has a creator-facing admission index entry", () => {
  const vuoNodeDir = path.join(repoRoot, "vuo-nodes");
  const vuoNodes = fs.readdirSync(vuoNodeDir)
    .filter((name) => name.endsWith(".c"))
    .map((name) => name.replace(/\.c$/, ""))
    .sort();
  const index = readJson(vuoAdmissionIndexPath);
  const indexedNodeIds = new Set(index.entries.map((entry) => entry.nodeId));

  assert.ok(vuoNodes.length > 300, "expected the full checked-in Vuo node surface");
  assert.equal(index.kind, "VuoNodeAdmissionIndex");
  assert.equal(index.nodeCount, vuoNodes.length);
  assert.equal(index.entries.length, vuoNodes.length);
  const missing = vuoNodes.filter((nodeId) => !indexedNodeIds.has(nodeId));

  assert.deepEqual(missing, [], `missing admission index entries for ${missing.length} Vuo nodes`);
  for (const entry of index.entries) {
    assertPathExists(entry.sourcePath, `${entry.nodeId} source`);
    assert.ok(entry.creatorName.startsWith("my_"), `${entry.nodeId} missing creator-facing name`);
    assert.ok(["vuo", "proof-only", "blocked"].includes(entry.admission), `${entry.nodeId} has invalid admission`);
    assert.ok(entry.ports.length > 0, `${entry.nodeId} has no port contract`);
    for (const port of entry.ports) {
      for (const field of ["id", "direction", "type", "eventFlow", "cardinality", "default", "range"]) {
        assert.ok(Object.hasOwn(port, field), `${entry.nodeId}.${port.id} missing port ${field}`);
      }
    }
    assert.ok(["stateless", "stateful", "external-state"].includes(entry.state), `${entry.nodeId} has invalid state`);
    assert.ok(["low", "medium", "high"].includes(entry.riskLevel), `${entry.nodeId} has invalid riskLevel`);
    assert.ok(Array.isArray(entry.riskReasons), `${entry.nodeId} missing risk reasons`);
    assert.equal(typeof entry.requiresFullManifest, "boolean", `${entry.nodeId} missing manifest gate`);
    assert.ok(entry.maturity, `${entry.nodeId} missing maturity`);
    assertMaturityShape(entry.maturity, entry.nodeId);
    assertMaturityEvidence(entry.maturity, entry.nodeId);
    if (entry.maturity.level === "admissionReady") {
      assert.notEqual(entry.maturity.nativeUse, "execute-native", `${entry.nodeId} overclaims native execution`);
    }
    if (entry.maturity.level === "runtimeReady") {
      assert.ok(
        entry.riskReasons.length > 0 || /^docs\/contracts\/node_manifests\//.test(entry.maturity.evidence),
        `${entry.nodeId} runtimeReady needs risk reason or full manifest evidence`
      );
    }
    if (entry.admission === "proof-only") {
      assert.equal(entry.maturity.level, "admissionReady", `${entry.nodeId} proof-only must stay admissionReady`);
      assert.equal(entry.maturity.nativeUse, "known-only", `${entry.nodeId} proof-only must stay known-only`);
    }
    if (entry.riskLevel === "high") {
      assert.equal(entry.requiresFullManifest, true, `${entry.nodeId} high-risk node must require full manifest`);
      assert.ok(entry.manifestPath, `${entry.nodeId} high-risk node missing manifestPath`);
      assertPathExists(entry.manifestPath, `${entry.nodeId} high-risk manifest`);
      if (entry.admission !== "proof-only") {
        assert.notEqual(entry.maturity.level, "admissionReady", `${entry.nodeId} high-risk node stayed admissionReady`);
      }
      if (entry.maturity.level === "runtimeReady") {
        assert.equal(entry.maturity.evidence, entry.manifestPath, `${entry.nodeId} runtimeReady must cite full manifest`);
      }
    }
    assert.ok(["semantic-parity", "visual-proof", "body-layer-adapter", "host-layer-proof", "not-parity"].includes(entry.parity.tixl));
    assert.ok(["semantic-parity", "visual-proof", "body-layer-adapter", "host-layer-proof", "not-parity"].includes(entry.parity.vuo));
    assert.ok(entry.flow.timeOwner, `${entry.nodeId} missing timeOwner`);
    assert.ok(entry.flow.eventOrdering, `${entry.nodeId} missing eventOrdering`);
    assert.ok(entry.backendPolicy.missingCapability, `${entry.nodeId} missing backend degradation policy`);
    assert.ok(entry.failureCodes.includes("backend.capability.missing"), `${entry.nodeId} missing backend failure code`);
    for (const field of ["graphId", "frameId", "commandId", "nodeId", "backendId", "artifactPath", "diagnosticCode"]) {
      assert.ok(entry.observability.requiredContext.includes(field), `${entry.nodeId} missing ${field}`);
    }
    assert.ok(entry.evidence.tests.length > 0, `${entry.nodeId} missing test evidence`);
    for (const testPath of entry.evidence.tests) {
      assertPathExists(testPath, `${entry.nodeId} test`);
    }
  }
});

test("first interaction-contract SDF nodes are classified high-risk and have full manifests", () => {
  const index = readJson(vuoAdmissionIndexPath);
  const byId = new Map(index.entries.map((entry) => [entry.nodeId, entry]));

  for (const nodeId of [
    "my.field.generate.sdf.sphereSdf",
    "my.field.render.raymarchField",
    "my.field.combine.combineSdf"
  ]) {
    const entry = byId.get(nodeId);
    assert.ok(entry, `${nodeId} missing from admission index`);
    assert.equal(entry.riskLevel, "high");
    assert.equal(entry.requiresFullManifest, true);
    assert.match(entry.manifestPath, /^docs\/contracts\/node_manifests\//);
    const manifest = readJson(path.join(repoRoot, entry.manifestPath));
    assert.equal(manifest.nodeId, nodeId);
    assert.equal(manifest.family, "shader-field");
    assert.equal(manifest.flow.commandOwner, "commandGraph");
    assert.ok(manifest.failureCodes.includes("graph.edge.type_mismatch"));
    assert.ok(manifest.proof.nonclaims.length > 0);
    assertRequiredContext(manifest);
  }
});

function readManifest(name) {
  return readJson(path.join(manifestsDir, name));
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function assertRequiredContext(manifest) {
  [
    "graphId",
    "frameId",
    "commandId",
    "nodeId",
    "backendId",
    "artifactPath",
    "diagnosticCode"
  ].forEach((field) => {
    assert.ok(manifest.observability.requiredContext.includes(field), `${manifest.nodeId} missing ${field}`);
  });
}

function assertMaturityShape(maturity, label) {
  assert.ok(
    ["admissionReady", "interactionReady", "runtimeReady", "nativeExecutable"].includes(maturity.level),
    `${label} has invalid maturity level`
  );
  assert.ok(
    ["known-only", "editor-graph", "runtime-graph", "execute-native"].includes(maturity.nativeUse),
    `${label} has invalid nativeUse`
  );
  assert.equal(typeof maturity.evidence, "string", `${label} maturity evidence is not a string`);
  assert.ok(maturity.evidence.length > 0, `${label} maturity evidence is empty`);
  assert.ok(Array.isArray(maturity.unknowns), `${label} maturity unknowns missing`);
  if (maturity.nativeUse === "execute-native") {
    assert.equal(maturity.level, "nativeExecutable", `${label} execute-native must be nativeExecutable`);
  }
  if (maturity.level === "nativeExecutable") {
    assert.equal(maturity.nativeUse, "execute-native", `${label} nativeExecutable must use execute-native`);
  }
}

function assertMaturityEvidence(maturity, label) {
  if (maturity.level === "nativeExecutable") {
    assert.ok(
      /(^|\/)(tests|docs\/runtime\/artifacts)\//.test(maturity.evidence),
      `${label} nativeExecutable needs proof artifact or test evidence`
    );
    assertPathExists(maturity.evidence, `${label} nativeExecutable evidence`);
  }
  if (maturity.level === "runtimeReady" && /^docs\/contracts\/node_manifests\//.test(maturity.evidence)) {
    assertPathExists(maturity.evidence, `${label} runtimeReady manifest evidence`);
  }
}

function assertPathExists(relativePath, label) {
  assert.ok(fs.existsSync(path.join(repoRoot, relativePath)), `${label} missing: ${relativePath}`);
}
