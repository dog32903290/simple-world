// Image2dSDF field op (the FIRST field leaf that BINDS A TEXTURE — it rides the texture-into-field
// "Seam A" added to the frozen base: FieldNode::collectTextures + the template's /*{TEXTURES}*/ hooks).
// Like SphereSDF / CustomSDF this single .cpp owns BOTH halves of one SDF op: the codegen NODE
// (Image2dSDFNode below) AND the OP layer (a NodeSpec for the Add menu / findSpec + a FieldNodeFactory),
// registered via the file-scope FieldOp registrar. The base machinery (FieldNode interface,
// assembleFieldMSL, param packing) stays FROZEN — adding a field op = this one .cpp + one CMakeLists
// line, plus (for the texture seam) the ONE no-op collectTextures override.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/_/ExecuteImage2dSdf.cs (the codegen
// IGraphNodeOp — the authoritative param-packer) + Image2dSDF.cs (the public op shell).
//
//   AddDefinitions (ExecuteImage2dSdf.cs:33-59) registers ONE global `sdf2DColumn{node}` into c.Globals:
//     float sdf2DColumn{node}(float2 pos, float2 imageSize, float sdfScale)
//     {
//         float2 uv = pos / imageSize; // image projected onto XY plane
//         uv.y *= -1;
//         uv += 0.5;
//         float2 clampedUV = clamp(uv, 0.0, 1.0);
//         float2 delta = uv - clampedUV;
//         float texDist = 1-saturate({node}SdfImage.SampleLevel(ClampedSampler, clampedUV, 0.0));
//         texDist *= sdfScale;
//         float2 worldDelta = delta * imageSize;
//         float outsideDist = length(worldDelta);
//         if (all(uv >= 0.0) && all(uv <= 1.0))
//             return texDist;
//         return sqrt( outsideDist*outsideDist + texDist*texDist);
//     }
//   TryBuildCustomCode (ExecuteImage2dSdf.cs:61-66) emits the call (the leaf's preShaderCode here):
//     f{c}.w = (sdf2DColumn{node}(p{c}.xy, {node}Size, {node}Scale) + {node}Offset);
//     f{c}.xyz = p{c}.w < 0.5 ?  p{c}.xyz : 1; // save local space
//   AppendShaderResources (ExecuteImage2dSdf.cs:68-81) declares the SRV:
//     list.Add(new SrvBufferReference("Texture2D<float> {node}SdfImage", _srv));
//   [GraphParam] declaration order (ExecuteImage2dSdf.cs:89-99): Size (Vector2), Scale (float),
//     Offset (float). SdfImageSrv is a plain InputSlot<ShaderResourceView> (NOT [GraphParam]; it is the
//     bound texture, not a packed float). .t3 defaults are 0 (no Image2dSDF.t3 ships authored values;
//     the cook seam below injects a host texture + caller params for the golden).
//
// ★ THE TEXTURE SEAM (what this leaf proves): the SDF image is a fragment texture, not a packed float.
//   The leaf returns it via collectTextures (declName = "<prefix>SdfImage"); the assembler threads it
//   into the fragment ([[texture(N)]]) and into evalField, and the call passes it to sdf2DColumn. The
//   HOST supplies the texture through configureImage2dSdf (Seam B — the real image-input graph port —
//   is DEFERRED; the cook seam stands in for it so the golden has a deterministic authored texture).
//
// HLSL->MSL forks honored (named):
//   (1) GLOBAL SRV -> FUNCTION ARG: in TiXL `{node}SdfImage` and `ClampedSampler` are global symbols the
//       function body names directly (DX SRV + framework SamplerState). MSL has no global texture/sampler
//       in this template, so sdf2DColumn gains two params `texture2d<float> sdfImage, sampler s`, and the
//       call passes `P_{node}SdfImage` (the fragment texture arg) + `clampedSampler` (the template's
//       inline sampler). This is the load-bearing texture fork — same class as the cbuffer->struct fork.
//   (2) Texture2D<float>.SampleLevel(sampler, uv, 0.0) -> MSL sdfImage.sample(s, uv, level(0.0)).r
//       (.r reads the single R32Float channel; level() is MSL's explicit-LOD form of SampleLevel).
//   (3) saturate(x) -> clamp(x, 0.0, 1.0) (MSL has no `saturate`; this is the standard expansion).
//   (4) the call's `{node}Size`/`{node}Scale`/`{node}Offset` are PACKED params -> read P.{prefix}Name
//       (cbuffer->struct access). `1` in the color line -> float3(1.0); bare `p{c}.w` kept verbatim
//       (template seeds p.w=0 so `< 0.5` is true) — both already ported by SphereSDF/CustomSDF.
//   No math fork: the distance arithmetic (uv map, clamp, delta, texDist, outsideDist, the in/out-bounds
//   branch) is byte-identical text in HLSL and MSL.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, TexBinding, appendVec2/ScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- Image2dSDF codegen node (a FieldNode subclass; texture-binding leaf) -------------------------

