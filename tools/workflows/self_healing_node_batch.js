export const meta = {
  name: 'self-healing-node-batch',
  description: 'Death-resilient TiXL node-op production: per op dispatch implementer→refuter, auto-respawn on agent death, continue to next',
  phases: [
    { title: 'Implement', detail: 'one implementer per op (retry on death)' },
    { title: 'Refute', detail: 'adversarial refuter per landed op (retry on death)' },
  ],
}

// ── 柏為 2026-06-15 的要求：把「自己判斷 agent 死掉 → 再派一個 → 繼續走」做進 workflow ──
// agent() 在 subagent 終端死亡(socket 死/API error 重試耗盡)時回 null。resilient() 包一層
// retry：null=死 → 換一個 prompt(帶 salvage 提示)再派，最多 maxTries 次；全死才放棄回 null。
// 這就是 orchestrator 不再人肉盯死、不再空耗的結構化機制。
async function resilient(basePrompt, opts, label, maxTries) {
  for (let i = 1; i <= maxTries; i++) {
    const salvage = i > 1
      ? `\n\n[RESPAWN ${i}/${maxTries} — 前一個 agent 死了/沒交活。先 \`git status\` 盤主樹：前次死亡可能留下半成品檔，覆寫/補完而非從零；若已大致完成只缺驗證就補驗證。]`
      : ''
    const r = await agent(basePrompt + salvage, { ...opts, label: `${label}#${i}` })
    if (r) return r
    log(`☠ ${label} 第 ${i}/${maxTries} 次 agent 死亡 — 自動換一個再派`)
  }
  log(`✗ ${label} 連死 ${maxTries} 次 — 放棄此 op，繼續下一個`)
  return null
}

const IMPL_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  properties: {
    status: { enum: ['done', 'wrong_base', 'blocked'], description: 'done=全綠交活 / wrong_base=地基錯立停 / blocked=誠實擋下' },
    op: { type: 'string' },
    selftestName: { type: 'string', description: 'CLI selftest 名，如 convertcolors（--selftest-<name>）' },
    filesChanged: { type: 'array', items: { type: 'string' } },
    biteTail: { type: 'string', description: 'run_all_selftests.sh --bite 的尾兩行' },
    forks: { type: 'array', items: { type: 'string' }, description: '每條與 TiXL 的具名 fork' },
    dossier: { type: 'string', description: '完整 dossier（給 refuter + orchestrator）' },
    blockReason: { type: 'string' },
  },
  required: ['status', 'op', 'selftestName', 'dossier'],
}

const REFUTE_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  properties: {
    verdict: { enum: ['SURVIVE', 'BROKEN'] },
    evidence: { type: 'string', description: '逐行/逐點對 .hlsl 的比對 + 手算 case' },
    issues: { type: 'array', items: { type: 'string' } },
  },
  required: ['verdict', 'evidence'],
}

const CONTEXT = '讀 docs/agent/CONTEXT_PACK.md（鐵律+工具+雷區+dossier 規格）。'

function implPrompt(o) {
  return `${CONTEXT}然後實作一顆 ${o.kind} op：**${o.op}**。**直接在主 checkout 工作（非 worktree）**。

第零步（WRONG BASE 立停）：\`git branch --show-current\` == \`codex/js-to-cpp-contract-migration\` 且 \`ls ${o.baseProbe}\` 存在。任一失敗→status="wrong_base" 立停回報。不跑 agent_worktree_setup.sh。build \`cmake --build app/build\`。

模板 = \`${o.template}\`（照它的結構：cook + register + golden selftest 同檔）。
TiXL 權威（先逐行讀）：.cs=\`${o.tixlCs}\`；kernel=\`${o.tixlHlsl}\`（逐行港）；預設查對應 .t3（別猜，自查）。

任務承重：${o.notes}

護欄（逐字）：查 TiXL 不發明 port（port 順序照 .cs [Input] 序，append 不 insert）；逐行對 .hlsl，position/像素值都對；每條 fork 具名（誰/為何/權威源碼行）；**不 commit**（合流歸 orchestrator）。

產出：${o.outputs} + NodeSpec（鏡模板，port 對齊 .cs [Input] 序）+ 共享檔直接加並回報：\`point_ops.h\` 宣告 + \`selftests.cpp\` kTable \`{"${o.selftest}", run${o.fn}SelfTest},\`。

golden（headless 真斷言）：${o.golden}；injectBug 翻一個真邏輯 → 整套 FAIL(rc=1)。

自驗：\`cmake --build app/build\` → \`tools/run_all_selftests.sh --bite\`（FAILED 不含你的顆、NO-BITE:[]；注意 soundtrack 是已知 AVAudio 環境 flake 可忽略）→ \`tools/check_arch.sh\` 綠 → \`--selftest-${o.selftest}\`(綠) + injectBug(紅 rc=1)。

回報用 StructuredOutput：status / op="${o.op}" / selftestName="${o.selftest}" / filesChanged / biteTail（--bite 尾兩行）/ forks / dossier（完整：改動點+TiXL 依據含 .hlsl 引述+fork 具名+牙清單+疑慮盲區+改動檔清單）。`
}

