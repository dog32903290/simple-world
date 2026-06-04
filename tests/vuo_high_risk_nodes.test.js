const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

const nodes = {
  mainClock: path.join(repoRoot, "vuo-nodes/my.runtime.clock.mainClock.c"),
  constantImage: path.join(repoRoot, "vuo-nodes/my.image.generate.basic.constantImage.c"),
  blend: path.join(repoRoot, "vuo-nodes/my.image.use.blend.c"),
  keepPreviousFrame: path.join(repoRoot, "vuo-nodes/my.image.use.keepPreviousFrame.c"),
  renderTarget: path.join(repoRoot, "vuo-nodes/my.image.generate.basic.renderTarget.c"),
  clearRenderTarget: path.join(repoRoot, "vuo-nodes/my.render.dx11.api.clearRenderTarget.c"),
};

const compositionPath = path.join(repoRoot, "vuo-compositions/myworld-high-risk-vuo-node-lab.vuo");
const constantPipelineCompositionPath = path.join(repoRoot, "vuo-compositions/myworld-constant-image-pipeline-proof.vuo");
const constantFixturePath = path.join(repoRoot, "docs/runtime/fixtures/constant_image_to_output.graph.json");
const constantContractPath = path.join(repoRoot, "docs/runtime/CONSTANT_IMAGE_CONTRACT.md");

test("Vuo my_ConstantImage exposes the first my-world runtime image.constant proof node", () => {
  const source = fs.readFileSync(nodes.constantImage, "utf8");

  assert.match(source, /"title"\s*:\s*"my_ConstantImage"/);
  assert.match(source, /top_constant_to_output\.graph\.json/);
  assert.match(source, /Runtime type: image\.constant/);
  assert.match(source, /browser alias: top\.constant/);
  assert.match(source, /Primary output: Texture2D \/ VuoImage/);
  assert.match(source, /ColorForTextures #9F008A/);
  assert.match(source, /visible Vuo body-layer proof/);
  assert.match(source, /VuoInputEvent\(\) renderTick/);
  assert.match(source, /VuoInputData\(VuoColor[^)]*"name":"Color"[^)]*"default":\{"r":0\.02,"g":0\.02,"b":0\.02,"a":1\.0\}[^)]*\) Color/);
  assert.match(source, /VuoInputData\(VuoInteger[^)]*"name":"Width"[^)]*"default":1280[^)]*\) Width/);
  assert.match(source, /VuoInputData\(VuoInteger[^)]*"name":"Height"[^)]*"default":720[^)]*\) Height/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"Image"[^)]*\) Image/);
  assert.match(source, /VuoImage_makeColorImage\(Color, renderWidth, renderHeight\)/);
});

test("Vuo my_MainClock centralizes host frame pressure for live preview proofs", () => {
  const source = fs.readFileSync(nodes.mainClock, "utf8");
  const runtimeContract = fs.readFileSync(path.join(repoRoot, "docs/runtime/MY_WORLD_RUNTIME_CONTRACT.md"), "utf8");

  assert.match(source, /"title"\s*:\s*"my_MainClock"/);
  assert.match(source, /centralizes Vuo host frame events into one graph-level renderTick/);
  assert.match(source, /not a final native scheduler or audio-clock contract/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"name":"HostTime"[^)]*\) HostTime/);
  assert.match(source, /VuoInputEvent\(\{"data":"HostTime","eventBlocking":"none"\}\) clockIn/);
  assert.match(source, /VuoOutputData\(VuoInteger[^)]*"name":"FrameIndex"[^)]*\) FrameIndex/);
  assert.match(source, /VuoOutputData\(VuoReal[^)]*"name":"Time"[^)]*\) Time/);
  assert.match(source, /VuoOutputData\(VuoReal[^)]*"name":"DeltaTime"[^)]*\) DeltaTime/);
  assert.match(source, /VuoOutputEvent\(\) renderTick/);
  assert.match(source, /\*renderTick = true/);

  assert.match(runtimeContract, /## Main Clock Contract/);
  assert.match(runtimeContract, /Fire on Display Refresh -> my_MainClock -> renderTick/);
  assert.match(runtimeContract, /`renderTick` carries frame pressure only/);
  assert.match(runtimeContract, /audio clock, MIDI clock, Ableton Link, and final native scheduler remain/);
});


