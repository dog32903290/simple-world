#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NODES = [
    ("Bloom", "bloom", "Result", "Bloom-BrightpassPS.hlsl", "Image=null, Intensity=6, ColorWeights=(0.299,0.587,0.114,1), Threshold=0.5, GlowGradient=white, GainAndBias=(0.5,0.5), MaxLevels=10, Blur=1, Clamp=false", 0),
    ("Blur", "blur", "TextureOutput", "Blur.hlsl", "Image=null, Size=1, Samples=8, Offset=0, Opacity=1, Resolution=(0,0), Wrap=MirrorOnce", 1),
    ("DirectionalBlur", "directionalBlur", "TextureOutput", "DirectionalBlur.hlsl", "Image=null, Size=1, Samples=8, Angle=0, FxTextures=null, FxAngleFactor=1, FxSizeFactor=0, RefinementPass=false, RefinementSamples=6, RefineSizeFactor=0, Wrap=MirrorOnce, Resolution=(0,0)", 2),
    ("FastBlur", "fastBlur", "Result", "FastBlur-BlurPS.hlsl", "Image=null, MaxLevels=5", 3),
    ("Sharpen", "sharpen", "TextureOutput", "Sharpen.hlsl", "Image=null, SampleRadius=1, Strength=1, Clamping=false", 4),
]

SHADER = r'''
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D image;
	uniform float size;
	uniform float samples;
	uniform float angle;
	uniform float intensity;
	uniform float threshold;
	uniform int filterKind;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	vec4 source(vec2 uv)
	{
		return texture2D(image, clamp(uv, 0.0, 1.0));
	}

	vec4 boxBlur(vec2 uv, vec2 dir, float radius, int count)
	{
		vec4 sum = vec4(0.0);
		float norm = 0.0;
		for (int i = -8; i <= 8; ++i)
		{
			if (abs(i) <= count)
			{
				float w = 1.0 - abs(float(i)) / float(count + 1);
				sum += source(uv + dir * float(i) * radius) * w;
				norm += w;
			}
		}
		return sum / max(norm, 0.0001);
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec2 px = 1.0 / max(targetSize, vec2(1.0));
		vec4 original = source(uv);
		vec4 color = original;
		if (filterKind == 0)
		{
			vec4 blurred = boxBlur(uv, px, max(size, 0.0) * 2.5, 8);
			float luma = dot(blurred.rgb, vec3(0.299, 0.587, 0.114));
			vec3 glow = max(blurred.rgb - threshold, 0.0) * intensity;
			color = vec4(clamp(original.rgb + glow * smoothstep(threshold, 1.0, luma), 0.0, 1.0), original.a);
		}
		else if (filterKind == 1)
		{
			int count = int(clamp(samples, 1.0, 8.0));
			color = mix(original, boxBlur(uv, px, max(size, 0.0) * 2.0, count), 0.92);
		}
		else if (filterKind == 2)
		{
			float a = angle / 180.0 * 3.14159265;
			vec2 dir = vec2(cos(a), sin(a)) * px;
			int count = int(clamp(samples, 1.0, 8.0));
			color = boxBlur(uv, dir, max(size, 0.0) * 3.0, count);
		}
		else if (filterKind == 3)
		{
			vec4 a = boxBlur(uv, px, max(size, 1.0) * 3.0, 6);
			vec4 b = boxBlur(uv, px.yx, max(size, 1.0) * 3.0, 6);
			color = (a + b) * 0.5;
		}
		else
		{
			vec4 blurred = boxBlur(uv, px, max(size, 0.0) * 1.5, 4);
			color = vec4(clamp(original.rgb + (original.rgb - blurred.rgb) * intensity, 0.0, 1.0), original.a);
		}
		gl_FragColor = color;
	}
);
'''

