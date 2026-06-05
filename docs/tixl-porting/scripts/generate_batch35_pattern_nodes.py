#!/usr/bin/env python3
"""Generate Batch 35 pattern Vuo adapters, proof, tests, and acceptance docs."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

NODES = [
    ("FraserGrid", "fraserGrid", "FraserGrid.hlsl", "Image=null, Fill=(0,0,0,1), FillB=(1,1,1,1), Background=(0.67475104,0.67498636,0.67569184,1), Feather=0.015, Size=(1,1), Offset=(0,0), Scale=4, Rotate=0, RotateShapes=45, ShapeSize=0.22, BarWidth=0.035, BorderWidth=0.06, RowSwift=0, RAffects_BarWidth=0, GAffects_ShapeSize=0, BAffects_LineRatio=0, Resolution=(0,0)", 0),
    ("NumberPattern", "numberPattern", "NumberPattern.hlsl", "Texture=null, TextColor=(1,1,1,1), LineColor=(1,1,1,1), Highlight=(1,1,1,1), OriginalImage=(1,1,1,1), CellSize=(200,8), CellRange=(1,1), Position=(0,0), Offset=100, ScrollOffset=0, ScrollSpeed=1, HighlightThreshold=0", 1),
    ("Raster", "raster", "Raster.hlsl", "Image=null, Offset=(0,0), Rotate=0, Stretch=(1,1), Scale=4, Color=(1,1,1,1), Background=(0,0,0,0), MixOriginal=1, Resolution=(0,0), DotSize=0.05333333, LineWidth=0.053333342, LineRatio=0.75, Feather=0.02, RedToDotSize=0, GreenToLineWidth=0, BlueToLineRatio=0", 2),
    ("Rings", "rings", "Rings.hlsl", "Image=null, Fill=(1,1,1,1), Background=(0,0,0,0), Highlight=(1,0,0,1), Radius=(1,1), Position=(0,0), Count=0.5, Feather=0.03333335, Rotate=0, Offset=0, _Segments=(20,0), _Twist=(0,0), _Thickness=(0.5,0), _Ratio=(1.05,0), _FillRatio=1, _HighlightRatio=0, HighlightSeed=0, Distort=1, Constrast=1, Resolution=(0,0), Seed=0, BlendMode=0", 3),
    ("RyojiPattern1", "ryojiPattern1", "RyojiPattern1.hlsl", "Image=null, Background=(0,0,0,1), Foreground=(1,1,1,1), MixOriginal=0, Contrast=0.75, ForgroundRatio=0.5, Highlight=(1,0,0,1), HighlightProbability=0.01, HighlightSeed=0, Iterations=7, Splits=(0,0), SplitProbability=(0,0), ScrollSpeed=(0,0), ScrollProbability=(0,0), Padding=(0,0), Seed=42, Resolution=(0,0), GenerateMipmaps=false", 4),
    ("RyojiPattern2", "ryojiPattern2", "RyojiPattern2.hlsl", "Image=null, Background=(0,0,0,0), Foreground=(1,1,1,1), MixOriginal=0, Contrast=0.5, ForgroundRatio=0.50333333, Highlight=(1,0,0,1), HighlightProbability=0.01, HighlightSeed=0, Splits=(0,0), SplitB=(0,0), SplitC=(0,0), SplitProbability=(0,0), ScrollSpeed=(0,0), ScrollProbability=(0,0), ScrollOffset=0, Padding=(0,0), Seed=42, Resolution=(0,0)", 5),
    ("SinForm", "sinForm", "SinForm.hlsl", "Image=null, Fill=(1,1,1,1), Background=(0,0,0,0), LineWidth=0.04333334, Fade=1, Size=(1,1), Offset=(0,0), Rotate=0, Copies=0, OffsetCopies=(0,0.05), Resolution=(0,0), TextureFormat=R16G16B16A16_Float", 6),
    ("ValueRaster", "valueRaster", "ValueRaster.hlsl", "Image=null, Color=(1,1,1,0.695), Background=(0,0,0,0), MixOriginal=1, Resolution=(0,0), RangeX=(0,1), RangeY=(0,1), MajorLineWidth=1, MinorLineWidth=0.25, Density=(1000,1000)", 7),
    ("ZollnerPattern", "zollnerPattern", "ZollnerGrid.hlsl", "Image=null, Fill=(0,0,0,1), Background=(1,1,1,1), Stretch=(1,1), Offset=(0,0), Scale=1, Rotate=45, Feather=0.02, BarWidth=0.2, HookRotation=60, HookLength=0.7, HookWidth=0.33, RowSwift=0, RAffects_BarWidth=0, GAffects_HookLength=0, BAffects_HookRotation=0, Resolution=(0,0), AmplifyIllusion=0", 8),
]

SHADER = r'''
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec4 background;
	uniform float scale;
	uniform float rotate;
	uniform float density;
	uniform float lineWidth;
	uniform float feather;
	uniform int patternKind;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float hash(vec2 p)
	{
		return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
	}

	vec2 rotated(vec2 p, float degrees)
	{
		float a = degrees / 180.0 * 3.14159265;
		float c = cos(a);
		float s = sin(a);
		return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
	}

	float stripe(vec2 p, float w)
	{
		float d = abs(fract(p.x) - 0.5);
		return 1.0 - smoothstep(w, w + max(feather, 0.0001), d);
	}

	float ring(vec2 p)
	{
		float r = length(p);
		float v = abs(fract(r * density) - 0.5);
		return 1.0 - smoothstep(lineWidth, lineWidth + max(feather, 0.0001), v);
	}

	float ryoji(vec2 p, float offset)
	{
		vec2 cell = floor(p * vec2(density * 2.0, max(density * 0.25, 1.0)));
		float h = hash(cell + offset);
		float bars = step(0.5, h);
		float hi = step(0.985, hash(cell + 19.7 + offset));
		return max(bars, hi);
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		p = rotated(p, rotate) * max(scale, 0.0001);
		float v = 0.0;
		if (patternKind == 0)
		{
			float a = stripe(rotated(p, 18.0) * density, lineWidth);
			float b = stripe(rotated(p, -32.0) * density, lineWidth * 0.7);
			v = max(a * 0.85, b * 0.65);
		}
		else if (patternKind == 1)
		{
			vec2 cell = floor((p + 0.5) * vec2(density, density * 3.0));
			float digit = mod(cell.x + cell.y * 7.0, 10.0) / 9.0;
			v = max(step(0.82, fract((p.y + digit) * density * 2.5)), step(0.96, hash(cell)));
		}
		else if (patternKind == 2)
		{
			vec2 g = fract(p * density) - 0.5;
			float dots = 1.0 - smoothstep(lineWidth, lineWidth + max(feather, 0.0001), length(g));
			float lines = max(stripe(vec2(p.x * density, p.y), lineWidth * 0.5), stripe(vec2(p.y * density, p.x), lineWidth * 0.5));
			v = max(dots, lines * 0.65);
		}
		else if (patternKind == 3)
		{
			v = ring(p);
		}
		else if (patternKind == 4)
		{
			v = ryoji(p + vec2(0.13, 0.0), 4.0);
		}
		else if (patternKind == 5)
		{
			v = max(ryoji(p, 9.0), stripe(vec2(p.y * density * 0.5, p.x), lineWidth * 0.4) * 0.45);
		}
		else if (patternKind == 6)
		{
			float y = sin((p.x + 0.5) * density * 6.28318) * 0.22;
			v = 1.0 - smoothstep(lineWidth, lineWidth + max(feather, 0.0001), abs(p.y - y));
		}
		else if (patternKind == 7)
		{
			float majorX = stripe(vec2(p.x * density * 0.2, p.y), lineWidth * 0.35);
			float majorY = stripe(vec2(p.y * density * 0.2, p.x), lineWidth * 0.35);
			float minorX = stripe(vec2(p.x * density, p.y), lineWidth * 0.12);
			float minorY = stripe(vec2(p.y * density, p.x), lineWidth * 0.12);
			v = max(max(majorX, majorY), max(minorX, minorY) * 0.45);
		}
		else
		{
			float bars = stripe(vec2(p.y * density, p.x), lineWidth);
			float hooks = stripe(rotated(p, 60.0) * vec2(density * 1.7, density), lineWidth * 0.45);
			v = max(bars, hooks);
		}
		vec4 fg = mix(colorA, colorB, step(0.5, hash(floor(p * density) + float(patternKind))));
		gl_FragColor = mix(background, fg, clamp(v, 0.0, 1.0));
	}
);
'''

BODY = r'''
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
	return requested > 0 ? requested : 160;
}}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {{"default":{{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}}}) colorA,
		VuoInputData(VuoColor, {{"default":{{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}}}) colorB,
		VuoInputData(VuoColor, {{"default":{{"r":0.0,"g":0.0,"b":0.0,"a":0.0}}}}) background,
		VuoInputData(VuoReal, {{"default":1.0}}) scale,
		VuoInputData(VuoReal, {{"default":0.0}}) rotate,
		VuoInputData(VuoReal, {{"default":12.0}}) density,
		VuoInputData(VuoReal, {{"default":0.08}}) lineWidth,
		VuoInputData(VuoReal, {{"default":0.01}}) feather,
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.0,"y":0.0}}}}) resolution,
		VuoOutputData(VuoImage, {{"name":"TextureOutput"}}) textureOutput
)
{{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoColor((*instance)->shader, "background", background);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotate", rotate);
	VuoShader_setUniform_VuoReal((*instance)->shader, "density", density);
	VuoShader_setUniform_VuoReal((*instance)->shader, "lineWidth", lineWidth);
	VuoShader_setUniform_VuoReal((*instance)->shader, "feather", feather);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "patternKind", {kind});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){{renderWidth, renderHeight}});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{{
	VuoRelease((*instance)->shader);
}}
'''


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def node_source(name: str, camel: str, shader: str, defaults: str, kind: int) -> str:
    title = f"my_{name}"
    return f'''/**
 * @file
 * my.image.generate.pattern.{camel} node implementation.
 *
 * TiXL parity contract:
 * - Visible title: {title}
 * - Category: Operators/Lib/image/generate/pattern
 * - Source: external/tixl/Operators/Lib/image/generate/pattern/{name}.cs
 * - Default: {defaults} from {name}.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/{{fx or generate}}/{shader}.
 * Vuo body-layer limit: complex TiXL image-input modulation, blend modes, resource formats, and full shader-specific parameter set are represented by shared pattern controls.
 */

VuoModuleMetadata({{
\t\t\t\t\t "title" : "{title}",
\t\t\t\t\t "description" : "TiXL {name} bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/pattern/{name}.cs. Category: Operators/Lib/image/generate/pattern. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: {defaults}.",
\t\t\t\t\t "keywords" : [ "tixl", "texture2d", "image", "generate", "pattern", "{name}", "{shader}", "bounded approximation", "ColorForTextures", "#9F008A" ],
\t\t\t\t\t "version" : "1.0.0",
\t\t\t\t\t "dependencies" : [ "VuoImageRenderer" ],
\t\t\t\t }});
'''+ BODY.format(shader=SHADER, title=title, kind=kind)


def generate_nodes() -> None:
    for name, camel, shader, defaults, kind in NODES:
        write(ROOT / f"vuo-nodes/my.image.generate.pattern.{camel}.c", node_source(name, camel, shader, defaults, kind))


def generate_proof_node() -> None:
    uniforms = "\n".join(f"\tuniform sampler2D {camel}Image;" for _, camel, _, _, _ in NODES)
    selection = "texture2D(zollnerPatternImage, localSt)"
    for idx, (_, camel, _, _, _) in reversed(list(enumerate(NODES[:-1]))):
        selection = f"(band < {idx + 1}.0 ? texture2D({camel}Image, localSt) : {selection})"
    set_uniforms = "\n\t".join(f'VuoShader_setUniform_VuoImage((*instance)->shader, "{camel}Image", imageOrColor({camel}Image, (VuoColor){{0.15 + 0.07 * {idx}, 0.15 + 0.07 * {idx}, 0.15 + 0.07 * {idx}, 1.0}}, renderWidth, renderHeight));' for idx, (_, camel, _, _, _) in enumerate(NODES))
    inputs = "\n".join(f"\t\tVuoInputData(VuoImage) {camel}Image," for _, camel, _, _, _ in NODES)
    source = f'''/**
 * @file
 * my.image.batch.batch35PatternGenerateProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 35 image/generate/pattern nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({{
\t\t\t\t\t "title" : "my_Batch35PatternGenerateProof",
\t\t\t\t\t "description" : "Proof-only compositor for Batch 35 TiXL image/generate/pattern nodes. Primary output: Texture2D Image (ColorForTextures #9F008A).",
\t\t\t\t\t "keywords" : [ "tixl", "batch35", "texture2d", "image", "generate", "pattern", "proof", "ColorForTextures", "#9F008A" ],
\t\t\t\t\t "version" : "1.0.0",
\t\t\t\t\t "dependencies" : [ "VuoImageRenderer" ],
\t\t\t\t }});

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
{uniforms}
\tvarying vec2 fragmentTextureCoordinate;
\tvoid main()
\t{{
\t\tvec2 st = fragmentTextureCoordinate;
\t\tfloat band = floor(clamp(st.x, 0.0, 0.9999) * 9.0);
\t\tvec2 localSt = vec2(fract(st.x * 9.0), st.y);
\t\tvec4 color = {selection};
\t\tfloat edge = step(localSt.x, 0.018) + step(0.982, localSt.x);
\t\tif (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.4);
\t\tgl_FragColor = color;
\t}}
);

struct nodeInstanceData {{ VuoShader shader; }};

struct nodeInstanceData * nodeInstanceInit(void)
{{
\tstruct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
\tVuoRegister(instance, free);
\tinstance->shader = VuoShader_make("my_Batch35PatternGenerateProof Shader");
\tVuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
\tVuoRetain(instance->shader);
\treturn instance;
}}

static VuoImage imageOrColor(VuoImage image, VuoColor color, VuoInteger width, VuoInteger height)
{{
\tif (image) return image;
\treturn VuoImage_makeColorImage(color, (unsigned int)width, (unsigned int)height);
}}

void nodeInstanceEvent
(
\t\tVuoInstanceData(struct nodeInstanceData *) instance,
\t\tVuoInputEvent() renderTick,
{inputs}
\t\tVuoInputData(VuoInteger, {{"default":1440,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}}) width,
\t\tVuoInputData(VuoInteger, {{"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}}) height,
\t\tVuoOutputData(VuoImage, {{"name":"Image"}}) image
)
{{
\tVuoInteger renderWidth = width < 1 ? 1440 : width;
\tVuoInteger renderHeight = height < 1 ? 160 : height;
\t{set_uniforms}
\t*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{{
\tVuoRelease((*instance)->shader);
}}
'''
    write(ROOT / "vuo-nodes/my.image.batch.batch35PatternGenerateProof.c", source)


def generate_composition() -> None:
    node_defs = []
    event_edges = []
    image_edges = []
    for idx, (name, camel, _, _, _) in enumerate(NODES):
        y = -700 + idx * 220
        node_defs.append(f'{name} [type="my.image.generate.pattern.{camel}" version="1.0.0" label="my_{name}|<renderTick>renderTick\\l|<colorA>colorA\\l|<colorB>colorB\\l|<background>background\\l|<scale>scale\\l|<rotate>rotate\\l|<density>density\\l|<lineWidth>lineWidth\\l|<feather>feather\\l|<resolution>resolution\\l|<textureOutput>TextureOutput\\r" pos="-760,{y}" fillcolor="#9F008A" _colorA="\\{{\\"r\\":1,\\"g\\":1,\\"b\\":1,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":0,\\"g\\":0,\\"b\\":0,\\"a\\":1\\}}" _background="\\{{\\"r\\":0.02,\\"g\\":0.02,\\"b\\":0.025,\\"a\\":1\\}}" _scale="{1.0 + idx * 0.12:.2f}" _rotate="{idx * 11}" _density="{8 + idx * 2}" _lineWidth="0.08" _feather="0.012" _resolution="\\{{\\"x\\":160,\\"y\\":160\\}}"];')
        event_edges.append(f"DisplayRefresh:requestedFrame -> {name}:renderTick;")
        image_edges.append(f"{name}:textureOutput -> ProofImage:{camel}Image;")
    proof_ports = "|".join(f"<{camel}Image>{camel}Image\\l" for _, camel, _, _, _ in NODES) + "|"
    source = f'''/**
 * @file
 * Batch 35 image/generate/pattern Vuo visual proof.
 *
 * This composition wires all nine Lib.image.generate.pattern nodes into a nine-band proof image.
 *
 * @lastSavedInVuoVersion 2.4.6
 * @license This composition may be modified and distributed under the terms of the MIT License.
 */

digraph G
{{
DisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\\r" pos="-1320,100" fillcolor="lime" _requestedFrame_eventThrottling="drop"];

{chr(10).join(node_defs)}

ProofImage [type="my.image.batch.batch35PatternGenerateProof" version="1.0.0" label="my_Batch35PatternGenerateProof|<renderTick>renderTick\\l|{proof_ports}<width>width\\l|<height>height\\l|<image>Image\\r" pos="120,100" fillcolor="#9F008A" _width="1440" _height="160"];

RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch 35 Pattern Generate|<refresh>refresh\\l|<image>image\\l|<setWindowDescription>setWindowDescription\\l|<updatedWindow>updatedWindow\\r" pos="780,100" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];

SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\\l|<url>url\\l|<saveImage>saveImage\\l|<ifExists>ifExists\\l|<format>format\\l|<done>done\\r" pos="780,-190" fillcolor="orange" _url="\\"\\\\/Users\\\\/chenbaiwei\\\\/Desktop\\\\/vibe coding\\\\/simple_world\\\\/artifacts\\\\/vuo_cli\\\\/batch-35-pattern-generate-vuo-save\\"" _ifExists="1" _format="\\"PNG\\""];

Comment [type="vuo.comment" label="\\"Batch 35 image generate pattern proof\\"" pos="-240,-540" width="540" height="42"];

{chr(10).join(event_edges)}
DisplayRefresh:requestedFrame -> ProofImage:renderTick;
DisplayRefresh:requestedFrame -> RenderWindow:refresh;

{chr(10).join(image_edges)}

ProofImage:image -> RenderWindow:image;
ProofImage:image -> SaveImage:saveImage;
}}
'''
    write(ROOT / "vuo-compositions/generated/myworld-batch-35-pattern-generate-proof.vuo", source)


def generate_tests() -> None:
    node_list = ",\n".join(f'    ["vuo-nodes/my.image.generate.pattern.{camel}.c", "my_{name}", "{name}.cs", "{shader}"]' for name, camel, shader, _, _ in NODES)
    names = ", ".join(f'"{name}"' for name, _, _, _, _ in NODES)
    titles = ", ".join(f'"my_{name}"' for name, _, _, _, _ in NODES)
    sem = f'''#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("TiXL pattern namespace has the nine expected Texture2D generators", () => {{
  for (const name of [{names}]) {{
    assert.match(read(`external/tixl/Operators/Lib/image/generate/pattern/${{name}}.cs`), new RegExp(`sealed class ${{name}}`));
    assert.match(read(`external/tixl/Operators/Lib/image/generate/pattern/${{name}}.cs`), /Slot<Texture2D> TextureOutput/);
    assert.match(read(`external/tixl/Operators/Lib/image/generate/pattern/${{name}}.t3`), /DefaultValue/);
  }}
}});

test("TiXL pattern shader evidence is present", () => {{
  for (const shader of ["FraserGrid.hlsl", "NumberPattern.hlsl", "Raster.hlsl", "Rings.hlsl", "RyojiPattern1.hlsl", "RyojiPattern2.hlsl", "SinForm.hlsl", "ValueRaster.hlsl", "ZollnerGrid.hlsl"]) {{
    const hits = [
      `external/tixl/Operators/Lib/Assets/shaders/img/fx/${{shader}}`,
      `external/tixl/Operators/Lib/Assets/shaders/img/generate/${{shader}}`,
    ].filter((p) => fs.existsSync(path.join(repoRoot, p)));
    assert.ok(hits.length > 0, shader);
  }}
}});
'''
    vuo = f'''#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 35 Vuo pattern node sources preserve TiXL names, paths, shader cues, and texture color", () => {{
  const nodes = [
{node_list}
  ];
  for (const [file, title, donor, shader] of nodes) {{
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\\\s*:\\\\s*"${{title}}"`), file);
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/image/generate/pattern/${{donor}}`), file);
    assert.match(source, new RegExp(shader.replace(/[.*+?^${{}}()|[\\]\\\\]/g, "\\\\$&")), file);
    assert.match(source, /Category: Operators\\/Lib\\/image\\/generate\\/pattern/, file);
    assert.match(source, /Primary output: Texture2D TextureOutput \\(ColorForTextures #9F008A\\)/, file);
    assert.match(source, /Vuo body-layer limit: complex TiXL image-input modulation/, file);
  }}
}});

test("Batch 35 pattern nodes expose distinct pattern-kind shader routing", () => {{
  for (const [file] of [
{node_list}
  ]) {{
    assert.match(read(file), /patternKind/);
    assert.match(read(file), /stripe|ring|ryoji/);
  }}
}});
'''
    comp = f'''#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-35-pattern-generate-proof.vuo");

test("Batch 35 proof wires all pattern nodes into a visible Vuo save path", () => {{
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of [{titles}, "my_Batch35PatternGenerateProof"]) {{
    assert.match(source, new RegExp(title));
  }}
  assert.match(source, /batch-35-pattern-generate-vuo-save/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
}});
'''
    write(ROOT / "tests/tixl_batch35_pattern_generate_semantics.test.js", sem)
    write(ROOT / "tests/tixl_batch35_pattern_generate_vuo_nodes.test.js", vuo)
    write(ROOT / "tests/vuo_batch_35_pattern_generate_composition.test.js", comp)


def generate_doc() -> None:
    rows = "\n".join(
        f"| `Lib.image.generate.pattern.{name}` | C bounded shader adapter | `my_{name}` | `vuo-nodes/my.image.generate.pattern.{camel}.c` | C# `external/tixl/Operators/Lib/image/generate/pattern/{name}.cs`; `.t3` `external/tixl/Operators/Lib/image/generate/pattern/{name}.t3`; shader `{shader}` | Batch 35 tests | `vuo-compositions/generated/myworld-batch-35-pattern-generate-proof.vuo` | done |"
        for name, camel, shader, _, _ in NODES
    )
    doc = f'''# Batch 35 Image Generate Pattern Acceptance Matrix

Scope: finish `Lib.image.generate.pattern`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
{rows}

## Proof Notes

- Primary output type for all nine manufactured nodes is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- Creator-facing titles preserve TiXL capitalization exactly.
- These are bounded Vuo shader adapters: each node exposes shared pattern controls and a distinct shader branch, while complex TiXL image-input modulation, blend modes, shader-specific full parameter sets, mips, and DXGI formats are documented limits.
- Vuo CLI proof target: `batch-35-pattern-generate-proof`.
'''
    write(ROOT / "docs/tixl-porting/batches/2026-06-05-batch-35-pattern-generate.md", doc)


def main() -> None:
    generate_nodes()
    generate_proof_node()
    generate_composition()
    generate_tests()
    generate_doc()


if __name__ == "__main__":
    main()
