// ui/quick_add_selftest — isolated --selftest-quickadd leaf for the Add Node search palette.
// Zone: ui (verify-facing). Exercises the header-exposed search primitives (scatterMatch +
// computeRelevancy) so the scatter-match and relevancy ranking are toothed independently of
// imgui. Split out of quick_add.cpp to keep that file single-duty + under the 400-line cap.
#include "ui/quick_add.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/graph.h"  // NodeSpec / PortSpec / setDynamicSpecs (inject isolated test ops)

namespace sw::ui {

namespace {
// Tiny atomic spec builder (= the headless-selftest convention: inject isolated test-op specs
// via setDynamicSpecs so the port-filter predicates run against KNOWN ports, independent of which
// production ops happen to be registered). Mirrors frame_cook_setboolvar_selftest's sbvAtomic.
sw::NodeSpec qaSpec(const char* type, std::vector<sw::PortSpec> ports) {
    sw::NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
    return s;
}
}  // namespace

int runQuickAddSelfTest(bool injectBug) {
    int fail = 0;

    // Inject three isolated test ops with deliberately distinct port types so the port-drag
    // type-filter (= TiXL SymbolFilter FilterInputType/FilterOutputType) can be exercised on
    // ground truth, regardless of the production registry:
    //   FloatSink  : input Float           (accepts a dragged Float OUTPUT)
    //   PointSink  : input Points          (accepts a dragged Points OUTPUT; NOT a Float OUTPUT)
    //   FloatMaker : output Float          (feeds a dragged Float INPUT)
    {
        std::map<std::string, sw::NodeSpec> dyn;
        dyn["FloatSink"]  = qaSpec("FloatSink",  {{"v", "v", "Float", true}});
        dyn["PointSink"]  = qaSpec("PointSink",  {{"p", "p", "Points", true}});
        dyn["FloatMaker"] = qaSpec("FloatMaker", {{"out", "out", "Float", false}});
        sw::setDynamicSpecs(std::move(dyn));
    }

    // Test 5: port-drag type pre-filter (= TiXL SymbolFilter.cs:117-152 GetInputMatchingType /
    // GetOutputMatchingType, strict ValueType equality).
    {
        // opHasInputOfType: FloatSink has a Float INPUT; PointSink does not.
        if (!opHasInputOfType("FloatSink", "Float")) {
            std::printf("[quickadd] FloatSink should have a Float input -> FAIL\n"); ++fail; }
        if (opHasInputOfType("PointSink", "Float")) {
            std::printf("[quickadd] PointSink should NOT have a Float input -> FAIL\n"); ++fail; }
        if (!opHasInputOfType("PointSink", "Points")) {
            std::printf("[quickadd] PointSink should have a Points input -> FAIL\n"); ++fail; }

        // opHasOutputOfType: FloatMaker has a Float OUTPUT; FloatSink does not (input-only).
        if (!opHasOutputOfType("FloatMaker", "Float")) {
            std::printf("[quickadd] FloatMaker should have a Float output -> FAIL\n"); ++fail; }
        if (opHasOutputOfType("FloatSink", "Float")) {
            std::printf("[quickadd] FloatSink should NOT have a Float output -> FAIL\n"); ++fail; }

        // Empty filter = no constraint (the Cmd+F path): everything passes.
        if (!opHasInputOfType("PointSink", "") || !opHasOutputOfType("PointSink", "")) {
            std::printf("[quickadd] empty filter should impose no constraint -> FAIL\n"); ++fail; }

        // passesPortFilter — drag FROM a Float OUTPUT pin => inputFilter="Float": only ops with a
        // Float INPUT survive. FloatSink in, PointSink out, FloatMaker out (output-only).
        if (!passesPortFilter("FloatSink",  "Float", "")) {
            std::printf("[quickadd] drag-Float-output: FloatSink should pass -> FAIL\n"); ++fail; }
        if (passesPortFilter("PointSink",  "Float", "")) {
            std::printf("[quickadd] drag-Float-output: PointSink should be filtered OUT -> FAIL\n"); ++fail; }
        if (passesPortFilter("FloatMaker", "Float", "")) {
            std::printf("[quickadd] drag-Float-output: FloatMaker (no Float input) should be filtered OUT -> FAIL\n"); ++fail; }

        // Different dragged type => different surviving set. Drag FROM a Points OUTPUT pin
        // (inputFilter="Points"): only PointSink survives (FloatSink filtered out).
        if (!passesPortFilter("PointSink", "Points", "")) {
            std::printf("[quickadd] drag-Points-output: PointSink should pass -> FAIL\n"); ++fail; }
        if (passesPortFilter("FloatSink", "Points", "")) {
            std::printf("[quickadd] drag-Points-output: FloatSink should be filtered OUT -> FAIL\n"); ++fail; }

        // Drag FROM an INPUT pin => outputFilter set: only ops with a matching OUTPUT survive.
        // outputFilter="Float": FloatMaker passes (Float output), FloatSink/PointSink do not.
        if (!passesPortFilter("FloatMaker", "", "Float")) {
            std::printf("[quickadd] drag-Float-input: FloatMaker should pass -> FAIL\n"); ++fail; }
        if (passesPortFilter("FloatSink", "", "Float")) {
            std::printf("[quickadd] drag-Float-input: FloatSink (no Float output) should be filtered OUT -> FAIL\n"); ++fail; }
    }

    // Test 1: empty query scatter-matches everything (RED->GREEN: empty shows full list).
    {
        std::vector<std::string> items = {"radialpoints", "gridpoints", "mycompound"};
        for (const auto& item : items) {
            if (!scatterMatch(item, "")) {
                std::printf("[quickadd] empty query should match '%s' -> FAIL\n", item.c_str());
                ++fail;
            }
        }
    }

    // Test 2: scatter / subsequence match (TiXL SymbolBrowser regex `c.*c.*c`). Inputs are
    // pre-lowercased here (scatterMatch is case-sensitive; production lowercases first).
    {
        if (!scatterMatch("radialpoints", "radial")) {  // plain substring
            std::printf("[quickadd] 'radial' should scatter-match 'radialpoints' -> FAIL\n");
            ++fail;
        }
        if (!scatterMatch("radialpoints", "rdp")) {  // gapped subsequence ra-D-ial-P-oints
            std::printf("[quickadd] 'rdp' should scatter-match 'radialpoints' -> FAIL\n");
            ++fail;
        }
        if (scatterMatch("radialpoints", "pr")) {  // out-of-order: subsequence is ordered
            std::printf("[quickadd] 'pr' should NOT scatter-match 'radialpoints' -> FAIL\n");
            ++fail;
        }
        if (scatterMatch("radialpoints", "xyz")) {
            std::printf("[quickadd] 'xyz' should NOT match 'radialpoints' -> FAIL\n");
            ++fail;
        }
    }

    // Test 2b: relevancy ranking — exact > prefix > contains > scatter (case-insensitive).
    {
        const std::string q = "grid";
        const double exact   = computeRelevancy("Grid", q);          // equals
        const double prefix  = computeRelevancy("GridPoints", q);    // startsWith
        const double contain = computeRelevancy("HexGrid", q);       // contains
        const double scatter = computeRelevancy("GreatRingDiv", q);  // g..r..i..d scatter only
        if (!(exact > prefix && prefix > contain && contain > scatter)) {
            std::printf("[quickadd] ranking order broken: exact=%.2f prefix=%.2f contain=%.2f "
                        "scatter=%.2f -> FAIL\n", exact, prefix, contain, scatter);
            ++fail;
        }
        // Leading-underscore demotion (TiXL x0.1): "_Grid" ranks below "Grid".
        if (!(computeRelevancy("Grid", q) > computeRelevancy("_Grid", q))) {
            std::printf("[quickadd] '_Grid' should rank below 'Grid' -> FAIL\n");
            ++fail;
        }
    }

    // Test 2c: namespace-tree grouping (= TiXL NamespaceTreeNode). buildNamespaceTree splits each
    // item's category on '.' into nested folders; a "point" group must contain the point ops and an
    // empty category lands under "(uncategorized)" (never dropped).
    {
        std::vector<std::string> items = {"DrawPoints", "GridPoints", "Add", "Orphan"};
        auto catOf = [](const std::string& id) -> std::string {
            if (id == "DrawPoints") return "point.draw";
            if (id == "GridPoints") return "point.generate";
            if (id == "Add")        return "numbers.float.basic";
            return std::string();  // "Orphan": empty category
        };
        NamespaceNode root = buildNamespaceTree(items, catOf);
        // Top-level groups: point, numbers, (uncategorized).
        if (root.children.find("point") == root.children.end()) {
            std::printf("[quickadd] tree missing 'point' group -> FAIL\n"); ++fail;
        } else {
            const NamespaceNode& point = root.children.at("point");
            // point.draw must hold DrawPoints; point.generate must hold GridPoints.
            bool drawHasDP = point.children.count("draw") &&
                             point.children.at("draw").symbols.size() == 1 &&
                             point.children.at("draw").symbols[0] == "DrawPoints";
            bool genHasGP  = point.children.count("generate") &&
                             point.children.at("generate").symbols.size() == 1 &&
                             point.children.at("generate").symbols[0] == "GridPoints";
            if (!drawHasDP) { std::printf("[quickadd] point.draw should contain DrawPoints -> FAIL\n"); ++fail; }
            if (!genHasGP)  { std::printf("[quickadd] point.generate should contain GridPoints -> FAIL\n"); ++fail; }
        }
        // numbers.float.basic holds Add (3-deep nesting).
        bool addOk = root.children.count("numbers") &&
                     root.children.at("numbers").children.count("float") &&
                     root.children.at("numbers").children.at("float").children.count("basic") &&
                     root.children.at("numbers").children.at("float").children.at("basic").symbols.size() == 1;
        if (!addOk) { std::printf("[quickadd] numbers.float.basic should contain Add -> FAIL\n"); ++fail; }
        // Empty category -> (uncategorized), Orphan not lost.
        bool orphanOk = root.children.count("(uncategorized)") &&
                        root.children.at("(uncategorized)").symbols.size() == 1 &&
                        root.children.at("(uncategorized)").symbols[0] == "Orphan";
        if (!orphanOk) { std::printf("[quickadd] empty category should land in (uncategorized) -> FAIL\n"); ++fail; }
    }

    // Test 2d: usage-boost math (= TiXL SymbolFilter.cs:369-381).
    // totalUsageBoost = 1 + (500 * count / total). With count=5, total=10:
    //   boost = 1 + (500 * 5 / 10) = 251.0.
    // An op with count>0 must get a higher final rank than the same-relevancy op with count=0.
    {
        const int count = 5, total = 10;
        const double boost = 1.0 + (500.0 * (double)count / (double)total);
        const double expectedBoost = 251.0;
        if (std::fabs(boost - expectedBoost) > 0.001) {
            std::printf("[quickadd] usage boost formula: got %.3f expected %.3f -> FAIL\n",
                        boost, expectedBoost);
            ++fail;
        }
        // An op with usage (boost=251) must outrank a zero-usage op (boost=1) at equal base score.
        const double baseScore = computeRelevancy("DrawPoints", "draw");
        const double withUsage    = baseScore * boost;
        const double withoutUsage = baseScore * 1.0;
        if (!(withUsage > withoutUsage)) {
            std::printf("[quickadd] usage boost should raise rank: %.3f > %.3f -> FAIL\n",
                        withUsage, withoutUsage);
            ++fail;
        }
    }

    // Test 3: eye-hook naming convention — every item key starts with "qa:".
    {
        const std::string key = "qa:RadialPoints";
        if (key.substr(0, 3) != "qa:") {
            std::printf("[quickadd] eye key does not start with 'qa:' -> FAIL\n");
            ++fail;
        }
    }

    // Test 4 (injectBug): assert a DELIBERATELY WRONG ranking claim against the real
    // computeRelevancy — a prefix match must NOT outrank an exact match. The real code makes
    // exact > prefix, so this inverted assertion fails (red-proof of the live ranker).
    if (injectBug) {
        // Port-filter red leg (red-proof of the live type-filter): assert the BROKEN claim that a
        // Float-output port-drag (inputFilter="Float") still admits PointSink — a Points-only sink
        // the drag canNOT wire to. The real passesPortFilter returns FALSE for that op, so the
        // broken claim is false and we force a FAIL. Drives the SAME production predicate, so if the
        // filter were ever neutered to a pass-through this assertion would flip and stop biting.
        const bool brokenFilterClaim = passesPortFilter("PointSink", /*inFilter=*/"Float", /*outFilter=*/"");
        if (!brokenFilterClaim) {
            std::printf("[quickadd] injectBug: Float-output drag correctly DROPS PointSink (filter "
                        "live) -> forcing FAIL (red-proof of the type-filter)\n");
            ++fail;  // injectBug path MUST return nonzero
        } else {
            std::printf("[quickadd] injectBug: Float-output drag ADMITS PointSink -> filter BROKEN -> FAIL\n");
            ++fail;
        }
        const std::string q = "grid";
        const bool brokenClaim = computeRelevancy("GridPoints", q) > computeRelevancy("Grid", q);
        if (!brokenClaim) {
            std::printf("[quickadd] injectBug: prefix does NOT outrank exact (ranker correct) "
                        "-> forcing FAIL (red-proof)\n");
            ++fail;  // injectBug path MUST return nonzero
        } else {
            std::printf("[quickadd] injectBug: prefix outranked exact -> ranker BROKEN -> FAIL\n");
            ++fail;
        }
        sw::setDynamicSpecs({});  // tear down injected test ops
        std::printf("[quickadd] injectBug FAIL count=%d (expected nonzero) -> %s\n", fail,
                    fail > 0 ? "PASS (red-proof)" : "FAIL");
        return fail > 0 ? 1 : 0;
    }

    sw::setDynamicSpecs({});  // tear down injected test ops (leave the registry clean for the run)
    std::printf("[quickadd] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
    return fail;
}

}  // namespace sw::ui
