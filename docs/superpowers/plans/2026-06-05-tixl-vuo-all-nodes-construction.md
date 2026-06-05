# TiXL Vuo All Nodes Construction Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the full TiXL node vocabulary into Vuo / My World in batches, with every manufactured node backed by TiXL source evidence, runtime contract proof, and Vuo-visible functional acceptance.

**Architecture:** `docs/tixl-porting/TIXL_TO_VUO_PORTING.md` remains the commander-level construction map; `docs/tixl-porting/PORT_STATUS_BOARD.md` remains the generated built-node ledger. Each batch turns a bounded set of node cards into semantic tests, Vuo C nodes, optional runtime fixtures, Vuo proof compositions, and ledger updates.

**Tech Stack:** TiXL C# / `.t3` source, Node.js test runner, Vuo C custom nodes, `tools/vuo_harness.py`, generated markdown ledgers.

---

## Global Gates

Every batch must use `skills/tixl-vuo-node-port/SKILL.md` and pass these gates:

1. Read the porting construction map and current status board before choosing nodes.
2. Confirm the candidate is not already built or recorded as proof-only.
3. Audit TiXL C# source, `.t3` defaults, docs, output type, and TiXL type color.
4. Write or update semantic tests before implementation.
5. Implement Vuo node with exact visible title `my_<ExactTiXLNodeName>`, TiXL category, source path, primary output type, and color evidence.
6. Install changed `.c` nodes into `~/Library/Application Support/Vuo/Modules/` before Vuo CLI proof.
7. Provide Vuo-visible acceptance for each manufactured node. For pure value nodes, the proof may be a compact visual meter/composition that makes defaults and one control change visible.
8. Run narrow tests plus Vuo compile/link/run proof. Accept only when screenshot exists and `mostlyBlack` is `false`, or record the exact runtime/Vuo blocker.
9. Regenerate `docs/tixl-porting/PORT_STATUS_BOARD.md` using `python3 docs/tixl-porting/scripts/generate_port_status_board.py --write`.

## Batch Order

1. Grade A scalar/control nodes from `docs/tixl-porting/namespaces/numbers.md`.
2. Grade A string/data nodes from `docs/tixl-porting/namespaces/io_flow_string_data.md`.
3. Grade B value/list/vector/color nodes after type-shape mini-contracts exist.
4. Grade C image/SDF/mesh/render nodes only after the required My World runtime contract or proof adapter exists.
5. Grade D nodes stay documented until the missing runtime/device/app capability is explicitly built.

## Runtime Block Rule

If node work exposes a runtime failure, stop expanding that batch and reduce it to:

```text
<graph fixture> -> <runtime component> -> <observable artifact or failure>
```

Classify the break as `contract gap`, `runtime bug`, `Vuo body-layer limit`, or `TiXL mismatch`. Resume node manufacturing only after an observable passing artifact exists, or mark the node blocked with the missing runtime capability.

---

### Task 1: Batch 1 Candidate Audit And Acceptance Matrix

**Files:**
- Modify: `docs/tixl-porting/TIXL_TO_VUO_PORTING.md`
- Modify: `docs/tixl-porting/PORT_STATUS_BOARD.md` only through generator
- Create: `docs/tixl-porting/batches/2026-06-05-batch-1-grade-a-numbers.md`

- [ ] **Step 1: Create the batch directory**

Run:

```bash
mkdir -p docs/tixl-porting/batches
```

Expected: directory exists.

- [ ] **Step 2: Audit 8-12 unbuilt Grade A nodes**

Use these sources in order:

```text
docs/tixl-porting/TIXL_TO_VUO_PORTING.md
docs/tixl-porting/PORT_STATUS_BOARD.md
docs/tixl-porting/namespaces/numbers.md
docs/tixl-porting/reports/source_inventory.md
external/tixl/Operators/Lib
```

Exclude nodes already present in `vuo-nodes/` or `PORT_STATUS_BOARD.md`.

- [ ] **Step 3: Write the batch acceptance matrix**

Create `docs/tixl-porting/batches/2026-06-05-batch-1-grade-a-numbers.md` with one row per candidate:

```markdown
| TiXL node | grade | Vuo title | Vuo source | source evidence | defaults checked | semantic test | Vuo visual proof | status |
|---|---|---|---|---|---|---|---|---|
| Lib.numbers.float.process.SmoothStep | A | my_SmoothStep | vuo-nodes/my.numbers.float.process.smoothStep.c | C# + .t3 + docs | yes | pending | pending | selected |
```