struct Image2dSDFNode : FieldNode {
  // [GraphParam] declaration order: Size (vec2) -> Scale (scalar) -> Offset (scalar). .t3 has no
  // authored defaults (0); the cook seam injects real values for the golden.
  float sizeX = 0.f, sizeY = 0.f;  // Size (image-plane extent in field space, vec2)
  float scale = 0.f;               // Scale (sdfScale — multiplies the in-bounds texture distance)
  float offset = 0.f;              // Offset (added to the whole column distance)
  const void* sdfImage = nullptr;  // opaque MTL::Texture* (the bound SDF image); host-supplied via cook

  explicit Image2dSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix AND the unique
    // sdf2DColumn function-name suffix AND the SdfImage texture-decl prefix.
    prefix = "Image2dSDF_" + shortId + "_";
  }

  // Unique global-function name = "sdf2DColumn" + prefix (TiXL uses `sdf2DColumn{ShaderNode}`).
  std::string columnFnName() const { return "sdf2DColumn_" + prefix; }
  // The texture decl name (without the assembler's "P_") = "<prefix>SdfImage" (TiXL `{node}SdfImage`).
  std::string sdfImageArgName() const { return prefix + "SdfImage"; }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY ExecuteImage2dSdf.cs:33-59 AddDefinitions — register the sdf2DColumn global (de-duped by
    // the unique fn-name key, like SphereSDF's params). Forks (1)(2)(3) applied; math byte-verbatim.
    const std::string fn = columnFnName();
    c.globals[fn] =
        "float " + fn + "(float2 pos, float2 imageSize, float sdfScale, texture2d<float> sdfImage, sampler s)\n"
        "{\n"
        "    float2 uv = pos / imageSize; // image projected onto XY plane\n"
        "    uv.y *= -1;\n"
        "    uv += 0.5;\n"
        "    float2 clampedUV = clamp(uv, 0.0, 1.0);\n"
        "    float2 delta = uv - clampedUV;\n"
        "\n"
        "    float texDist = 1-clamp(sdfImage.sample(s, clampedUV, level(0.0)).r, 0.0, 1.0);\n"
        "    texDist *= sdfScale;\n"
        "\n"
        "    float2 worldDelta = delta * imageSize;\n"
        "    float outsideDist = length(worldDelta);\n"
        "\n"
        "    // If inside bounds, return texture value\n"
        "    if (all(uv >= 0.0) && all(uv <= 1.0))\n"
        "        return texDist;\n"
        "\n"
        "    // Outside bounds: approximate distance to closest edge or corner\n"
        "    return sqrt( outsideDist*outsideDist + texDist*texDist);\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY ExecuteImage2dSdf.cs:63-64 TryBuildCustomCode. The call passes p{c}.xy (local), the two
    // PACKED params Size/Scale (P.-qualified), then the TEXTURE arg + the inline clampedSampler. Offset
    // is added outside the column call. The color line is verbatim (fork: `1`->float3(1.0)).
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = (" + columnFnName() + "(p" + ctx + ".xy, P." + prefix + "Size, P." +
                 prefix + "Scale, P_" + sdfImageArgName() + ", clampedSampler) + P." + prefix +
                 "Offset);");
    c.appendCall("f" + ctx + ".xyz = p" + ctx + ".w < 0.5 ? p" + ctx +
                 ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order: Size (vec2) -> Scale (scalar) -> Offset (scalar). Size starts at offset 0
    // (vec2 pad = 0), then Scale=floats[2], Offset=floats[3] -> one 16B slot, no trailing padding.
    appendVec2Param(floatParams, paramFields, prefix + "Size", sizeX, sizeY);
    appendScalarParam(floatParams, paramFields, prefix + "Scale", scale);
    appendScalarParam(floatParams, paramFields, prefix + "Offset", offset);
  }

  void collectTextures(std::vector<TexBinding>& out) const override {
    // PARITY ExecuteImage2dSdf.cs:68-81 AppendShaderResources — declare the one SDF-image texture. The
    // assembler turns this into a fragment [[texture(N)]] arg `P_<prefix>SdfImage`; field_render binds
    // the opaque handle at slot N. (TiXL: `Texture2D<float> {node}SdfImage`.)
    out.push_back(TexBinding{sdfImageArgName(), sdfImage});
  }
};

NodeSpec image2dSdfSpec() {
  NodeSpec s;
  s.type = "Image2dSDF";
  s.title = "Image 2d SDF";
  // SdfImage = the bound texture input (Texture2D). Seam B (the real image graph-cook port) is
  // DEFERRED; this port exists so the op is shaped like TiXL, but the golden binds via the cook seam.
  PortSpec img; img.id = "SdfImage"; img.name = "Sdf Image"; img.dataType = "Texture2D"; img.isInput = true;
  // Size = Vec2 head run (.x/.y), the image-plane extent. .t3 default (0,0) (no authored .t3 values).
  PortSpec sx; sx.id = "Size.x"; sx.name = "Size"; sx.dataType = "Float"; sx.isInput = true;
  sx.def = 0.0f; sx.minV = -100.0f; sx.maxV = 100.0f; sx.widget = Widget::Vec; sx.vecArity = 2;
  PortSpec sy; sy.id = "Size.y"; sy.name = "Size.y"; sy.dataType = "Float"; sy.isInput = true;
  sy.def = 0.0f; sy.minV = -100.0f; sy.maxV = 100.0f;
  // Scale (sdfScale), Offset = scalar Floats. .t3 defaults 0.
  PortSpec sc; sc.id = "Scale"; sc.name = "Scale"; sc.dataType = "Float"; sc.isInput = true;
  sc.def = 0.0f; sc.minV = -10.0f; sc.maxV = 10.0f;
  PortSpec of; of.id = "Offset"; of.name = "Offset"; of.dataType = "Float"; of.isInput = true;
  of.def = 0.0f; of.minV = -10.0f; of.maxV = 10.0f;
  // Output: a Field (ShaderGraphNode in TiXL).
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {img, sx, sy, sc, of, out};
  return s;
}

// Factory: build an Image2dSDFNode for an instance. Params/texture default to none (.t3 has no authored
// values); a graph cook (Seam B, deferred) would override them. The golden sets them via the cook seam.
std::shared_ptr<FieldNode> makeImage2dSdf(const std::string& shortId) {
  return std::make_shared<Image2dSDFNode>(shortId);
}

const FieldOp g_image2dSdfOp(image2dSdfSpec(), makeImage2dSdf);

}  // namespace

// Param-cook seam (mirrors configureCustomSdf / configureCombineSdf): set the packed params AND inject
// the HOST-supplied SDF image texture on a node built via makeFieldNode("Image2dSDF", ...). The leaf
// type is TU-private; this free function downcasts inside the owning TU so callers (the GPU golden;
// later a graph-cook walk) can override without the type leaking. `texture` is an opaque MTL::Texture*
// (the caller owns its lifetime through the render). No-op if `node` is not an Image2dSDFNode.
//
// THIS STANDS IN FOR SEAM B: the real op would read SdfImage from a connected Texture2D port during a
// graph cook. That graph-cook image port is DELIBERATELY NOT BUILT in this batch — the cook seam feeds
// a deterministic host texture so the golden tests the texture-BIND path (Seam A), not the image port.
void configureImage2dSdf(FieldNode& node, const void* texture, float sizeX, float sizeY, float scale,
                         float offset) {
  if (auto* n = dynamic_cast<Image2dSDFNode*>(&node)) {
    n->sdfImage = texture;
    n->sizeX = sizeX;
    n->sizeY = sizeY;
    n->scale = scale;
    n->offset = offset;
  }
}

}  // namespace sw
