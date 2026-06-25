// mesh_blendpick_golden — goldens for BlendMeshVertices + PickMeshBuffer (mesh modify Phase C batch).
//
// BlendMeshVertices: QuadMeshA(0,0,0)+QuadMeshB(10,0,0) → op; assert blended vertex positions vs
//   TiXL mesh-BlendVertices.hlsl closed-form math.
//   Sub-case 1: BlendMode=0,BlendValue=0.5,Scatter=0,Pairing=0. resultCount=4; f=0.5 for all.
//     v0.Position=lerp((0,0,0),(10,0,0),0.5)=(5,0,0) [LIVE probe]. Topology from MeshA unchanged.
//   Sub-case 2: BlendMode=1(UseW1AsWeight). f=Selection=1 → full B. v0=(10,0,0).
//   Sub-case 3: Scatter=0.3 — exercises Dave-Hoskins hash11 path (hash-functions.hlsl:10).
//     i=1, t=0.25: hash11(0.25)≈0.47832 → f≈0.49350 → v1.x≈4.9350 (wrong sin-hash gives ≈3.670).
//   Production-pixel leg: single QuadMesh → op(MeshA only) → DrawMeshUnlit → RenderTarget via resident.
//   injectBug corrupts v[0].Position → sub-case 1+3 probes fail + pixel probe dark → RED.
//
// PickMeshBuffer: two QuadMeshes into MultiInput → pick by Index (mod-wrap).
//   Index=0 → MeshA (v0=(0,0,0)). Index=1 → MeshB (v0=(10,0,0)). Index=2 → wraps → MeshA.
//   Production-pixel leg: single QuadMesh → pick(Index=0) → DrawMeshUnlit → RenderTarget.
//   injectBug corrupts v[0].Position → sub-case 0 probe fails + pixel probe dark → RED.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/field_camera.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/mesh_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/sw_mesh.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-3f) { return std::fabs(a - b) < t; }
bool p3(const SW_MESH_PACKED3& v, float x, float y, float z) {
  return nearf(v.x, x) && nearf(v.y, y) && nearf(v.z, z); }
bool triEq(const SwTriIndex& t, int x, int y, int z) { return t.X==x && t.Y==y && t.Z==z; }

Node makeQuad(int id, float cx, float cy, float cz) {
  Node m; m.id=id; m.type="QuadMesh";
  m.params["Segments.x"]=1.0f; m.params["Segments.y"]=1.0f; m.params["Scale"]=1.0f;
  m.params["Stretch.x"]=1.0f; m.params["Stretch.y"]=1.0f;
  m.params["Pivot.x"]=0.5f; m.params["Pivot.y"]=0.5f;
  m.params["Center.x"]=cx; m.params["Center.y"]=cy; m.params["Center.z"]=cz;
  return m;
}
int meshOutPin(const char* type) {
  const NodeSpec* s=findSpec(type);
  for (size_t i=0;i<s->ports.size();++i) if (!s->ports[i].isInput&&s->ports[i].dataType=="Mesh") return (int)i;
  for (size_t i=0;i<s->ports.size();++i) if (!s->ports[i].isInput) return (int)i;
  return 0;
}

