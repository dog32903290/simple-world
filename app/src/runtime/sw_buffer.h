// runtime/sw_buffer — the "Buffer" PORT CURRENCY (the Seam-1 keystone leaf).
//
// TiXL authority: external/tixl/Core/DataTypes/BufferWithViews.cs:5-9 —
//     public sealed class BufferWithViews : IDisposable {
//         public SharpDX.Direct3D11.Buffer Buffer;
//         public SharpDX.Direct3D11.ShaderResourceView Srv;
//         public SharpDX.Direct3D11.UnorderedAccessView Uav;
//     }
// A DX11 BufferWithViews carries a triple — the backing Buffer + a read view (Srv) + a write view
// (Uav). On Metal there is ONE object: an MTL::Buffer is directly readable AND writable from a
// compute/render shader (no separate view objects), and a structured-buffer SRV's ElementCount /
// StructureByteStride are just integers the producer already knows. So the DX11 triple collapses to a
// single MTL::Buffer plus the two integers DX11 stored on the view descriptors.
//
// NAMED FORK `bufferwithviews-collapse-to-mtlbuffer` — Buffer+Srv+Uav → one `bytes` MTL::Buffer. Every
//   Buffer-op leaf header cites this fork. The integers DX11 read off the SRV/UAV descriptors
//   (Srv.Description.Buffer.ElementCount = GetBufferComponents.cs:60; Buffer.Description.
//   StructureByteStride = :61) ride as `elementCount` / `elementStride` here, set by the producer.
//
// LIFETIME: `bytes` is DRIVER-OWNED (PointGraph::Impl::rawBuf, keyed by flatKey/resident path, the same
// allocate→reuse→reallocate-on-grow lifetime as outBuf). A SwBuffer is a BORROWED VIEW handed between
// Buffer-typed ports during one cook — it never owns/releases `bytes`. Mirrors how a Points op borrows
// the MTL::Buffer the driver allocated for it.
#pragma once

#include <cstdint>

namespace MTL {
class Buffer;
}  // namespace MTL

namespace sw {

// The "Buffer" currency (== TiXL BufferWithViews collapsed for Metal). All four members are plain data;
// `bytes` is borrowed (driver-owned). A default SwBuffer (null + zeros) is the "no buffer / invalid"
// state (TiXL's null BufferWithViews → GetBufferComponents IsValid=false, GetBufferComponents.cs:79,88).
struct SwBuffer {
  MTL::Buffer* bytes = nullptr;   // the ONE buffer (DX11 Buffer+Srv+Uav folded); driver-owned, borrowed
  uint32_t elementStride = 0;     // bytes/element (TiXL Buffer.Description.StructureByteStride)
  uint32_t elementCount = 0;      // element count (TiXL Srv.Description.Buffer.ElementCount = Length)
  uint32_t elementFormat = 0;     // tag: 0=raw/float (reserved for typed buffers; unused in the spike)
};

}  // namespace sw