- [ ] **Step 4: Run the status board test**

Run:

```bash
node --test tests/tixl_vuo_port_status_board.test.js
```

Expected: pass before implementation, confirming the existing ledger is readable.

---

### Task 2: Batch 1 Semantic Tests

**Files:**
- Create or modify: `tests/tixl_float_process_semantics.test.js`
- Create or modify: `tests/tixl_float_trigonometry_semantics.test.js`
- Create or modify: `tests/tixl_int_basic_semantics.test.js`

- [ ] **Step 1: Add source-backed semantic cases**

For each selected node, add deterministic cases from TiXL C# behavior and `.t3` defaults. Include at least:

```text
SmoothStep: below Min -> 0, midpoint -> 0.5, above Max -> 1, formula is smootherstep
Sin: radians behavior, not Vuo degree behavior
Cos: radians behavior, not Vuo degree behavior
```

- [ ] **Step 2: Run semantics tests and verify failure if nodes are not yet implemented**

Run:

```bash
node --test tests/tixl_float_process_semantics.test.js tests/tixl_float_trigonometry_semantics.test.js tests/tixl_int_basic_semantics.test.js
```

Expected: semantic reference tests pass once written; source-contract tests may fail until Vuo nodes exist.

---

### Task 3: Batch 1 Vuo C Nodes And Source Contracts

**Files:**
- Create: `vuo-nodes/my.numbers.float.process.smoothStep.c`
- Create additional `vuo-nodes/*.c` files only for nodes selected in Task 1
- Create or modify matching `tests/*_vuo_nodes.test.js`

- [ ] **Step 1: Write failing Vuo source-contract tests**

Each test must assert title, category, TiXL source path, `.t3` defaults, primary output type, type color, ports, output, and core expression.

- [ ] **Step 2: Implement minimal Vuo C node**

Use exact TiXL naming:

```text
visible title: my_<ExactTiXLNodeName>
class path: my.<tixl_category_path>.<lowerCamelNodeName>
```

- [ ] **Step 3: Run source-contract tests**

Run:

```bash
node --test tests/tixl_*_vuo_nodes.test.js
```

Expected: all selected node source-contract tests pass.

---

### Task 4: Batch 1 Vuo Visual Proof

**Files:**
- Create: `vuo-compositions/generated/myworld-batch-1-grade-a-numbers-proof.vuo`
- Create or modify: `tests/vuo_batch_1_grade_a_numbers_composition.test.js`

- [ ] **Step 1: Build a compact visual proof composition**

The composition must make each selected node visible through a value-to-image, text, bar, color, or meter display and include one meaningful control change.

- [ ] **Step 2: Install custom nodes**

Run:

```bash
cp vuo-nodes/*.c "$HOME/Library/Application Support/Vuo/Modules/"
```

Expected: changed nodes are physically installed for Vuo.

- [ ] **Step 3: Run Vuo CLI proof**

Run:

```bash
python3 tools/vuo_harness.py cli-status
python3 tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-1-grade-a-numbers-proof.vuo --seconds 3 --name batch-1-grade-a-numbers
```

Expected: compile/link/run return `0`, selected custom node classes appear in `loadedUserNodes`, screenshot exists, and `mostlyBlack` is `false`.

---

### Task 5: Ledger Update And Batch Close

**Files:**
- Modify: `docs/tixl-porting/PORT_STATUS_BOARD.md`
- Modify: `docs/tixl-porting/batches/2026-06-05-batch-1-grade-a-numbers.md`

- [ ] **Step 1: Regenerate status board**

Run:

```bash
python3 docs/tixl-porting/scripts/generate_port_status_board.py --write
```

Expected: new nodes appear with Vuo node + proof status.

- [ ] **Step 2: Run narrow regression**

Run:

```bash
node --test tests/tixl_vuo_node_port_skill.test.js tests/tixl_vuo_port_status_board.test.js
node --test tests/tixl_float_process_semantics.test.js tests/tixl_float_trigonometry_semantics.test.js tests/tixl_int_basic_semantics.test.js
```

Expected: all pass.

- [ ] **Step 3: Record unresolved blockers**

If any selected node lacks Vuo visual proof, mark it `blocked` in the batch matrix with the exact missing runtime or Vuo body-layer capability. Do not mark it done.