// Production-pixel leg: single QuadMesh(−0.5,−0.5,0) → op(meshInputPin wired) → DrawMeshUnlit → RT.
// Assert lower-left interior is RED (proves op runs on resident path).
int productionLeg(const char* opType, const char* tag, bool injectBug,
                  const std::vector<std::pair<const char*,float>>& opParams, int meshInputPin) {
  NS::AutoreleasePool* pool=NS::AutoreleasePool::alloc()->init();
  const uint32_t W=256, H=256;
  MTL::Device* dev=MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q=dev->newCommandQueue();
  NS::Error* err=nullptr;
  MTL::Library* lib=dev->newLibrary(NS::String::string(SW_SHADER_METALLIB,NS::UTF8StringEncoding),&err);
  if (!lib) { std::printf("[%s] FAIL: no metallib\n",tag); q->release(); dev->release(); pool->release(); return 1; }
  registerBuiltinPointOps();
  PointGraph pg(dev,lib,q,W,H);
  Graph g;
  g.nodes.push_back(makeQuad(1,-0.5f,-0.5f,0.0f));
  Node op; op.id=2; op.type=opType;
  for (auto& kv:opParams) op.params[kv.first]=kv.second;
  g.nodes.push_back(op);
  Node draw; draw.id=3; draw.type="DrawMeshUnlit";
  draw.params["Color.x"]=1.0f; draw.params["Color.y"]=0.0f; draw.params["Color.z"]=0.0f; draw.params["Color.w"]=1.0f;
  g.nodes.push_back(draw);
  Node rt; rt.id=4; rt.type="RenderTarget";
  rt.params["Resolution"]=4.0f; rt.params["CustomW"]=(float)W; rt.params["CustomH"]=(float)H;
  g.nodes.push_back(rt);
  int qOut=meshOutPin("QuadMesh"), opOut=meshOutPin(opType);
  int drawMeshIn=-1, drawOut=-1, rtCmdIn=0;
  { const NodeSpec* ds=findSpec("DrawMeshUnlit");
    for (size_t i=0;i<ds->ports.size();++i) {
      if (ds->ports[i].isInput&&ds->ports[i].dataType=="Mesh"&&drawMeshIn<0) drawMeshIn=(int)i;
      if (!ds->ports[i].isInput&&drawOut<0) drawOut=(int)i; } }
  { const NodeSpec* rs=findSpec("RenderTarget");
    for (size_t i=0;i<rs->ports.size();++i)
      if (rs->ports[i].isInput&&rs->ports[i].dataType=="Command") { rtCmdIn=(int)i; break; } }
  g.connections.push_back({100,pinId(1,qOut),pinId(2,meshInputPin)});
  g.connections.push_back({101,pinId(2,opOut),pinId(3,drawMeshIn)});
  g.connections.push_back({102,pinId(3,drawOut),pinId(4,rtCmdIn)});
  SymbolLibrary slib=libFromGraph(g);
  ResidentEvalGraph rg=buildEvalGraph(slib,slib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex=0; ctx.time=0.0f; ctx.deltaTime=1.0f/60.0f;
  meshInjectBug()=injectBug;
  pg.cookResident(rg,ctx,nullptr,"4");
  meshInjectBug()=false;
  MTL::Texture* tex=pg.target();
  bool sized=tex&&(uint32_t)tex->width()==W&&(uint32_t)tex->height()==H;
  auto project=[&](float wx,float wy,float wz,float out[3]) {
    LayerCameraForward cam=defaultLayerCameraForward(1.0f);
    Mat4 o2c=objectToClipSpace(mat4Identity(),cam.worldToCamera,cam.cameraToClipSpace);
    mat4TransformPointDivW(o2c,wx,wy,wz,out); };
  auto ndcX=[&](float n){ return (int)((n*0.5f+0.5f)*(float)(W-1)+0.5f); };
  auto ndcY=[&](float n){ return (int)((1.0f-(n*0.5f+0.5f))*(float)(H-1)+0.5f); };
  int ir=0,ig=0,ib=0,cr=0,cg=0,cb=0;
  if (sized) {
    std::vector<uint8_t> px((size_t)W*H*4,0);
    tex->getBytes(px.data(),W*4,MTL::Region::Make2D(0,0,W,H),0);
    auto rd=[&](int x,int y,int& r,int& gg,int& b){ size_t i=((size_t)y*W+x)*4; r=px[i]; gg=px[i+1]; b=px[i+2]; };
    float ie[3]; project(-0.3f,-0.3f,0.0f,ie);
    float fc[3]; project(0.9f,0.9f,0.0f,fc);
    rd(ndcX(ie[0]),ndcY(ie[1]),ir,ig,ib);
    rd(ndcX(fc[0]),ndcY(fc[1]),cr,cg,cb); }
  bool pass=sized&&ir>250&&ig<5&&ib<5&&cr<30&&cg<30&&cb<30;
  std::printf("[%s] pixel: f0=(%d,%d,%d) corner=(%d,%d,%d) -> %s\n",tag,ir,ig,ib,cr,cg,cb,pass?"PASS":"FAIL");
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace

// ============================== BlendMeshVertices ==============================
int runMeshBlendMeshVerticesGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool=NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev=MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q=dev->newCommandQueue();
  bool ok=true;

  int meshAPin=-1, meshBPin=-1;
  { const NodeSpec* s=findSpec("BlendMeshVertices");
    for (size_t i=0;i<s->ports.size();++i)
      if (s->ports[i].isInput&&s->ports[i].dataType=="Mesh") {
        if (meshAPin<0) meshAPin=(int)i; else if (meshBPin<0) meshBPin=(int)i; } }

  // Sub-case 1: BlendMode=0 (Blend), BlendValue=0.5, no scatter. f=0.5 → halfway positions.
  {
    PointGraph pg(dev,nullptr,q,64,64); Graph g;
    g.nodes.push_back(makeQuad(1,0.0f,0.0f,0.0f)); g.nodes.push_back(makeQuad(2,10.0f,0.0f,0.0f));
    Node op; op.id=3; op.type="BlendMeshVertices";
    op.params["BlendValue"]=0.5f; op.params["Scatter"]=0.0f; op.params["BlendMode"]=0.0f; op.params["Pairing"]=0.0f;
    g.nodes.push_back(op);
    int qOut=meshOutPin("QuadMesh");
    g.connections.push_back({100,pinId(1,qOut),pinId(3,meshAPin)});
    g.connections.push_back({101,pinId(2,qOut),pinId(3,meshBPin)});
    EvaluationContext ctx{}; ctx.frameIndex=0; ctx.time=0.0f; ctx.deltaTime=1.0f/60.0f;
    meshInjectBug()=injectBug; pg.cook(g,ctx,nullptr,3); pg.cook(g,ctx,nullptr,3); meshInjectBug()=false;
    const MTL::Buffer* vb=nullptr; const MTL::Buffer* ib=nullptr; uint32_t vc=0,fc=0;
    bool got=pg.debugCookedMesh(3,vb,vc,ib,fc);
    bool pass=got&&vc==4&&fc==2;
    if (pass) {
      const SwVertex* v=(const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      const SwTriIndex* fi=(const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
      bool v0ok=p3(v[0].Position,5.0f,0.0f,0.0f);   // lerp((0,0,0),(10,0,0),0.5)
      bool v1ok=p3(v[1].Position,5.0f,1.0f,0.0f);
      bool v2ok=p3(v[2].Position,6.0f,0.0f,0.0f);
      bool v3ok=p3(v[3].Position,6.0f,1.0f,0.0f);
      bool f0ok=triEq(fi[0],0,2,1)&&triEq(fi[1],2,3,1);  // topology from MeshA
      bool nOk=p3(v[0].Normal,0.0f,0.0f,1.0f);            // lerp((0,0,1),(0,0,1),0.5)=(0,0,1)
      bool selOk=nearf(v[0].Selection,1.0f);               // lerp(1,1,0.5)=1
      pass=v0ok&&v1ok&&v2ok&&v3ok&&f0ok&&nOk&&selOk;
      std::printf("[selftest-mesh-blendmeshvertices] blend0.5: vc=%u v0=(%.1f,%.1f,%.1f) v2=(%.1f,%.1f,%.1f) "
                  "f0=(%d,%d,%d) ok=%d\n", vc,v[0].Position.x,v[0].Position.y,v[0].Position.z,
                  v[2].Position.x,v[2].Position.y,v[2].Position.z,fi[0].X,fi[0].Y,fi[0].Z,pass);
    } else { std::printf("[selftest-mesh-blendmeshvertices] blend0.5 FAIL cook (got=%d vc=%u)\n",got,vc); }
    ok=ok&&pass;
  }

  // Sub-case 2: BlendMode=1 (UseW1AsWeight). f=A.Selection=1 → fully B positions.
  {
    PointGraph pg2(dev,nullptr,q,64,64); Graph g;
    g.nodes.push_back(makeQuad(1,0.0f,0.0f,0.0f)); g.nodes.push_back(makeQuad(2,10.0f,0.0f,0.0f));
    Node op; op.id=3; op.type="BlendMeshVertices";
    op.params["BlendMode"]=1.0f; op.params["Scatter"]=0.0f; op.params["Pairing"]=0.0f;
    g.nodes.push_back(op);
    int qOut=meshOutPin("QuadMesh");
    g.connections.push_back({100,pinId(1,qOut),pinId(3,meshAPin)});
    g.connections.push_back({101,pinId(2,qOut),pinId(3,meshBPin)});
    EvaluationContext ctx{}; ctx.frameIndex=0; ctx.time=0.0f; ctx.deltaTime=1.0f/60.0f;
    meshInjectBug()=false; pg2.cook(g,ctx,nullptr,3); meshInjectBug()=false;
    const MTL::Buffer* vb=nullptr; const MTL::Buffer* ib=nullptr; uint32_t vc=0,fc=0;
    bool got=pg2.debugCookedMesh(3,vb,vc,ib,fc);
    bool pass=got&&vc==4;
    if (pass) {
      const SwVertex* v=(const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      bool v0ok=p3(v[0].Position,10.0f,0.0f,0.0f); bool v3ok=p3(v[3].Position,11.0f,1.0f,0.0f);
      pass=v0ok&&v3ok;
      std::printf("[selftest-mesh-blendmeshvertices] blendW1: v0=(%.1f,%.1f,%.1f) v3ok=%d -> %s\n",
                  v[0].Position.x,v[0].Position.y,v[0].Position.z,v3ok,pass?"PASS":"FAIL");
    } else { std::printf("[selftest-mesh-blendmeshvertices] blendW1 FAIL\n"); }
    ok=ok&&pass;
  }

  // Sub-case 3: Scatter>0 — exercises hash11 path.
  // Setup: same QuadA(0,0,0)+QuadB(10,0,0), BlendMode=0, BlendValue=0.5, Scatter=0.3.
  // Probe vertex i=1 (resultCount=4): t = 1/4 = 0.25.
  // hash11(0.25) — Dave-Hoskins formula (hash-functions.hlsl:10-16):
  //   p = frac(0.25 * 0.1031) = 0.025775
  //   p = 0.025775 * (0.025775 + 33.33) = 0.025775 * 33.355775 = 0.859739...
  //   p = 0.859739 * (0.859739 + 0.859739) = 0.859739 * 1.719478 = 1.478523...
  //   frac(1.478523) = 0.478523...  →  hash11(0.25) ≈ 0.47832
  // f_base = 0.5  (BlendMode=0, BlendValue=0.5)
  // fallOff = smoothstep(1 - |0.5-0.5|*2) = smoothstep(1.0) = 1.0
  // f = 0.5 + (0.47832 - 0.5) * 0.3 * 1.0 = 0.5 - 0.00650 = 0.49350
  // A.v1 = (0,1,0) (QuadA center=(0,0,0)), B.v1 = (10,1,0) (QuadB center=(10,0,0))
  // expected: position.x = lerp(0, 10, 0.49350) = 4.9350  (y=1, z=0 unchanged)
  // The WRONG sin-hash gives hash11_wrong(0.25)≈0.05652 → position.x≈3.670 — distinguishable by >1.2 units.
  {
    PointGraph pg3(dev,nullptr,q,64,64); Graph g;
    g.nodes.push_back(makeQuad(1,0.0f,0.0f,0.0f)); g.nodes.push_back(makeQuad(2,10.0f,0.0f,0.0f));
    Node op; op.id=3; op.type="BlendMeshVertices";
    op.params["BlendValue"]=0.5f; op.params["Scatter"]=0.3f; op.params["BlendMode"]=0.0f; op.params["Pairing"]=0.0f;
    g.nodes.push_back(op);
    int qOut=meshOutPin("QuadMesh");
    g.connections.push_back({100,pinId(1,qOut),pinId(3,meshAPin)});
    g.connections.push_back({101,pinId(2,qOut),pinId(3,meshBPin)});
    EvaluationContext ctx{}; ctx.frameIndex=0; ctx.time=0.0f; ctx.deltaTime=1.0f/60.0f;
    meshInjectBug()=injectBug; pg3.cook(g,ctx,nullptr,3); pg3.cook(g,ctx,nullptr,3); meshInjectBug()=false;
    const MTL::Buffer* vb=nullptr; const MTL::Buffer* ib=nullptr; uint32_t vc=0,fc=0;
    bool got=pg3.debugCookedMesh(3,vb,vc,ib,fc);
    bool pass=got&&vc==4;
    if (pass) {
      const SwVertex* v=(const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      // v[1].Position.x must match Dave-Hoskins hash11(0.25) path: expected ~4.9350
      // Wrong sin-hash would give ~3.670 (>1.2 units off), so tolerance 0.05 catches the difference.
      bool v1xOk=nearf(v[1].Position.x, 4.9350f, 0.05f);
      bool v1yOk=nearf(v[1].Position.y, 1.0f, 1e-3f);
      bool v1zOk=nearf(v[1].Position.z, 0.0f, 1e-3f);
      pass=v1xOk&&v1yOk&&v1zOk;
      std::printf("[selftest-mesh-blendmeshvertices] scatter0.3 i=1: x=%.4f (expect 4.9350) y=%.1f z=%.1f -> %s\n",
                  v[1].Position.x, v[1].Position.y, v[1].Position.z, pass?"PASS":"FAIL");
    } else { std::printf("[selftest-mesh-blendmeshvertices] scatter0.3 FAIL cook (got=%d vc=%u)\n",got,vc); }
    ok=ok&&pass;
  }

  q->release(); dev->release(); pool->release();
  int pixRet=productionLeg("BlendMeshVertices","selftest-mesh-blendmeshvertices-pixel",injectBug,
                            {{"BlendValue",0.5f},{"Scatter",0.0f},{"BlendMode",0.0f},{"Pairing",0.0f}},meshAPin);
  ok=ok&&(pixRet==0);
  std::printf("[selftest-mesh-blendmeshvertices] %s\n",ok?"PASS":"FAIL");
  return ok ? 0 : 1;
}

// ============================== PickMeshBuffer ==============================
int runMeshPickMeshBufferGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool=NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev=MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q=dev->newCommandQueue();
  bool ok=true;

  int pickMeshIn=-1;
  { const NodeSpec* s=findSpec("PickMeshBuffer");
    for (size_t i=0;i<s->ports.size();++i)
      if (s->ports[i].isInput&&s->ports[i].dataType=="Mesh") { pickMeshIn=(int)i; break; } }

  auto cookPick=[&](int nodeIndex, bool bug, const SwVertex** vOut, uint32_t& vc,
                    const SwTriIndex** iOut, uint32_t& fc, PointGraph& pg) -> bool {
    Graph g;
    g.nodes.push_back(makeQuad(1,0.0f,0.0f,0.0f)); g.nodes.push_back(makeQuad(2,10.0f,0.0f,0.0f));
    Node op; op.id=3; op.type="PickMeshBuffer"; op.params["Index"]=(float)nodeIndex; g.nodes.push_back(op);
    int qOut=meshOutPin("QuadMesh");
    g.connections.push_back({100,pinId(1,qOut),pinId(3,pickMeshIn)});
    g.connections.push_back({101,pinId(2,qOut),pinId(3,pickMeshIn)});
    EvaluationContext ctx{}; ctx.frameIndex=0; ctx.time=0.0f; ctx.deltaTime=1.0f/60.0f;
    meshInjectBug()=bug; pg.cook(g,ctx,nullptr,3); pg.cook(g,ctx,nullptr,3); meshInjectBug()=false;
    const MTL::Buffer* vb=nullptr; const MTL::Buffer* ib=nullptr;
    bool got=pg.debugCookedMesh(3,vb,vc,ib,fc);
    if (!got) return false;
    *vOut=(const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    *iOut=(const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    return true; };

  // Sub-case: Index=0 → MeshA
  { PointGraph pg(dev,nullptr,q,64,64);
    const SwVertex* v=nullptr; const SwTriIndex* fi=nullptr; uint32_t vc=0,fc=0;
    bool got=cookPick(0,injectBug,&v,vc,&fi,fc,pg);
    bool pass=got&&vc==4&&fc==2;
    if (pass) {
      bool v0ok=p3(v[0].Position,0.0f,0.0f,0.0f), v3ok=p3(v[3].Position,1.0f,1.0f,0.0f);
      bool f0ok=triEq(fi[0],0,2,1)&&triEq(fi[1],2,3,1);
      pass=v0ok&&v3ok&&f0ok;
      std::printf("[selftest-mesh-pickmeshbuffer] idx=0: v0=(%.1f,%.1f,%.1f) v3ok=%d f0ok=%d ok=%d\n",
                  v[0].Position.x,v[0].Position.y,v[0].Position.z,v3ok,f0ok,pass);
    } else { std::printf("[selftest-mesh-pickmeshbuffer] idx=0 FAIL (got=%d vc=%u)\n",got,vc); }
    ok=ok&&pass; }

  // Sub-case: Index=1 → MeshB
  { PointGraph pg2(dev,nullptr,q,64,64);
    const SwVertex* v=nullptr; const SwTriIndex* fi=nullptr; uint32_t vc=0,fc=0;
    bool got=cookPick(1,false,&v,vc,&fi,fc,pg2);
    bool pass=got&&vc==4;
    if (pass) {
      bool v0ok=p3(v[0].Position,10.0f,0.0f,0.0f), v3ok=p3(v[3].Position,11.0f,1.0f,0.0f);
      pass=v0ok&&v3ok;
      std::printf("[selftest-mesh-pickmeshbuffer] idx=1: v0=(%.1f,%.1f,%.1f) v3ok=%d -> %s\n",
                  v[0].Position.x,v[0].Position.y,v[0].Position.z,v3ok,pass?"PASS":"FAIL");
    } else { std::printf("[selftest-mesh-pickmeshbuffer] idx=1 FAIL\n"); }
    ok=ok&&pass; }

  // Sub-case: Index=2 → wraps mod 2 = 0 → MeshA
  { PointGraph pg3(dev,nullptr,q,64,64);
    const SwVertex* v=nullptr; const SwTriIndex* fi=nullptr; uint32_t vc=0,fc=0;
    bool got=cookPick(2,false,&v,vc,&fi,fc,pg3);
    bool pass=got&&vc==4&&p3(v[0].Position,0.0f,0.0f,0.0f);
    std::printf("[selftest-mesh-pickmeshbuffer] idx=2(wrap): v0=(%.1f,%.1f,%.1f) -> %s\n",
                got&&vc>0?v[0].Position.x:0.0f,got&&vc>0?v[0].Position.y:0.0f,
                got&&vc>0?v[0].Position.z:0.0f,pass?"PASS":"FAIL");
    ok=ok&&pass; }

  q->release(); dev->release(); pool->release();
  int pixRet=productionLeg("PickMeshBuffer","selftest-mesh-pickmeshbuffer-pixel",injectBug,
                            {{"Index",0.0f}},pickMeshIn);
  ok=ok&&(pixRet==0);
  std::printf("[selftest-mesh-pickmeshbuffer] %s\n",ok?"PASS":"FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