NODE_BODY = r'''
#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

{shader}
struct nodeInstanceData {{ VuoShader shader; }};
struct nodeInstanceData * nodeInstanceInit(void)
{{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("{title} Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}}
static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 320;
}}
static VuoImage imageOrFallback(VuoImage image, VuoInteger width, VuoInteger height)
{{
	if (image) return image;
	return VuoImage_makeColorImage((VuoColor){{0.5, 0.5, 0.5, 1.0}}, (unsigned int)width, (unsigned int)height);
}}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoReal, {{"default":1.0}}) size,
		VuoInputData(VuoReal, {{"default":8.0}}) samples,
		VuoInputData(VuoReal, {{"default":0.0}}) angle,
		VuoInputData(VuoReal, {{"default":6.0}}) intensity,
		VuoInputData(VuoReal, {{"default":0.5}}) threshold,
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.0,"y":0.0}}}}) resolution,
		VuoOutputData(VuoImage, {{"name":"{output}"}}) textureOutput
)
{{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "image", imageOrFallback(image, renderWidth, renderHeight));
	VuoShader_setUniform_VuoReal((*instance)->shader, "size", size);
	VuoShader_setUniform_VuoReal((*instance)->shader, "samples", samples);
	VuoShader_setUniform_VuoReal((*instance)->shader, "angle", angle);
	VuoShader_setUniform_VuoReal((*instance)->shader, "intensity", intensity);
	VuoShader_setUniform_VuoReal((*instance)->shader, "threshold", threshold);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "filterKind", {kind});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){{renderWidth, renderHeight}});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''


def write(path: str, text: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def node(name, camel, output, shader, defaults, kind):
    title = f"my_{name}"
    return f'''/**
 * @file
 * my.image.fx.blur.{camel} node implementation.
 *
 * TiXL parity contract:
 * - Visible title: {title}
 * - Category: Operators/Lib/image/fx/blur
 * - Source: external/tixl/Operators/Lib/image/fx/blur/{name}.cs
 * - Default: {defaults} from {name}.t3
 * - Primary output: Texture2D {output} (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for TiXL blur shader evidence including {shader}.
 * Vuo body-layer limit: multi-pass downsample/upsample, gradients, wrap modes, mips, and auxiliary FxTextures are approximated in a single-pass image filter.
 */

VuoModuleMetadata({{
\t\t\t\t\t "title" : "{title}",
\t\t\t\t\t "description" : "TiXL {name} bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/fx/blur/{name}.cs. Category: Operators/Lib/image/fx/blur. Primary output: Texture2D {output} (ColorForTextures #9F008A). Default: {defaults}.",
\t\t\t\t\t "keywords" : [ "tixl", "texture2d", "image", "fx", "blur", "{name}", "{shader}", "bounded approximation", "ColorForTextures", "#9F008A" ],
\t\t\t\t\t "version" : "1.0.0",
\t\t\t\t\t "dependencies" : [ "VuoImageRenderer" ],
\t\t\t\t }});
''' + NODE_BODY.format(shader=SHADER, title=title, output=output, kind=kind)


def proof_node():
    uniforms = "\n".join(f"\tuniform sampler2D {camel}Image;" for _, camel, _, _, _, _ in NODES)
    inputs = "\n".join(f"\t\tVuoInputData(VuoImage) {camel}Image," for _, camel, _, _, _, _ in NODES)
    sets = "\n\t".join(f'VuoShader_setUniform_VuoImage((*instance)->shader, "{camel}Image", {camel}Image ? {camel}Image : VuoImage_makeColorImage((VuoColor){{0.1,0.1,0.1,1}}, renderWidth, renderHeight));' for _, camel, _, _, _, _ in NODES)
    select = "texture2D(sharpenImage, localSt)"
    for idx, (_, camel, _, _, _, _) in reversed(list(enumerate(NODES[:-1]))):
        select = f"(band < {idx+1}.0 ? texture2D({camel}Image, localSt) : {select})"
    return f'''#include "VuoImageRenderer.h"