test("Vuo my_Blend exposes TiXL Blend as a stateless Texture2D body adapter", () => {
  const source = fs.readFileSync(nodes.blend, "utf8");

  assert.match(source, /"title"\s*:\s*"my_Blend"/);
  assert.match(source, /Operators\/Lib\/image\/use\/Blend\.cs/);
  assert.match(source, /Primary output: Texture2D \/ VuoImage/);
  assert.match(source, /ColorForTextures #9F008A/);
  assert.match(source, /not a full TiXL Blend clone/);
  assert.match(source, /VuoInputEvent\(\) renderTick/);
  assert.match(source, /VuoInputData\(VuoImage[^)]*"name":"Background"[^)]*\) Background/);
  assert.match(source, /VuoInputData\(VuoImage[^)]*"name":"Foreground"[^)]*\) Foreground/);
  assert.match(source, /VuoInputData\(VuoReal[^)]*"default":0\.75[^)]*\) ForegroundOpacity/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"Blended"[^)]*\) Blended/);
  assert.match(source, /VuoShader_setUniform_VuoImage\s+\(\(\*instance\)->shader, "background"/);
  assert.match(source, /VuoImageRenderer_render/);
});

test("Vuo my_KeepPreviousFrame exposes stateful current and previous frame outputs", () => {
  const source = fs.readFileSync(nodes.keepPreviousFrame, "utf8");

  assert.match(source, /"title"\s*:\s*"my_KeepPreviousFrame"/);
  assert.match(source, /Operators\/Lib\/image\/use\/KeepPreviousFrame\.cs/);
  assert.match(source, /Primary output: Texture2D \/ VuoImage/);
  assert.match(source, /Stateful body-layer adapter/);
  assert.match(source, /VuoInputEvent\(\) update/);
  assert.match(source, /VuoInputData\(VuoImage[^)]*"name":"ImageA"[^)]*\) ImageA/);
  assert.match(source, /VuoInputData\(VuoBoolean[^)]*"default":true[^)]*\) Keep/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"CurrentFrame"[^)]*\) CurrentFrame/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"PreviousFrame"[^)]*\) PreviousFrame/);
  assert.match(source, /previousValid/);
  assert.match(source, /VuoRetain\(ImageA\)/);
  assert.match(source, /VuoRelease\(\(\*instance\)->currentFrame\)/);
});

test("Vuo my_RenderTarget wraps scene rendering without claiming TextureView parity", () => {
  const source = fs.readFileSync(nodes.renderTarget, "utf8");

  assert.match(source, /"title"\s*:\s*"my_RenderTarget"/);
  assert.match(source, /Operators\/Lib\/image\/generate\/basic\/RenderTarget\.cs/);
  assert.match(source, /Vuo body proof: vuo\.scene\.render\.image2/);
  assert.match(source, /does not prove DirectX11 SRV\/UAV\/RTV TextureView parity/);
  assert.match(source, /VuoInputEvent\(\) renderTick/);
  assert.match(source, /VuoInputData\(VuoList_VuoSceneObject[^)]*"name":"Command"[^)]*\) Command/);
  assert.match(source, /VuoInputData\(VuoInteger[^)]*"default":960[^)]*\) Width/);
  assert.match(source, /VuoInputData\(VuoInteger[^)]*"default":540[^)]*\) Height/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"ColorBuffer"[^)]*\) ColorBuffer/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"DepthBuffer"[^)]*\) DepthBuffer/);
  assert.match(source, /VuoSceneRenderer_renderToImage/);
  assert.doesNotMatch(source, /SrvFromTexture2d|UavFromTexture2d|RtvFromTexture2d/);
});

test("Vuo my_ClearRenderTarget makes the first command proof visible without claiming DX11 parity", () => {
  const source = fs.readFileSync(nodes.clearRenderTarget, "utf8");

  assert.match(source, /"title"\s*:\s*"my_ClearRenderTarget"/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/ClearRenderTarget\.cs/);
  assert.match(source, /Primary output in TiXL: Command \/ ColorForCommands #22B8C2/);
  assert.match(source, /Vuo proof output: cleared Texture2D \/ VuoImage plus trace text/);
  assert.match(source, /does not prove RTV\/DSV TextureView identity/);
  assert.match(source, /VuoInputEvent\(\) renderTick/);
  assert.match(source, /VuoInputData\(VuoBoolean[^)]*"name":"TargetValid"[^)]*"default":true[^)]*\) TargetValid/);
  assert.match(source, /VuoInputData\(VuoColor[^)]*"name":"ClearColor"[^)]*\) ClearColor/);
  assert.match(source, /VuoOutputData\(VuoImage[^)]*"name":"ClearedColorBuffer"[^)]*\) ClearedColorBuffer/);
  assert.match(source, /VuoOutputData\(VuoText[^)]*"name":"CommandTrace"[^)]*\) CommandTrace/);
  assert.match(source, /prepare:ClearRenderTarget -> update:clear color buffer -> restore:ClearRenderTarget/);
  assert.match(source, /VuoImage_makeColorImage\(ClearColor, renderWidth, renderHeight\)/);
});

