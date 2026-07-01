// runtime/resident_eval_flatten — the FLATTENER half of the resident engine: inline a nested
// SymbolLibrary into the resident eval graph (inlineSymbol + buildEvalGraph). Split from
// resident_eval_graph.cpp along the two-job seam (ARCHITECTURE rule 4): that file keeps the
// EVAL/RESOLVE engine (evalResidentFloat / resolveResidentFloatInputs / sampleAutomation);
// this one owns BUILD-TIME wiring — driver projection (Constant/Connection/Automation, incl.
// the B2 compound-input animator projection), the 修C bypassed-compound redirect, and the B1
// dataflow ordering of compound children. runtime leaf: resident_eval_graph.h + graph.h only.
#include "runtime/resident_eval_graph.h"

#include "runtime/graph.h"  // NodeSpec / findSpec / animGroupForSlot (Vec anim grouping)

namespace sw {

namespace {

// A symbol/child's output producers: outputDefId -> the DRIVER a consumer of that output copies.
// A real inner producer is a Connection (srcNodePath, srcSlotId); a BYPASSED compound child's
// main output aliases whatever drives its main input (修C) — which can be Constant / Automation /
// Connection — so the producer currency is the full ResidentInput, not a (path, slot) pair.
using ProducerMap = std::map<std::string, ResidentInput>;

// Inline one symbol's subgraph at `prefix`, given how its OWN input defs are driven from the
// outer graph (`inBindings`: inputDefId -> the resolved driver to copy onto inner consumers).
// Appends resident nodes to g, returns this symbol's output producers (ProducerMap above).
// `onPath` carries the symbol ids active on the current path for the self-nesting guard (TiXL
// does NOT guard this — contract S14). depth bounds runaway recursion.
ProducerMap inlineSymbol(
    const SymbolLibrary& lib, const Symbol& sym, const std::string& prefix,
    const std::map<std::string, ResidentInput>& inBindings, ResidentEvalGraph& g,
    std::vector<std::string>& onPath, int depth) {

  ProducerMap outProducers;
  if (depth > 64) return outProducers;

  // Per-child output producers for COMPOUND children (filled by recursion — or, for a bypassed
  // compound child, by the 修C redirect), keyed by child id.
  std::map<int, ProducerMap> childOuts;

  // Resolve "sibling child srcChild's output srcSlot" to the driver a consumer copies. A compound
  // sibling resolves through its ProducerMap (for a BYPASSED compound that map aliases the main
  // input's own driver — the 修C rewire); an atomic sibling is a Connection to its resident path.
  // A compound sibling slot with NO producer entry (secondary output of a bypassed compound,
  // unwired boundary output, skipped self-nest) falls back to the same dangling Connection the
  // old (path, slot)-pair fallback produced — consumers evaluate it as 0 / null buffer.
  auto producerOf = [&](int childId, const std::string& slot) -> ResidentInput {
    auto cit = childOuts.find(childId);
    if (cit != childOuts.end()) {
      auto pit = cit->second.find(slot);
      if (pit != cit->second.end()) return pit->second;
    }
    ResidentInput r;
    r.driver = ResidentInput::Driver::Connection;
    r.srcNodePath = prefix + std::to_string(childId);
    r.srcSlotId = slot;
    return r;
  };

  // 1. Emit ATOMIC children as resident nodes; COMPOUND children are deferred to step 1b and
  //    processed in DATAFLOW order, not declaration order (B1 批次9): a consumer compound declared
  //    before its producer compound would call producerOf() before childOuts has the producer's
  //    entry, and the fallback Connection then points at the COMPOUND'S OWN path — a ghost (no
  //    resident node ever lives there; a compound's children inline UNDER "<id>/...") — so the
  //    whole consumer subtree read 0 / null bag. Atomic siblings are immune: the fallback path IS
  //    the atomic's resident path, deterministic before the node is even emitted.
  std::vector<const SymbolChild*> compoundKids;
  for (const SymbolChild& c : sym.children) {
    const Symbol* def = lib.find(c.symbolId);
    if (!def) continue;
    if (!def->atomic) { compoundKids.push_back(&c); continue; }
    {
      ResidentNode rn;
      rn.path = prefix + std::to_string(c.id);
      rn.opType = c.symbolId;
      // S2 bypass: an atomic child whose MAIN I/O type is bypassable (childIsBypassable) and that is
      // marked isBypassed has its MAIN output redirected to its MAIN input's upstream at eval time
      // (= TiXL ByPassUpdate). Record the main-slot ids now; the redirect itself lives in pull/eval.
      // Whitelist/refusal mirrors SetBypassed — a non-bypassable type leaves bypassed=false (silent
      // no-op, like TiXL's IsBypassable() guard). Output-connectedness is enforced by the COMMAND
      // layer (SetBypassChildCommand) before the flag is ever set, so the builder trusts the flag.
      if (c.isBypassed && childIsBypassable(lib, c)) {
        rn.bypassed = true;
        rn.bypassInSlot = def->inputDefs[0].id;
        rn.bypassOutSlot = def->outputDefs[0].id;
      }
      // S2 per-output isDisabled / triggerOverride: project the child's sparse maps onto the resident
      // node (the cache reads them in initResidentNodeCache). Only outputs the referenced symbol
      // actually defines are carried (a stale slot id would be ignored downstream, but filter here
      // for cleanliness). Only Always matters for the LIVE-source derivation; None/Animated leave the
      // default chase (Animated arrives via the Automation driver path, not a per-output trigger here).
      for (const auto& kv : c.disabledOutputs)
        if (kv.second) rn.disabledOut[kv.first] = true;
      for (const auto& kv : c.triggerOverrides)
        if (kv.second == TriggerOverride::Always) rn.triggerAlwaysOut[kv.first] = true;
      const NodeSpec* spec = findSpec(c.symbolId);  // anim-group lookup (Vec multi-channel)
      for (const SlotDef& d : def->inputDefs) {
        // String sub-seam: a String input carries BOTH a resolved-const fallback (strInputs) AND a
        // driver slot (the resident string-wire rail, task_32b5b6e5). The override-else-strDef const
        // lives in strInputs for the UNWIRED case (byte-identical to the pre-rail behaviour — every
        // existing unwired String port still resolves its const). ALSO project a Constant-driver
        // ResidentInput so the wire resolvers (loops 2/3 below) can OVERWRITE it to Driver::Connection
        // when a String wire feeds this slot — exactly as they do for a Float slot (the projection is
        // dataType-agnostic; MultiInput like CombineStrings.Input rides extraConns the same way). The
        // Float-only resolvers (resolveResidentFloatInputs / evalResidentFloat) iterate by Float/
        // FloatList dataType, so they NEVER read this String ResidentInput — additive/inert to them.
        // The string cook (cookResidentString) reads the driver here; an unwired String slot stays
        // Driver::Constant → the cook falls back to strInputs[d.id].
        if (d.dataType == "String") {
          rn.strInputs[d.id] = effectiveStrInput(lib, c, d.id, d.strDef);
          ResidentInput in;
          in.slotId = d.id;
          in.driver = ResidentInput::Driver::Constant;  // wire-OR-const: loops 2/3 upgrade if wired
          rn.inputs.push_back(in);
          continue;
        }
        ResidentInput in;
        in.slotId = d.id;
        // S3: an Automation driver projects from the PARENT symbol's Animator (the def owning this
        // child). The constant is still the KEPT fallback a future un-animate restores (= TiXL reads
        // the default live; same kept-fallback rule as patchLibSetDefault). If not animated, plain
        // Constant. A wire feeding this slot overwrites the driver below (loop 2/3) — automation and
        // a connection don't coexist on one input (single-cardinality), connection wins on a clash.
        //
        // Vec multi-channel (批次8): the Animator keys a vector param's curves under the group
        // HEAD's slot id (= TiXL one inputId -> Curve[N]); each component slot resolves its OWN
        // channel index. animGroupForSlot is the SAME grouping the Inspector gesture uses (同源,
        // graph.h) — a scalar resolves to {d.id, 0, 1}, identical to the pre-Vec projection.
        in.constant = effectiveInput(lib, c, d.id, d.def);
        const AnimGroup ag = spec ? animGroupForSlot(*spec, d.id) : AnimGroup{d.id, 0, 1};
        if (sym.animator.isAnimated(c.id, ag.headId)) {
          in.driver = ResidentInput::Driver::Automation;
          in.curveRef = Animator::makeRef(c.id, ag.headId, ag.channel);
          in.animSymbolId = sym.id;
        } else {
          in.driver = ResidentInput::Driver::Constant;
        }
        rn.inputs.push_back(in);
      }
      g.byPath[rn.path] = (int)g.nodes.size();
      g.nodes.push_back(std::move(rn));
    }
  }

  // 1b. Order the deferred compound children along compound->compound wires (Kahn topological
  //     sort: every producer before its consumers — bypassed compounds included, since their 修C
  //     redirect both CONSUMES childIn and IS consumed through producerOf the same way). A wire
  //     cycle among compounds leaves its members unsorted -> appended in declaration order, which
  //     degrades to the old dangling fallback for the back edge only (the existing cycle-guard
  //     domain: eval's depth cap owns the runtime side; Kahn terminates on any graph, so no new
  //     infinite loop).
  std::vector<int> compoundOrder;
  {
    std::map<int, int> byChildId;  // child id -> index into compoundKids
    for (size_t i = 0; i < compoundKids.size(); ++i) byChildId[compoundKids[i]->id] = (int)i;
    std::vector<std::vector<int>> consumers(compoundKids.size());
    std::vector<int> pending(compoundKids.size(), 0);  // count of unprocessed producers
    for (const SymbolConnection& w : sym.connections) {
      if (w.srcChild == w.dstChild) continue;  // self-wire = cycle by definition -> leftover path
      auto si = byChildId.find(w.srcChild), di = byChildId.find(w.dstChild);
      if (si == byChildId.end() || di == byChildId.end()) continue;
      consumers[si->second].push_back(di->second);
      pending[di->second]++;
    }
    for (size_t i = 0; i < compoundKids.size(); ++i)
      if (pending[i] == 0) compoundOrder.push_back((int)i);
    for (size_t h = 0; h < compoundOrder.size(); ++h)
      for (int v : consumers[compoundOrder[h]])
        if (--pending[v] == 0) compoundOrder.push_back(v);
    for (size_t i = 0; i < compoundKids.size(); ++i)  // cycle leftovers, declaration order
      if (pending[i] > 0) compoundOrder.push_back((int)i);
  }

  for (int oi : compoundOrder) {
    const SymbolChild& c = *compoundKids[oi];
    const Symbol* def = lib.find(c.symbolId);  // non-null: filtered when deferred
    {
      // Gather how THIS compound child's input defs are driven by the current symbol's wires
      // (ahead of the guards: the 修C bypass redirect below consumes childIn too).
      // Seed every compound input def with its effective Constant driver (override else def),
      // so boundary-INPUT consumers that are NOT wire-driven still receive the value. The wire
      // loop below overwrites the slots that ARE wired. (Mirrors the atomic branch's effectiveInput
      // seeding; refuter Finding 1: without this, override/default-driven compound inputs are dropped.)
      std::map<std::string, ResidentInput> childIn;
      for (const SlotDef& d : def->inputDefs) {
        ResidentInput ci;
        ci.slotId = d.id;
        ci.driver = ResidentInput::Driver::Constant;
        ci.constant = effectiveInput(lib, c, d.id, d.def);
        // B2 (批次9): a compound input def is animatable too — the GUI's Animate has no atomic
        // gate, and TiXL ByPassUpdate pulls Inputs[0].GetValue, which evaluates the animation
        // (Slot.cs:176-179) — so mirror the atomic branch's projection: query the PARENT symbol's
        // Animator. Compounds have no NodeSpec, so the anim group is the scalar identity (= the
        // atomic branch's spec==nullptr fallback; compound SlotDefs are scalar today). Both
        // downstream arms already carry Automation end-to-end (修C): the bypass redirect samples
        // it, and the inBindings copy projects it onto inner consumers. The constant stays the
        // KEPT fallback. A wire on the same slot overwrites below (connection wins on a clash).
        if (sym.animator.isAnimated(c.id, d.id)) {
          ci.driver = ResidentInput::Driver::Automation;
          ci.curveRef = Animator::makeRef(c.id, d.id, 0);
          ci.animSymbolId = sym.id;
        }
        childIn[d.id] = ci;
      }
      for (const SymbolConnection& w : sym.connections) {
        if (w.dstChild != c.id) continue;  // wires feeding this compound child's inputs
        ResidentInput in;
        in.slotId = w.dstSlot;
        if (sourceIsSymbolInput(w)) {
          // source = parent's own input def -> copy the driver the outer graph gave us.
          auto bit = inBindings.find(w.srcSlot);
          if (bit != inBindings.end()) { in = bit->second; in.slotId = w.dstSlot; }
        } else {
          // source is a sibling child: resolve to its producer driver (atomic Connection /
          // compound ProducerMap / bypassed-compound redirect — producerOf owns all three).
          in = producerOf(w.srcChild, w.srcSlot);
          in.slotId = w.dstSlot;
        }
        childIn[w.dstSlot] = in;
      }

      // 修C (批次9) cook-level COMPOUND bypass = TiXL SetBypassFor on a composition Instance
      // (Instance.Connections.cs:265-267: Outputs[0].TrySetBypassToInput(Inputs[0]); Slot.cs:176-179
      // ByPassUpdate: Value = the main INPUT's GetValue — the subgraph behind Outputs[0] is never
      // pulled). Flattened analog: the child's main output PRODUCER becomes whatever drives its
      // main input — a rewire-level passthrough. The subgraph is NOT inlined: zero resident
      // footprint = zero cook, zero per-path state (= TiXL never pulling the inner ops).
      // childIsBypassable guarantees inputDefs[0]/outputDefs[0] exist + main I/O types match the
      // whitelist; an unwired main input stays the Constant seed (Float passes the value through =
      // TiXL input default; buffer-shaped types cook null/empty — same as any unwired input).
      // NAMED FORK: TiXL redirects ONLY Outputs[0]; its secondary outputs still pull the live
      // subgraph. With zero footprint our secondary outputs go DANGLING (cook 0 / null) — accepted
      // until a multi-output compound bypass user appears (no production op family needs it yet).
      if (c.isBypassed && childIsBypassable(lib, c)) {
        ProducerMap red;
        red[def->outputDefs[0].id] = childIn[def->inputDefs[0].id];
        childOuts[c.id] = std::move(red);
        continue;
      }

      // self-nesting / cycle guard: skip if this symbol id is already active on the path.
      bool nested = false;
      for (const std::string& id : onPath)
        if (id == c.symbolId) { nested = true; break; }
      if (nested) continue;

      onPath.push_back(c.symbolId);
      childOuts[c.id] = inlineSymbol(lib, *def, prefix + std::to_string(c.id) + "/", childIn, g,
                                     onPath, depth + 1);
      onPath.pop_back();
    }
  }

  // 2. Resolve this symbol's wires onto atomic dst inputs + collect this symbol's output
  //    producers, in a SINGLE pass over sym.connections (骨7b). producerOf is the ONE
  //    source resolver (atomic Connection / compound ProducerMap / 修C bypassed-compound redirect).
  //
  //    ★骨7b ORDER FIX: this used to be TWO passes — a child→child pass (old loop 2) that ran
  //    ENTIRELY BEFORE a boundary→child pass (old loop 3), both appending to the SAME slot's
  //    extraConns. A MultiInput slot fed by BOTH a child wire and a boundary wire therefore always
  //    ended up ordered [all child…, all boundary…], regardless of the real wire-declaration order
  //    in sym.connections. Downstream the marshal cook (point_graph_resident_buffer.cpp:145-147)
  //    packs floatInputs positionally as [primary, extraConns…] straight into a constant buffer, so a
  //    scrambled extraConns = a scrambled constant buffer = wrong compute-shader output. The entire
  //    mesh/modify + mesh/draw Lib family (DisplaceMesh / DeformMesh / DrawMesh …, ~224 production
  //    compounds) has such mixed slots. Folding both source kinds into ONE pass makes extraConns
  //    honour the true declaration order (refuter 骨7 Finding3, CONFIRMED).
  //
  //    Boundary-only + single-child behaviour is unchanged: a boundary wire still copies inBindings,
  //    a child wire still copies producerOf, and a non-Connection / non-multi slot still whole-
  //    replaces the primary exactly as before (single-cardinality inputs byte-identical).
  for (const SymbolConnection& w : sym.connections) {
    if (targetIsSymbolOutput(w)) {                 // child -> this symbol's external output
      if (sourceIsSymbolInput(w)) continue;        // pass-through input->output (rare); skip in slice 1
      outProducers[w.dstSlot] = producerOf(w.srcChild, w.srcSlot);
      continue;
    }
    // dst is a child. Only ATOMIC dst gets a resident input here (compound dst handled via childIn).
    std::string dstPath = prefix + std::to_string(w.dstChild);
    auto it = g.byPath.find(dstPath);
    if (it == g.byPath.end()) continue;            // dst is compound (driven via childIn) -> skip

    // Resolve the driver to apply by SOURCE kind, but apply it through the SAME slot-append logic
    // for both kinds — that shared logic is what preserves the declaration order across mixed slots.
    ResidentInput pr;
    if (sourceIsSymbolInput(w)) {
      // boundary-input: copy the outer driver the parent gave us (old loop 3). A boundary input
      // with no binding (unwired parent slot) drops, exactly as the old loop-3 miss did.
      auto bit = inBindings.find(w.srcSlot);
      if (bit == inBindings.end()) continue;
      pr = bit->second;
    } else {
      // child -> child: resolve to the sibling's producer driver (old loop 2).
      pr = producerOf(w.srcChild, w.srcSlot);
    }
    pr.slotId = w.dstSlot;

    // Is the dst slot a MultiInput port? (批次25 seam) — then a 2nd+ Connection wire APPENDS to the
    // slot's extraConns instead of overwriting the primary, so eval can expand all N into in[].
    bool dstMulti = false;
    if (const NodeSpec* dspec = findSpec(g.nodes[it->second].opType))
      for (const PortSpec& p : dspec->ports)
        if (p.id == w.dstSlot) { dstMulti = p.multiInput; break; }
    for (ResidentInput& in : g.nodes[it->second].inputs)
      if (in.slotId == w.dstSlot) {
        if (dstMulti && in.driver == ResidentInput::Driver::Connection &&
            pr.driver == ResidentInput::Driver::Connection) {
          // primary already set by an earlier wire (child OR boundary) → this wire is an extra
          // MultiInput source. Appended in the order sym.connections lists it = true declaration
          // order (the 骨7b fix — no longer "all child then all boundary").
          in.extraConns.emplace_back(pr.srcNodePath, pr.srcSlotId);
        } else {
          // A Connection keeps the input's KEPT Constant fallback (in.constant survives under a wire
          // — patchRemoveConnection restores it); a Constant/Automation redirect replaces it whole.
          if (pr.driver == ResidentInput::Driver::Connection) pr.constant = in.constant;
          in = pr;  // first wire (or non-multi / non-Connection) sets the primary driver
        }
      }
  }

  return outProducers;
}

}  // namespace

namespace {
// 骨7 top-level boundary injection: materialize a synthetic resident `Const` producer node holding
// `value` (evaluated via evalResidentFloat("out")), keyed by a "$in/" path that cannot collide with a
// real child id chain. Returns the ResidentInput a boundary consumer copies (a Connection to it).
ResidentInput injectBoundaryConst(ResidentEvalGraph& g, const std::string& path, float value) {
  ResidentNode rn;
  rn.path = path;
  rn.opType = "Const";
  ResidentInput vin;
  vin.slotId = "value";
  vin.driver = ResidentInput::Driver::Constant;
  vin.constant = value;
  rn.inputs.push_back(vin);
  g.byPath[rn.path] = (int)g.nodes.size();
  g.nodes.push_back(std::move(rn));
  ResidentInput out;
  out.driver = ResidentInput::Driver::Connection;
  out.srcNodePath = path;
  out.srcSlotId = "out";
  return out;
}
}  // namespace

ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId,
                                 const std::map<std::string, std::vector<float>>& boundaryFloatInputs) {
  ResidentEvalGraph g;
  const Symbol* root = lib.find(rootId);
  if (!root) return g;
  std::vector<std::string> onPath = {rootId};
  // 骨7 boundary injection: seed the root's boundary INPUT bindings from the caller-supplied values.
  // Each root inputDef becomes ONE inBinding — a Connection to a synthetic Const producer. A boundary
  // input feeding a MultiInput consumer through several DIFFERENT inputDefs rides loop-3 append; a
  // single inputDef carrying several values (rare) materializes several Consts and the extra values
  // ride the binding's extraConns (so a boundary input wired to a MultiInput slot expands to N).
  std::map<std::string, ResidentInput> rootBindings;
  for (const auto& [defId, values] : boundaryFloatInputs) {
    if (values.empty()) continue;
    ResidentInput b = injectBoundaryConst(g, "$in/" + defId + "#0", values[0]);
    for (size_t i = 1; i < values.size(); ++i) {
      ResidentInput extra = injectBoundaryConst(g, "$in/" + defId + "#" + std::to_string(i), values[i]);
      b.extraConns.emplace_back(extra.srcNodePath, extra.srcSlotId);
    }
    rootBindings[defId] = b;
  }
  // The public outputs map stays (path, slot)-shaped; only Connection producers are expressible
  // there (a root output whose producer is a bypassed compound's Constant redirect is dropped —
  // same expressiveness gap as the input->output passthrough skip, named in inlineSymbol).
  for (const auto& [outId, ri] : inlineSymbol(lib, *root, "", rootBindings, g, onPath, 0))
    if (ri.driver == ResidentInput::Driver::Connection)
      g.outputs[outId] = {ri.srcNodePath, ri.srcSlotId};
  return g;
}

ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId) {
  return buildEvalGraph(lib, rootId, {});  // no boundary injection — the plain flatten
}

}  // namespace sw
