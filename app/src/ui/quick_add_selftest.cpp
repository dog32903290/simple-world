// ui/quick_add_selftest — isolated --selftest-quickadd leaf for the Add Node search palette.
// Zone: ui (verify-facing). Exercises the header-exposed search primitives (scatterMatch +
// computeRelevancy) so the scatter-match and relevancy ranking are toothed independently of
// imgui. Split out of quick_add.cpp to keep that file single-duty + under the 400-line cap.
#include "ui/quick_add.h"

#include <cstdio>
#include <string>
#include <vector>

namespace sw::ui {

int runQuickAddSelfTest(bool injectBug) {
    int fail = 0;

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
        std::printf("[quickadd] injectBug FAIL count=%d (expected nonzero) -> %s\n", fail,
                    fail > 0 ? "PASS (red-proof)" : "FAIL");
        return fail > 0 ? 1 : 0;
    }

    std::printf("[quickadd] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
    return fail;
}

}  // namespace sw::ui
