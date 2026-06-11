// app/frame_cook — the per-frame PRODUCTION cook (product behaviour, out of the main shell).
// Zone: app. Depends on runtime only (never ui — the caller resolves viewTarget from ui
// session state and passes the plain node id).
//
// One call = one frame: advance the app clock, rebuild the resident mirror if the graph
// revision changed (document.h mirror contract), cook every AudioReaction node from the live
// spectrum (outCache + resident extOut), then cook the RESIDENT eval graph into pg's target.
#pragma once

namespace sw {
class PointGraph;
}

namespace sw::framecook {

// Cook one production frame into `pg`. `viewTarget` = the flat node id the viewport shows
// (pin/selection/terminal — resolved by the shell); bridge paths == node ids, so the
// resident target path is its string form.
void run(PointGraph& pg, int viewTarget);

}  // namespace sw::framecook
