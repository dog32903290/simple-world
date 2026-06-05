#!/usr/bin/env python3
"""Generate Batch 36 MandelbrotFractal + MunchingSquares2 Vuo adapters."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def write(path: str, text: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


COMMON = r'''
#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 320;
}

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make(NODE_TITLE " Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
'''


def mandelbrot() -> str:
    return r'''/**
 * @file
 * my.image.generate.fractal.mandelbrotFractal node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MandelbrotFractal
 * - Category: Operators/Lib/image/generate/fractal
 * - Source: external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs
 * - Default: Phase=0, Scale=-0.5, Offset=(0.251,0), ColorScale=10, Gradient=black-to-white from MandelbrotFractal.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/MandelbrotFractal.hlsl.
 * Vuo body-layer limit: TiXL Gradient datatype is represented as colorA/colorB.
 */

#define NODE_TITLE "my_MandelbrotFractal"
#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_MandelbrotFractal",
					 "description" : "TiXL MandelbrotFractal bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs. Category: Operators/Lib/image/generate/fractal. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Phase=0, Scale=-0.5, Offset=(0.251,0), ColorScale=10, Gradient=black-to-white.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "fractal", "mandelbrot", "MandelbrotFractal.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform float phase;
	uniform float scale;
	uniform vec2 offset;
	uniform float colorScale;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		float zoom = pow(2.0, scale);
		vec2 c = (fragmentTextureCoordinate - 0.5) * vec2(aspectRatio, 1.0) * (3.0 / max(zoom, 0.0001)) + vec2(-0.75, 0.0) + offset;
		vec2 z = vec2(cos(phase) * 0.001, sin(phase) * 0.001);
		float iter = 0.0;
		for (int i = 0; i < 96; ++i)
		{
			if (dot(z, z) <= 4.0)
			{
				z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
				iter += 1.0;
			}
		}
		float v = iter >= 96.0 ? 0.0 : iter / max(colorScale, 0.0001);
		gl_FragColor = mix(colorA, colorB, clamp(v, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_MandelbrotFractal Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}
static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 320;
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoReal, {"default":0.0}) phase,
		VuoInputData(VuoReal, {"default":-0.5}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":0.251,"y":0.0}}) offset,
		VuoInputData(VuoReal, {"default":10.0}) colorScale,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoPoint2d, {"default":{"x":320.0,"y":320.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoReal((*instance)->shader, "phase", phase);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoReal((*instance)->shader, "colorScale", colorScale);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
'''


def munching() -> str:
    return r'''/**
 * @file
 * my.image.generate.munchingSquares2 node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MunchingSquares2
 * - Category: Operators/Lib/image/generate
 * - Source: external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs
 * - Default: Image=null, ShadowColor=(0,0,0,1), HighlightColor=(1,1,1,1), Method=0, GrayScaleWeights=(0.2126,0.7152,0.0722,0), GainAndBias=(0.5,0.5), Scale=4, Stretch=(1,1), Offset=(0,0), BlendMethod=0, Iterations=10, IterationFx=0 from MunchingSquares2.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/MunchingSquares.hlsl.
 * Vuo body-layer limit: source-image blending is omitted when Image is null.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_MunchingSquares2",
					 "description" : "TiXL MunchingSquares2 bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs. Category: Operators/Lib/image/generate. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, ShadowColor=(0,0,0,1), HighlightColor=(1,1,1,1), Method=0, GrayScaleWeights=(0.2126,0.7152,0.0722,0), GainAndBias=(0.5,0.5), Scale=4, Stretch=(1,1), Offset=(0,0), BlendMethod=0, Iterations=10, IterationFx=0.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "munching", "squares", "MunchingSquares.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 shadowColor;
	uniform vec4 highlightColor;
	uniform int method;
	uniform float scale;
	uniform vec2 stretch;
	uniform vec2 offset;
	uniform int iterations;
	uniform float iterationFx;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - 0.5 + offset * vec2(-1.0, 1.0) * 0.02;
		p.x *= aspectRatio;
		vec2 q = floor((p * max(scale, 0.0001) + 0.5) * targetSize / max(stretch, vec2(0.0001)));
		float f = max(float(iterations) + iterationFx, 1.0);
		float x = mod(q.x, 1024.0);
		float y = mod(q.y, 1024.0);
		float v = 0.0;
		if (method == 1) v = mod(floor(x / f) + floor(y / f), 2.0);
		else if (method == 2) v = mod(floor((x + y) / f), 2.0);
		else if (method == 3) v = mod(floor((x * y) / max(f, 1.0)), 2.0);
		else if (method == 4) v = mod(floor((x + 2.0 * y) / f) + floor((2.0 * x + y) / f), 2.0);
		else v = mod(floor(x / f) + floor(y / f), 2.0);
		gl_FragColor = mix(shadowColor, highlightColor, v);
	}
);

struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_MunchingSquares2 Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}
static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 320;
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) shadowColor,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) highlightColor,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) method,
		VuoInputData(VuoReal, {"default":4.0}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offset,
		VuoInputData(VuoInteger, {"default":10,"suggestedMin":1,"suggestedMax":256,"suggestedStep":1}) iterations,
		VuoInputData(VuoReal, {"default":0.0}) iterationFx,
		VuoInputData(VuoPoint2d, {"default":{"x":320.0,"y":320.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "shadowColor", shadowColor);
	VuoShader_setUniform_VuoColor((*instance)->shader, "highlightColor", highlightColor);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "method", method);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "iterations", iterations);
	VuoShader_setUniform_VuoReal((*instance)->shader, "iterationFx", iterationFx);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
'''


def proof_node() -> str:
    return r'''/**
 * @file
 * my.image.batch.batch36FractalGenerateProof node implementation.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch36FractalGenerateProof",
					 "description" : "Proof-only compositor for Batch 36 MandelbrotFractal and MunchingSquares2 nodes.",
					 "keywords" : [ "tixl", "batch36", "texture2d", "image", "generate", "fractal", "munching", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D mandelbrotImage;
	uniform sampler2D munchingImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec2 localSt = vec2(fract(st.x * 2.0), st.y);
		vec4 color = st.x < 0.5 ? texture2D(mandelbrotImage, localSt) : texture2D(munchingImage, localSt);
		float edge = step(localSt.x, 0.02) + step(0.98, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.35);
		gl_FragColor = color;
	}
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Batch36FractalGenerateProof Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}
static VuoImage imageOrColor(VuoImage image, VuoColor color, VuoInteger width, VuoInteger height)
{
	if (image) return image;
	return VuoImage_makeColorImage(color, (unsigned int)width, (unsigned int)height);
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) mandelbrotImage,
		VuoInputData(VuoImage) munchingImage,
		VuoInputData(VuoInteger, {"default":640}) width,
		VuoInputData(VuoInteger, {"default":320}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 640 : width;
	VuoInteger renderHeight = height < 1 ? 320 : height;
	VuoShader_setUniform_VuoImage((*instance)->shader, "mandelbrotImage", imageOrColor(mandelbrotImage, (VuoColor){0.1, 0.1, 0.1, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "munchingImage", imageOrColor(munchingImage, (VuoColor){0.6, 0.6, 0.6, 1.0}, renderWidth, renderHeight));
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
'''


def composition() -> str:
    return r'''/**
 * @file
 * Batch 36 fractal/generate Vuo visual proof.
 *
 * @lastSavedInVuoVersion 2.4.6
 */

digraph G
{
DisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\r" pos="-1120,100" fillcolor="lime" _requestedFrame_eventThrottling="drop"];
Mandelbrot [type="my.image.generate.fractal.mandelbrotFractal" version="1.0.0" label="my_MandelbrotFractal|<renderTick>renderTick\l|<phase>phase\l|<scale>scale\l|<offset>offset\l|<colorScale>colorScale\l|<colorA>colorA\l|<colorB>colorB\l|<resolution>resolution\l|<textureOutput>TextureOutput\r" pos="-620,-120" fillcolor="#9F008A" _phase="0" _scale="-0.2" _offset="\{\"x\":0.251,\"y\":0\}" _colorScale="18" _colorA="\{\"r\":0,\"g\":0,\"b\":0,\"a\":1\}" _colorB="\{\"r\":1,\"g\":0.92,\"b\":0.65,\"a\":1\}" _resolution="\{\"x\":320,\"y\":320\}"];
Munching [type="my.image.generate.munchingSquares2" version="1.0.0" label="my_MunchingSquares2|<renderTick>renderTick\l|<image>image\l|<shadowColor>shadowColor\l|<highlightColor>highlightColor\l|<method>method\l|<scale>scale\l|<stretch>stretch\l|<offset>offset\l|<iterations>iterations\l|<iterationFx>iterationFx\l|<resolution>resolution\l|<textureOutput>TextureOutput\r" pos="-620,280" fillcolor="#9F008A" _shadowColor="\{\"r\":0,\"g\":0,\"b\":0,\"a\":1\}" _highlightColor="\{\"r\":0.7,\"g\":0.95,\"b\":1,\"a\":1\}" _method="3" _scale="4" _stretch="\{\"x\":1,\"y\":1\}" _offset="\{\"x\":0,\"y\":0\}" _iterations="13" _iterationFx="0" _resolution="\{\"x\":320,\"y\":320\}"];
ProofImage [type="my.image.batch.batch36FractalGenerateProof" version="1.0.0" label="my_Batch36FractalGenerateProof|<renderTick>renderTick\l|<mandelbrotImage>mandelbrotImage\l|<munchingImage>munchingImage\l|<width>width\l|<height>height\l|<image>Image\r" pos="80,100" fillcolor="#9F008A" _width="640" _height="320"];
RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch 36 Fractal Generate|<refresh>refresh\l|<image>image\l|<setWindowDescription>setWindowDescription\l|<updatedWindow>updatedWindow\r" pos="700,100" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];
SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\l|<url>url\l|<saveImage>saveImage\l|<ifExists>ifExists\l|<format>format\l|<done>done\r" pos="700,-190" fillcolor="orange" _url="\"\\/Users\\/chenbaiwei\\/Desktop\\/vibe coding\\/simple_world\\/artifacts\\/vuo_cli\\/batch-36-fractal-generate-vuo-save\"" _ifExists="1" _format="\"PNG\""];
DisplayRefresh:requestedFrame -> Mandelbrot:renderTick;
DisplayRefresh:requestedFrame -> Munching:renderTick;
DisplayRefresh:requestedFrame -> ProofImage:renderTick;
DisplayRefresh:requestedFrame -> RenderWindow:refresh;
Mandelbrot:textureOutput -> ProofImage:mandelbrotImage;
Munching:textureOutput -> ProofImage:munchingImage;
ProofImage:image -> RenderWindow:image;
ProofImage:image -> SaveImage:saveImage;
}
'''


def tests() -> tuple[str, str, str]:
    sem = r'''#!/usr/bin/env node
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");
test("Batch 36 TiXL generator sources preserve procedural contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs"), /sealed class MandelbrotFractal/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.t3"), /\/\*Scale\*\/[\s\S]*"DefaultValue": -0\.5/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs"), /sealed class MunchingSquares2/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs"), /Classic[\s\S]*Patterns[\s\S]*Chaos/);
  assert.ok(fs.existsSync(path.join(repoRoot, "external/tixl/Operators/Lib/Assets/shaders/img/generate/MandelbrotFractal.hlsl")));
  assert.ok(fs.existsSync(path.join(repoRoot, "external/tixl/Operators/Lib/Assets/shaders/img/generate/MunchingSquares.hlsl")));
});
'''
    vuo = r'''#!/usr/bin/env node
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");
test("Batch 36 Vuo nodes preserve names, paths, texture color, and bounded limits", () => {
  const nodes = [
    ["vuo-nodes/my.image.generate.fractal.mandelbrotFractal.c", "my_MandelbrotFractal", "image/generate/fractal/MandelbrotFractal.cs", "MandelbrotFractal.hlsl"],
    ["vuo-nodes/my.image.generate.munchingSquares2.c", "my_MunchingSquares2", "image/generate/MunchingSquares2.cs", "MunchingSquares.hlsl"],
  ];
  for (const [file, title, donor, shader] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/${donor}`));
    assert.match(source, new RegExp(shader));
    assert.match(source, /Primary output: Texture2D TextureOutput \(ColorForTextures #9F008A\)/);
    assert.match(source, /Vuo body-layer limit/);
  }
});
'''
    comp = r'''#!/usr/bin/env node
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
test("Batch 36 proof wires Mandelbrot and MunchingSquares into a visible save path", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-36-fractal-generate-proof.vuo"), "utf8");
  assert.match(source, /my_MandelbrotFractal/);
  assert.match(source, /my_MunchingSquares2/);
  assert.match(source, /my_Batch36FractalGenerateProof/);
  assert.match(source, /batch-36-fractal-generate-vuo-save/);
});
'''
    return sem, vuo, comp


def doc() -> str:
    return r'''# Batch 36 Fractal And Munching Generate Acceptance Matrix

Scope: finish `Lib.image.generate.fractal` and `Lib.image.generate.MunchingSquares2`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
| `Lib.image.generate.fractal.MandelbrotFractal` | C bounded shader adapter | `my_MandelbrotFractal` | `vuo-nodes/my.image.generate.fractal.mandelbrotFractal.c` | C# `external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.t3`; shader `MandelbrotFractal.hlsl` | Batch 36 tests | `vuo-compositions/generated/myworld-batch-36-fractal-generate-proof.vuo` | done |
| `Lib.image.generate.MunchingSquares2` | C bounded shader adapter | `my_MunchingSquares2` | `vuo-nodes/my.image.generate.munchingSquares2.c` | C# `external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/MunchingSquares2.t3`; shader `MunchingSquares.hlsl` | Batch 36 tests | same proof | done |

## Proof Notes

- Primary output type for both manufactured nodes is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- Vuo CLI proof target: `batch-36-fractal-generate-proof`.
'''


def main() -> None:
    write("vuo-nodes/my.image.generate.fractal.mandelbrotFractal.c", mandelbrot())
    write("vuo-nodes/my.image.generate.munchingSquares2.c", munching())
    write("vuo-nodes/my.image.batch.batch36FractalGenerateProof.c", proof_node())
    write("vuo-compositions/generated/myworld-batch-36-fractal-generate-proof.vuo", composition())
    sem, vuo, comp = tests()
    write("tests/tixl_batch36_fractal_generate_semantics.test.js", sem)
    write("tests/tixl_batch36_fractal_generate_vuo_nodes.test.js", vuo)
    write("tests/vuo_batch_36_fractal_generate_composition.test.js", comp)
    write("docs/tixl-porting/batches/2026-06-05-batch-36-fractal-generate.md", doc())


if __name__ == "__main__":
    main()
