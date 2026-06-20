// ListToBuffer — the CPU PointList → GPU Points UPLOAD BRIDGE (pointlist seam, the承重 crossing).
// TiXL authority: external/tixl/Operators/Lib/render/_dx11/buffer/ListToBuffer.cs (verbatim below).
//
//   ListToBuffer.cs Update():
//     var listsCollectedInputs = Lists.CollectedInputs.Select(c => c.GetValue(context))
//                                     .Where(c => c != null).ToList();
//     if (listsCollectedInputs.Count == 0) { OutBuffer.Value = null; Length.Value = 0; return; }
//     var totalSizeInBytes = sum(entry.TotalSizeInBytes);
//     using (var data = new DataStream(totalSizeInBytes,...)) {
//        foreach (structuredList) structuredList.WriteToStream(data);     // concat all lists' bytes
//        ResourceManager.SetupStructuredBuffer(data, totalSizeInBytes, elementSizeInBytes, ref _buffer);
//        Length.Value = totalSizeInBytes / elementSizeInBytes;            // total element count
//     }
//     ... CreateStructuredBufferSrv/Uav ...; OutBuffer.Value = _bufferWithViews;
//
//   Input : Lists  = MultiInputSlot<StructuredList> (one or more host point lists, concatenated).
//   Output: OutBuffer = Slot<BufferWithViews> (= the GPU StructuredBuffer<Point> the rest of the GPU
//           point graph consumes via DrawPoints). Length = total element count.
//
// THE BRIDGE (why this op exists / the seam's whole point): sw's render terminal (DrawPoints) ONLY
// reads a GPU SwPoint buffer (draw_points.metal). The _cpu point family produces a HOST list. ListToBuffer
// is the ONLY crossing: it memcpys the concatenated host SwPoints into a StorageModeShared MTL buffer.
// After it, the EXISTING GPU path (cookNode → DrawPoints command → RenderTarget) draws the points with
// ZERO changes. This op's OUTPUT dataType is "Points" (a GPU buffer), so it IS a normal Points-producing
// op for everything downstream.
//
// WHERE THE WORK LIVES: ListToBuffer carries only its SPEC here (pushed into pointListSpecSink so
// node_registry's findSpec/specTypes surface it). The bridge BODY — gather the upstream PointList(s),
// size the output buffer (ensureOut), memcpy the host SwPoints into contents() — lives in the cook
// DRIVER (point_graph.cpp / point_graph_resident.cpp cookNode), detected via isListToBufferType(),
// because only the driver owns the Impl (ensureOut + the gathered list + outCount). ListToBuffer needs
// NO cookReg entry: the driver special-cases it BEFORE the generic Points cook, so it never falls into
// the generic dispatch. The inject-bug RED rides pointListInjectBug() (shared with the producer leaves):
// the gathered-list corruption (driver) or a stride-mismatch tooth fires the GPU readback RED.
//
// FORK (named):
//   - listtobuffer-host-memcpy: TiXL's DataStream + ResourceManager.SetupStructuredBuffer (a DX11
//     dynamic StructuredBuffer fill) becomes a Metal StorageModeShared buffer + a bytewise memcpy of
//     sizeof(SwPoint)*count into contents() (the 64B stride is byte-identical to T3 Point's Stride=64).
//   - listtobuffer-length-output-dropped: TiXL exposes a second Length(int) output; sw's GPU Points
//     flow carries the count in Impl::outCount (debugCookedCount) — a separate Float "Length" output
//     would need the host-scalar/outCache rail. DEFERRED (no production consumer needs it yet); the
//     count is fully observable via debugCookedCount / the GPU readback. Named, not silent.
#include <string>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // pointListSpecSink()

namespace sw {

// The bridge predicate: is `type` the ListToBuffer upload bridge? The cook driver checks this to take
// the PointList-gather + memcpy path instead of the normal Points-input gather. One member today;
// keeps the literal string out of two driver TUs.
bool isListToBufferType(const std::string& type) { return type == "ListToBuffer"; }

namespace {

struct RegisterListToBuffer {
  RegisterListToBuffer() {
    // NodeSpec: ONE PointList MultiInput ("Lists") → ONE Points output ("OutBuffer"). The Points output
    // makes every downstream GPU op (DrawPoints) treat it as a normal point bag. The SPEC rides the
    // pointListSpecSink() (the pointlist family's self-registration sink that node_registry's findSpec/
    // specTypes read live) so it surfaces in the Add menu + findSpec WITHOUT editing a static point-op
    // table. (No PointListCookFn / no cookReg entry — the driver owns the bridge body, see header.)
    NodeSpec spec;
    spec.type = "ListToBuffer";
    spec.title = "ListToBuffer";
    spec.ports = {
        {"OutBuffer", "OutBuffer", "Points", false},
        {"Lists", "Lists", "PointList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
         /*multiInput=*/true},
    };
    spec.evaluate = nullptr;
    pointListSpecSink().push_back(spec);
  }
};
const RegisterListToBuffer _reg_listtobuffer;

}  // namespace

}  // namespace sw