test("High-risk Vuo node lab wires the new custom nodes into visible render windows", () => {
  const source = fs.readFileSync(compositionPath, "utf8");

  assert.match(source, /Blend \[type="my\.image\.use\.blend"/);
  assert.match(source, /KeepPreviousFrame \[type="my\.image\.use\.keepPreviousFrame"/);
  assert.match(source, /RenderTarget \[type="my\.image\.generate\.basic\.renderTarget"/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> Blend:renderTick/);
  assert.match(source, /Blend:Blended -> KeepPreviousFrame:ImageA/);
  assert.match(source, /KeepPreviousFrame:PreviousFrame -> RenderBlendWindow:image/);
  assert.match(source, /RenderTarget:ColorBuffer -> RenderTargetWindow:image/);
  assert.match(source, /CLI-safe comments only/);
});

test("ConstantImage pipeline composition wires all requested proof lines", () => {
  const source = fs.readFileSync(constantPipelineCompositionPath, "utf8");

  assert.match(source, /MainClock \[type="my\.runtime\.clock\.mainClock"/);
  assert.match(source, /ConstantBackground \[type="my\.image\.generate\.basic\.constantImage"/);
  assert.match(source, /ConstantForeground \[type="my\.image\.generate\.basic\.constantImage"/);
  assert.match(source, /Blend \[type="my\.image\.use\.blend"/);
  assert.match(source, /KeepPreviousFrame \[type="my\.image\.use\.keepPreviousFrame"/);
  assert.match(source, /RenderTarget \[type="my\.image\.generate\.basic\.renderTarget"/);
  assert.match(source, /ClearRenderTarget \[type="my\.render\.dx11\.api\.clearRenderTarget"/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MainClock:HostTime/);
  assert.match(source, /MainClock:renderTick -> ConstantBackground:renderTick/);
  assert.match(source, /ConstantBackground:Image -> Blend:Background/);
  assert.match(source, /ConstantForeground:Image -> Blend:Foreground/);
  assert.match(source, /MainClock:renderTick -> Blend:renderTick/);
  assert.match(source, /Blend:Blended -> KeepPreviousFrame:ImageA/);
  assert.match(source, /MainClock:renderTick -> KeepPreviousFrame:update/);
  assert.match(source, /KeepPreviousFrame:CurrentFrame -> RenderBlendWindow:image/);
  assert.match(source, /MainClock:renderTick -> RenderTarget:renderTick/);
  assert.match(source, /MakeSceneList:list -> RenderTarget:Command/);
  assert.match(source, /RenderTarget:ColorBuffer -> RenderTargetWindow:image/);
  assert.match(source, /MainClock:renderTick -> ClearRenderTarget:renderTick/);
  assert.match(source, /ClearRenderTarget:ClearedColorBuffer -> RenderClearWindow:image/);
  assert.doesNotMatch(source, /FireOnDisplayRefresh:requestedFrame -> ConstantBackground:renderTick/);
  assert.doesNotMatch(source, /FireOnDisplayRefresh:requestedFrame -> Blend:renderTick/);
  assert.doesNotMatch(source, /FireOnDisplayRefresh:requestedFrame -> RenderTarget:renderTick/);
  assert.match(source, /CLI-safe comments only/);
});

test("ConstantImage contract preserves the my-world donor fixture and Vuo acceptance lines", () => {
  const fixture = JSON.parse(fs.readFileSync(constantFixturePath, "utf8"));
  const contract = fs.readFileSync(constantContractPath, "utf8");

  assert.equal(fixture.sourceDonor, "/Users/chenbaiwei/Projects/my-world/fixtures/runtime/top_constant_to_output.graph.json");
  assert.equal(fixture.graph.nodes[0].type, "image.constant");
  assert.equal(fixture.graph.nodes[0].visibleVuoNode, "my_ConstantImage");
  assert.equal(fixture.graph.nodes[0].browserAlias, "top.constant");
  assert.deepEqual(fixture.graph.nodes[0].params.resolution, [1280, 720]);
  assert.deepEqual(fixture.graph.nodes[0].params.color, [0.02, 0.02, 0.02, 1.0]);
  assert.ok(fixture.vuoProofs.includes("vuo-compositions/myworld-constant-image-pipeline-proof.vuo"));

  assert.match(contract, /my-world donor := \/Users\/chenbaiwei\/Projects\/my-world\/fixtures\/runtime\/top_constant_to_output\.graph\.json/);
  assert.match(contract, /runtime type := image\.constant/);
  assert.match(contract, /browser alias := top\.constant/);
  assert.match(contract, /Vuo visible node := my_ConstantImage/);
  assert.match(contract, /my_MainClock -> all render\/cook event ports/);
  assert.match(contract, /my_ConstantImage -> my_Blend/);
  assert.match(contract, /my_ConstantImage -> my_KeepPreviousFrame/);
  assert.match(contract, /my_ConstantImage -> my_RenderTarget/);
  assert.match(contract, /my_ClearRenderTarget -> Render Image to Window/);
  assert.match(contract, /does not prove DX11 RTV\/DSV identity or command stream prepare\/update\/restore parity/);
});
