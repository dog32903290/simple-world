const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const scriptPath = path.join(repoRoot, "docs/tixl-porting/scripts/generate_port_status_board.py");
const boardPath = path.join(repoRoot, "docs/tixl-porting/PORT_STATUS_BOARD.md");
const vuoNodesPath = path.join(repoRoot, "vuo-nodes");
const vuoCompositionsPath = path.join(repoRoot, "vuo-compositions");

function countFiles(dir, predicate) {
  return fs.readdirSync(dir, { withFileTypes: true })
    .filter((entry) => entry.isFile() && predicate(entry.name))
    .length;
}

function countCompositions() {
  const rootCount = countFiles(vuoCompositionsPath, (name) => name.endsWith(".vuo"));
  const generatedPath = path.join(vuoCompositionsPath, "generated");
  const generatedCount = countFiles(generatedPath, (name) => name.endsWith(".vuo"));
  return rootCount + generatedCount;
}

test("TiXL -> Vuo port status board is generated from current repo evidence", () => {
  const run = spawnSync("python3", [scriptPath, "--check"], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const source = fs.readFileSync(boardPath, "utf8");
  const vuoNodeCount = countFiles(vuoNodesPath, (name) => name.endsWith(".c"));
  const compositionCount = countCompositions();

  assert.match(source, /# TiXL -> Vuo Port Status Board/);
  assert.match(source, /TiXL indexed nodes: 935/);
  assert.match(source, new RegExp(`Vuo custom node sources: ${vuoNodeCount}`));
  assert.match(source, new RegExp(`Vuo proof compositions: ${compositionCount}`));
  for (const title of [
    "my_And",
    "my_Or",
    "my_BoolToFloat",
    "my_BoolToInt",
    "my_Not",
    "my_PickBool",
    "my_Xor",
    "my_Abs",
    "my_Ceil",
    "my_Clamp",
    "my_Floor",
    "my_InvertFloat",
    "my_Round",
    "my_Add",
    "my_Div",
    "my_Modulo",
    "my_Multiply",
    "my_Pow",
    "my_Sqrt",
    "my_Sub",
  ]) {
    assert.match(source, new RegExp(`\\| ${title} \\| Operators\\/Lib\\/numbers\\/(?:bool|float)\\/[^|]+ \\| Vuo node \\+ composition proof \\|`));
  }
  assert.match(source, /\| my_SphereSDF \| Operators\/Lib\/field\/generate\/sdf \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_SmoothStep \| Operators\/Lib\/numbers\/float\/process \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_Sin \| Operators\/Lib\/numbers\/float\/trigonometry \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_IntAdd \| Operators\/Lib\/numbers\/int\/basic \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_IsIntEven \| Operators\/Lib\/numbers\/int\/logic \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_SetMaterial \| Operators\/Lib\/render\/shading \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_Batch1GradeANumbersProof \| proof-adapter \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_DrawMeshPbrProof \| proof-adapter \| Vuo node \+ composition proof \|/);
  assert.match(source, /\| my_MainClock \| My World runtime adapter \| Vuo node \+ generated composition proof \|/);
  assert.match(source, /Next batch should start from Grade A value\/control nodes/);
});