function refutePrompt(o, impl) {
  return `${CONTEXT}你是對抗性 refuter。剛落地 op **${o.op}**（未 commit，主 checkout）。否證它與 TiXL 的 parity——預設它錯，找它哪裡破，不是複述它對。

我方檔：${(impl.filesChanged || []).join('、') || o.outputs}。
TiXL 權威：.cs=\`${o.tixlCs}\`；kernel=\`${o.tixlHlsl}\`（逐行）。
implementer 自報的 fork（重點攻擊這些）：${(impl.forks || []).join(' / ') || '(無)'}。
攻擊面：${o.refuteFocus}

實證（不要嘴上推理）：逐行/逐點比對 .metal/.cpp vs .hlsl/.cs；手算至少一個非平凡 case 對實作邏輯（可暫加 /tmp 驗證腳本跑 \`./app/build/simple_world --selftest-${o.selftest}\`，**跑完還原不留改動**）。

裁決：SURVIVE（逐行對齊+手算一致，附證據）或 BROKEN（指出具體哪行/哪值錯+TiXL 行+正確 vs 實際）。預設 refuted，除非拿出逐行比對+手算。**不留任何改動**。回報 StructuredOutput：verdict / evidence / issues。`
}

// Robustness: args may arrive as a real array (correct) OR — if the caller stringified
// it — as a JSON string. Without this guard, `for...of` over a string iterates CHARACTERS,
// spawning one garbage "undefined" work order per char (2026-06-15: 55 wasted wrong_base
// agents before the bug was caught). Parse strings; refuse anything that isn't an op array.
let ops = args
if (typeof ops === 'string') { try { ops = JSON.parse(ops) } catch (e) { ops = [] } }
if (!Array.isArray(ops)) ops = []
const valid = ops.filter(o => o && typeof o === 'object' && typeof o.op === 'string')
if (valid.length !== ops.length) log(`⚠ ${ops.length - valid.length} bad op entr(ies) dropped — each op needs {op, ...}`)
ops = valid
if (!ops.length) { log('no valid ops in args — nothing to do (pass args as a real JSON array of op specs)'); return [] }

const results = []
for (const o of ops) {
  log(`▶ ${o.op}: 派 implementer（死了自動換）`)
  const impl = await resilient(implPrompt(o), { schema: IMPL_SCHEMA, phase: 'Implement' }, `impl:${o.op}`, 3)
  if (!impl) { results.push({ op: o.op, impl: null, verdict: null, note: 'implementer 連死 3 次' }); continue }
  if (impl.status !== 'done') { results.push({ op: o.op, impl, verdict: null, note: `implementer status=${impl.status}` }); continue }
  log(`✔ ${o.op} 落地（${impl.selftestName}）→ 派 refuter`)
  const verdict = await resilient(refutePrompt(o, impl), { schema: REFUTE_SCHEMA, phase: 'Refute' }, `refute:${o.op}`, 2)
  results.push({ op: o.op, impl, verdict })
  log(`◆ ${o.op}: impl=done refuter=${verdict ? verdict.verdict : 'DIED(放行給 orchestrator 親核)'}`)
}
return results