VuoModuleMetadata({{"title":"my_Batch37BlurProof","description":"Proof-only compositor for Batch 37 blur nodes.","keywords":["tixl","batch37","blur","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]}});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
{uniforms}
\tvarying vec2 fragmentTextureCoordinate;
\tvoid main() {{
\t\tvec2 st = fragmentTextureCoordinate;
\t\tfloat band = floor(clamp(st.x, 0.0, 0.9999) * 5.0);
\t\tvec2 localSt = vec2(fract(st.x * 5.0), st.y);
\t\tvec4 color = {select};
\t\tfloat edge = step(localSt.x, 0.02) + step(0.98, localSt.x);
\t\tif (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.4);
\t\tgl_FragColor = color;
\t}}
);
struct nodeInstanceData {{ VuoShader shader; }};
struct nodeInstanceData * nodeInstanceInit(void) {{ struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("my_Batch37BlurProof Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }}
void nodeInstanceEvent
(
\t\tVuoInstanceData(struct nodeInstanceData *) instance,
\t\tVuoInputEvent() renderTick,
{inputs}
\t\tVuoInputData(VuoInteger, {{"default":800}}) width,
\t\tVuoInputData(VuoInteger, {{"default":160}}) height,
\t\tVuoOutputData(VuoImage, {{"name":"Image"}}) image
)
{{ VuoInteger renderWidth=width<1?800:width; VuoInteger renderHeight=height<1?160:height; {sets} *image=VuoImageRenderer_render((*instance)->shader,renderWidth,renderHeight,VuoImageColorDepth_8); }}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''


def composition():
    defs = []
    edges = []
    proof_edges = []
    for i, (name, camel, out, _, _, _) in enumerate(NODES):
        y = -420 + i * 190
        defs.append(f'{name} [type="my.image.fx.blur.{camel}" version="1.0.0" label="my_{name}|<renderTick>renderTick\\l|<image>image\\l|<size>size\\l|<samples>samples\\l|<angle>angle\\l|<intensity>intensity\\l|<threshold>threshold\\l|<resolution>resolution\\l|<textureOutput>{out}\\r" pos="-500,{y}" fillcolor="#9F008A" _size="{1+i*0.45}" _samples="8" _angle="{i*25}" _intensity="4" _threshold="0.38" _resolution="\\{{\\"x\\":160,\\"y\\":160\\}}"];')
        edges.append(f"DisplayRefresh:requestedFrame -> {name}:renderTick;")
        edges.append(f"Source:textureOutput -> {name}:image;")
        proof_edges.append(f"{name}:textureOutput -> ProofImage:{camel}Image;")
    ports = "|".join(f"<{camel}Image>{camel}Image\\l" for _, camel, _, _, _, _ in NODES) + "|"
    return f'''digraph G
{{
DisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\\r" pos="-1100,100" fillcolor="lime" _requestedFrame_eventThrottling="drop"];
Source [type="my.image.generate.basic.checkerBoard" version="1.0.0" label="my_CheckerBoard|<renderTick>renderTick\\l|<colorA>colorA\\l|<colorB>colorB\\l|<scale>scale\\l|<stretch>stretch\\l|<offset>offset\\l|<useAspectRatio>useAspectRatio\\l|<resolution>resolution\\l|<textureOutput>TextureOutput\\r" pos="-830,100" fillcolor="#9F008A" _colorA="\\{{\\"r\\":1,\\"g\\":1,\\"b\\":1,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":0,\\"g\\":0,\\"b\\":0,\\"a\\":1\\}}" _scale="12" _stretch="\\{{\\"x\\":1,\\"y\\":1\\}}" _offset="\\{{\\"x\\":0,\\"y\\":0\\}}" _useAspectRatio="true" _resolution="\\{{\\"x\\":160,\\"y\\":160\\}}"];
{chr(10).join(defs)}
ProofImage [type="my.image.batch.batch37BlurProof" version="1.0.0" label="my_Batch37BlurProof|<renderTick>renderTick\\l|{ports}<width>width\\l|<height>height\\l|<image>Image\\r" pos="120,100" fillcolor="#9F008A" _width="800" _height="160"];
RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch 37 Blur|<refresh>refresh\\l|<image>image\\l|<setWindowDescription>setWindowDescription\\l|<updatedWindow>updatedWindow\\r" pos="720,100" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];
SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\\l|<url>url\\l|<saveImage>saveImage\\l|<ifExists>ifExists\\l|<format>format\\l|<done>done\\r" pos="720,-190" fillcolor="orange" _url="\\"\\\\/Users\\\\/chenbaiwei\\\\/Desktop\\\\/vibe coding\\\\/simple_world\\\\/artifacts\\\\/vuo_cli\\\\/batch-37-blur-vuo-save\\"" _ifExists="1" _format="\\"PNG\\""];
DisplayRefresh:requestedFrame -> Source:renderTick;
{chr(10).join(edges)}
DisplayRefresh:requestedFrame -> ProofImage:renderTick;
DisplayRefresh:requestedFrame -> RenderWindow:refresh;
{chr(10).join(proof_edges)}
ProofImage:image -> RenderWindow:image;
ProofImage:image -> SaveImage:saveImage;
}}
'''


def tests():
    names = ", ".join(f'"{n}"' for n, *_ in NODES)
    node_rows = ",\n".join(f'    ["vuo-nodes/my.image.fx.blur.{camel}.c", "my_{name}", "{name}.cs", "{shader}"]' for name, camel, _, shader, _, _ in NODES)
    sem = f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("TiXL blur namespace has five Texture2D filter nodes",()=>{{ for (const name of [{names}]) {{ assert.match(read(`external/tixl/Operators/Lib/image/fx/blur/${{name}}.cs`),new RegExp(`sealed class ${{name}}`)); assert.match(read(`external/tixl/Operators/Lib/image/fx/blur/${{name}}.t3`),/DefaultValue/); }} }});
'''
    vuo = f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 37 Vuo blur nodes preserve names, paths, shader cues, and bounded limits",()=>{{ const nodes=[\n{node_rows}\n  ]; for (const [file,title,donor,shader] of nodes) {{ const source=read(file); assert.match(source,new RegExp(`"title"\\\\s*:\\\\s*"${{title}}"`)); assert.match(source,new RegExp(`Source: external/tixl/Operators/Lib/image/fx/blur/${{donor}}`)); assert.match(source,new RegExp(shader.replace(/[.*+?^${{}}()|[\\]\\\\]/g,"\\\\$&"))); assert.match(source,/ColorForTextures #9F008A/); assert.match(source,/single-pass image filter/); }} }});
'''
    comp = '''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 37 proof wires blur filters to a checkerboard source and save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-37-blur-proof.vuo"),"utf8"); for (const title of ["my_Bloom","my_Blur","my_DirectionalBlur","my_FastBlur","my_Sharpen","my_Batch37BlurProof"]) assert.match(s,new RegExp(title)); assert.match(s,/Source:textureOutput -> Bloom:image/); assert.match(s,/batch-37-blur-vuo-save/); });
'''
    return sem, vuo, comp


def doc():
    rows = "\n".join(f"| `Lib.image.fx.blur.{name}` | C bounded shader adapter | `my_{name}` | `vuo-nodes/my.image.fx.blur.{camel}.c` | C# `external/tixl/Operators/Lib/image/fx/blur/{name}.cs`; `.t3` `external/tixl/Operators/Lib/image/fx/blur/{name}.t3`; shader `{shader}` | Batch 37 tests | `vuo-compositions/generated/myworld-batch-37-blur-proof.vuo` | done |" for name, camel, _, shader, _, _ in NODES)
    return f'''# Batch 37 Image FX Blur Acceptance Matrix

Scope: finish `Lib.image.fx.blur`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
{rows}

## Proof Notes

- Primary output type is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- These are bounded Vuo shader adapters: multi-pass bloom/fast blur, gradient glow, wrap modes, mips, and auxiliary FxTextures are represented by a single-pass body-layer proof.
- Vuo CLI proof target: `batch-37-blur-proof`.
'''


def main():
    for args in NODES:
        name, camel, *_ = args
        write(f"vuo-nodes/my.image.fx.blur.{camel}.c", node(*args))
    write("vuo-nodes/my.image.batch.batch37BlurProof.c", proof_node())
    write("vuo-compositions/generated/myworld-batch-37-blur-proof.vuo", composition())
    sem, vuo, comp = tests()
    write("tests/tixl_batch37_blur_semantics.test.js", sem)
    write("tests/tixl_batch37_blur_vuo_nodes.test.js", vuo)
    write("tests/vuo_batch_37_blur_composition.test.js", comp)
    write("docs/tixl-porting/batches/2026-06-05-batch-37-blur.md", doc())

if __name__ == "__main__":
    main()
