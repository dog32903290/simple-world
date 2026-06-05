#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

NODES = [
    {
        "name": "GlitchDisplace",
        "camel": "glitchDisplace",
        "output": "Output2",
        "input": "image",
        "shader": "GlitchDisplace.hlsl",
        "shader_path": "Operators/Lib/Assets/shaders/points/draw/GlitchDisplace.hlsl",
        "defaults": "Image=null, Rows=300, Columns=25, Size=1, Amount=1, Threshold=0, Stretch=(3,0.5), Offset=(0.5,0), Scatter=(0.1,0.1), Colorize=(1,0,0.8917217,1), ColorRatio=0.1, Seed=0, Mode=3",
        "kind": 0,
        "inputs": [
            ("VuoInteger", "rows", '{"default":300,"suggestedMin":1,"suggestedMax":600}'),
            ("VuoInteger", "columns", '{"default":25,"suggestedMin":1,"suggestedMax":120}'),
            ("VuoReal", "size", '{"default":1.0}'),
            ("VuoReal", "amount", '{"default":1.0}'),
            ("VuoReal", "threshold", '{"default":0.0}'),
            ("VuoPoint2d", "stretch", '{"default":{"x":3.0,"y":0.5}}'),
            ("VuoPoint2d", "offset", '{"default":{"x":0.5,"y":0.0}}'),
            ("VuoPoint2d", "scatter", '{"default":{"x":0.1,"y":0.1}}'),
            ("VuoColor", "colorize", '{"default":{"r":1.0,"g":0.0,"b":0.8917217,"a":1.0}}'),
            ("VuoReal", "colorRatio", '{"default":0.1}'),
            ("VuoInteger", "seed", '{"default":0}'),
            ("VuoInteger", "mode", '{"default":3}'),
        ],
        "sets": [
            ("VuoInteger", "rows"),
            ("VuoInteger", "columns"),
            ("VuoReal", "size"),
            ("VuoReal", "amount"),
            ("VuoReal", "threshold"),
            ("VuoPoint2d", "stretch"),
            ("VuoPoint2d", "offset"),
            ("VuoPoint2d", "scatter"),
            ("VuoColor", "colorize"),
            ("VuoReal", "colorRatio"),
            ("VuoInteger", "seed"),
            ("VuoInteger", "mode"),
        ],
    },
    {
        "name": "RgbTV",
        "camel": "rgbTv",
        "output": "TextureOutput",
        "input": "image",
        "shader": "RgbTV.hlsl",
        "shader_path": "Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl",
        "defaults": "Image=null, Visibility=1, PatternAmount=0.2, ImageBrightess=0.5, BlackLevel=-0.100000024, ImageContrast=1, PatternSize=0.025, ShiftColumns=0.5, Gaps=0.03, GlitchAmount=1, Noise=0.1, Buldge=0.15, Vignette=1, Resolution=(0,0)",
        "kind": 1,
        "inputs": [
            ("VuoReal", "visibility", '{"default":1.0}'),
            ("VuoReal", "patternAmount", '{"default":0.2}'),
            ("VuoReal", "imageBrightness", '{"default":0.5}'),
            ("VuoReal", "blackLevel", '{"default":-0.100000024}'),
            ("VuoReal", "imageContrast", '{"default":1.0}'),
            ("VuoReal", "patternSize", '{"default":0.025}'),
            ("VuoReal", "shiftColumns", '{"default":0.5}'),
            ("VuoReal", "gaps", '{"default":0.03}'),
            ("VuoReal", "glitchAmount", '{"default":1.0}'),
            ("VuoReal", "noise", '{"default":0.1}'),
            ("VuoReal", "buldge", '{"default":0.15}'),
            ("VuoReal", "vignette", '{"default":1.0}'),
        ],
        "sets": [
            ("VuoReal", "visibility"),
            ("VuoReal", "patternAmount"),
            ("VuoReal", "imageBrightness"),
            ("VuoReal", "blackLevel"),
            ("VuoReal", "imageContrast"),
            ("VuoReal", "patternSize"),
            ("VuoReal", "shiftColumns"),
            ("VuoReal", "gaps"),
            ("VuoReal", "glitchAmount"),
            ("VuoReal", "noise"),
            ("VuoReal", "buldge"),
            ("VuoReal", "vignette"),
        ],
    },
    {
        "name": "SortPixelGlitch",
        "camel": "sortPixelGlitch",
        "output": "Output",
        "input": "texture2d",
        "shader": "SortPixelsGlitch-cs.hlsl",
        "shader_path": "Operators/Lib/Assets/shaders/img/fx/SortPixelsGlitch-cs.hlsl",
        "defaults": "Texture2d=null, Vertical=true, ScanHighlights=false, Threshold=0, Extend=0, BackgroundColor=(1,1,1,1), StreakColor=(1,1,1,1), GradientBias=0.75, ScatterThreshold=0, Offset=0, AddGrain=0, MaxSteps=2000, FadeStreaks=0, LumaBias=0",
        "kind": 2,
        "inputs": [
            ("VuoBoolean", "vertical", '{"default":true}'),
            ("VuoBoolean", "scanHighlights", '{"default":false}'),
            ("VuoReal", "threshold", '{"default":0.0}'),
            ("VuoReal", "extend", '{"default":0.0}'),
            ("VuoColor", "backgroundColor", '{"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}'),
            ("VuoColor", "streakColor", '{"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}'),
            ("VuoReal", "gradientBias", '{"default":0.75}'),
            ("VuoReal", "scatterThreshold", '{"default":0.0}'),
            ("VuoReal", "offset", '{"default":0.0}'),
            ("VuoReal", "addGrain", '{"default":0.0}'),
            ("VuoReal", "maxSteps", '{"default":2000.0}'),
            ("VuoReal", "fadeStreaks", '{"default":0.0}'),
            ("VuoReal", "lumaBias", '{"default":0.0}'),
        ],
        "sets": [
            ("VuoBoolean", "vertical"),
            ("VuoBoolean", "scanHighlights"),
            ("VuoReal", "threshold"),
            ("VuoReal", "extend"),
            ("VuoColor", "backgroundColor"),
            ("VuoColor", "streakColor"),
            ("VuoReal", "gradientBias"),
            ("VuoReal", "scatterThreshold"),
            ("VuoReal", "offset"),
            ("VuoReal", "addGrain"),
            ("VuoReal", "maxSteps"),
            ("VuoReal", "fadeStreaks"),
            ("VuoReal", "lumaBias"),
        ],
    },
    {
        "name": "SubdivisionStretch",
        "camel": "subdivisionStretch",
        "output": "TextureOutput",
        "input": "image",
        "shader": "StretchSubdivide.hlsl",
        "shader_path": "Operators/Lib/Assets/shaders/img/fx/StretchSubdivide.hlsl",
        "defaults": "Image=null, MaxSubdivisions=17, Threshold=0.1, UseAspectForSplit=false, SplitCenter=0.5, SplitCenterVariation=0.7, DirectionBias=0, ScrollOffset=(0,0), RandomPhase=0, RandomSeed=42, GapWidth=0.001, Feather=0.0005, GapColor=(0,0,0,1), GradientMode=0, ColorMode=0, Use4xMSAA=false, TextureFx=0",
        "kind": 3,
        "inputs": [
            ("VuoInteger", "maxSubdivisions", '{"default":17}'),
            ("VuoReal", "threshold", '{"default":0.1}'),
            ("VuoBoolean", "useAspectForSplit", '{"default":false}'),
            ("VuoReal", "splitCenter", '{"default":0.5}'),
            ("VuoReal", "splitCenterVariation", '{"default":0.7}'),
            ("VuoReal", "directionBias", '{"default":0.0}'),
            ("VuoPoint2d", "scrollOffset", '{"default":{"x":0.0,"y":0.0}}'),
            ("VuoReal", "randomPhase", '{"default":0.0}'),
            ("VuoInteger", "randomSeed", '{"default":42}'),
            ("VuoReal", "gapWidth", '{"default":0.001}'),
            ("VuoReal", "feather", '{"default":0.0005}'),
            ("VuoColor", "gapColor", '{"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}'),
            ("VuoReal", "textureFx", '{"default":0.0}'),
        ],
        "sets": [
            ("VuoInteger", "maxSubdivisions"),
            ("VuoReal", "threshold"),
            ("VuoBoolean", "useAspectForSplit"),
            ("VuoReal", "splitCenter"),
            ("VuoReal", "splitCenterVariation"),
            ("VuoReal", "directionBias"),
            ("VuoPoint2d", "scrollOffset"),
            ("VuoReal", "randomPhase"),
            ("VuoInteger", "randomSeed"),
            ("VuoReal", "gapWidth"),
            ("VuoReal", "feather"),
            ("VuoColor", "gapColor"),
            ("VuoReal", "textureFx"),
        ],
    },
]

SHADER = r'''
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D sourceImage;
	uniform int filterKind;
	uniform vec2 targetSize;

	uniform int rows;
	uniform int columns;
	uniform float size;
	uniform float amount;
	uniform float threshold;
	uniform vec2 stretch;
	uniform vec2 offset;
	uniform vec2 scatter;
	uniform vec4 colorize;
	uniform float colorRatio;
	uniform int seed;
	uniform int mode;

	uniform float visibility;
	uniform float patternAmount;
	uniform float imageBrightness;
	uniform float blackLevel;
	uniform float imageContrast;
	uniform float patternSize;
	uniform float shiftColumns;
	uniform float gaps;
	uniform float glitchAmount;
	uniform float noise;
	uniform float buldge;
	uniform float vignette;

	uniform bool vertical;
	uniform bool scanHighlights;
	uniform float extend;
	uniform vec4 backgroundColor;
	uniform vec4 streakColor;
	uniform float gradientBias;
	uniform float scatterThreshold;
	uniform float addGrain;
	uniform float maxSteps;
	uniform float fadeStreaks;
	uniform float lumaBias;

	uniform int maxSubdivisions;
	uniform bool useAspectForSplit;
	uniform float splitCenter;
	uniform float splitCenterVariation;
	uniform float directionBias;
	uniform vec2 scrollOffset;
	uniform float randomPhase;
	uniform int randomSeed;
	uniform float gapWidth;
	uniform float feather;
	uniform vec4 gapColor;
	uniform float textureFx;

	varying vec2 fragmentTextureCoordinate;

	float hash(vec2 p)
	{
		p += float(seed + randomSeed) * 0.013;
		return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
	}

	vec4 source(vec2 uv)
	{
		return texture2D(sourceImage, clamp(uv, 0.0, 1.0));
	}

	float luma(vec3 c)
	{
		return dot(c, vec3(0.299, 0.587, 0.114));
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec2 px = 1.0 / max(targetSize, vec2(1.0));
		vec4 original = source(uv);
		vec4 color = original;

		if (filterKind == 0)
		{
			vec2 cells = max(vec2(float(columns), float(rows)), vec2(1.0));
			vec2 cell = floor((uv + offset * 0.05) * cells);
			float r = hash(cell + vec2(float(mode), 13.0));
			float gate = step(threshold, r);
			float rowPulse = step(0.66, hash(vec2(cell.y, float(seed))));
			vec2 dir = vec2(hash(cell) - 0.5, hash(cell.yx + scatter * 17.0) - 0.5);
			dir *= vec2(stretch.x, stretch.y) * px * amount * size * 10.0;
			vec4 displaced = source(uv + dir * gate * rowPulse);
			color = mix(displaced, vec4(displaced.rgb * (1.0 - colorRatio) + colorize.rgb * colorRatio, displaced.a), gate * rowPulse);
		}
		else if (filterKind == 1)
		{
			vec2 centered = uv - 0.5;
			float d = dot(centered, centered);
			vec2 bulged = uv + centered * d * buldge;
			float line = sin((uv.y / max(patternSize, 0.002)) * 3.14159265);
			float gap = step(1.0 - gaps, fract(uv.y / max(patternSize * 2.0, 0.002)));
			float n = hash(floor(vec2(uv.y * 120.0, uv.x * 24.0)));
			float rowShift = (hash(vec2(floor(uv.y * 32.0), 4.0)) - 0.5) * shiftColumns * glitchAmount * 0.035;
			vec2 guv = bulged + vec2(rowShift + (n - 0.5) * noise * 0.02, 0.0);
			float ch = (1.0 + patternAmount) * px.x * 5.0;
			color.r = source(guv + vec2(ch, 0.0)).r;
			color.g = source(guv).g;
			color.b = source(guv - vec2(ch, 0.0)).b;
			color.rgb = (color.rgb + imageBrightness + blackLevel - 0.5) * imageContrast + 0.5;
			color.rgb *= mix(1.0, 0.75 + 0.25 * line, patternAmount);
			color.rgb += (n - 0.5) * noise;
			color.rgb *= 1.0 - gap * 0.65;
			color.rgb *= mix(1.0, smoothstep(0.62, 0.08, d), vignette);
			color = mix(original, color, visibility);
		}
		else if (filterKind == 2)
		{
			float axis = vertical ? uv.x : uv.y;
			float across = vertical ? uv.y : uv.x;
			float lum = luma(original.rgb) + lumaBias;
			float hot = scanHighlights ? step(threshold, lum) : step(lum, threshold + 0.35);
			float streak = smoothstep(0.0, 1.0, fract(axis * max(maxSteps, 1.0) * 0.006 + offset));
			float scatterGate = step(scatterThreshold, hash(vec2(floor(across * 64.0), floor(axis * 8.0))));
			float pull = (extend + gradientBias * streak + 0.08) * hot * scatterGate;
			vec2 sampleUv = uv + (vertical ? vec2(0.0, pull * 0.09) : vec2(pull * 0.09, 0.0));
			vec4 sorted = source(sampleUv);
			sorted.rgb = mix(sorted.rgb, streakColor.rgb, hot * 0.25);
			sorted.rgb += (hash(uv * targetSize) - 0.5) * addGrain;
			color = mix(mix(backgroundColor, sorted, sorted.a), sorted, 1.0 - fadeStreaks * 0.5);
		}
		else
		{
			vec2 p = uv + scrollOffset * 0.05;
			float bands = pow(2.0, clamp(float(maxSubdivisions) * 0.18, 1.0, 7.0));
			vec2 cell = floor(p * bands);
			float r = hash(cell + randomPhase);
			float split = splitCenter + (r - 0.5) * splitCenterVariation;
			bool xMajor = useAspectForSplit ? targetSize.x > targetSize.y : (r + directionBias > 0.5);
			vec2 local = fract(p * bands);
			float gap = xMajor ? smoothstep(gapWidth + feather, gapWidth, abs(local.x - split)) : smoothstep(gapWidth + feather, gapWidth, abs(local.y - split));
			vec2 warped = uv;
			if (xMajor)
				warped.x = (floor(p.x * bands) + split + (local.x - split) * (1.0 + textureFx)) / bands;
			else
				warped.y = (floor(p.y * bands) + split + (local.y - split) * (1.0 + textureFx)) / bands;
			color = source(warped);
			color = mix(color, gapColor, gap * smoothstep(0.0, 1.0, threshold + 0.6));
		}

		gl_FragColor = clamp(color, 0.0, 1.0);
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
	return VuoImage_makeColorImage((VuoColor){{0.08, 0.09, 0.12, 1.0}}, (unsigned int)width, (unsigned int)height);
}}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) {input_port},
{input_lines}
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.0,"y":0.0}}}}) resolution,
		VuoOutputData(VuoImage, {{"name":"{output}"}}) textureOutput
)
{{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "sourceImage", imageOrFallback({input_port}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoInteger((*instance)->shader, "filterKind", {kind});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){{renderWidth, renderHeight}});
{uniform_sets}
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''


def write(path: str, text: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def setter(vuo_type: str, name: str) -> str:
    suffix = {
        "VuoInteger": "VuoInteger",
        "VuoReal": "VuoReal",
        "VuoPoint2d": "VuoPoint2d",
        "VuoColor": "VuoColor",
        "VuoBoolean": "VuoBoolean",
    }[vuo_type]
    return f'\tVuoShader_setUniform_{suffix}((*instance)->shader, "{name}", {name});'


def node(spec):
    title = f"my_{spec['name']}"
    input_lines = "".join(f"\t\tVuoInputData({vuo_type}, {metadata}) {name},\n" for vuo_type, name, metadata in spec["inputs"])
    uniform_sets = "\n".join(setter(vuo_type, name) for vuo_type, name in spec["sets"])
    return f'''/**
 * @file
 * my.image.fx.glitch.{spec["camel"]} node implementation.
 *
 * TiXL parity contract:
 * - Visible title: {title}
 * - Category: Operators/Lib/image/fx/glitch
 * - Source: external/tixl/Operators/Lib/image/fx/glitch/{spec["name"]}.cs
 * - Default: {spec["defaults"]} from {spec["name"]}.t3
 * - Primary output: Texture2D {spec["output"]} (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for {spec["shader_path"]}.
 * Vuo body-layer limit: DX11 render targets, compute sorting, override buffers, gradients, mips, and multi-pass feedback are represented by a single-pass visual proof.
 */

VuoModuleMetadata({{
\t\t\t\t\t "title" : "{title}",
\t\t\t\t\t "description" : "TiXL {spec["name"]} bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/fx/glitch/{spec["name"]}.cs. Category: Operators/Lib/image/fx/glitch. Primary output: Texture2D {spec["output"]} (ColorForTextures #9F008A). Default: {spec["defaults"]}.",
\t\t\t\t\t "keywords" : [ "tixl", "texture2d", "image", "fx", "glitch", "{spec["name"]}", "{spec["shader"]}", "bounded approximation", "ColorForTextures", "#9F008A" ],
\t\t\t\t\t "version" : "1.0.0",
\t\t\t\t\t "dependencies" : [ "VuoImageRenderer" ],
\t\t\t\t }});
''' + NODE_BODY.format(shader=SHADER, title=title, output=spec["output"], kind=spec["kind"], input_port=spec["input"], input_lines=input_lines, uniform_sets=uniform_sets)


def proof_node():
    uniforms = "\n".join(f"\tuniform sampler2D {spec['camel']}Image;" for spec in NODES)
    inputs = "\n".join(f"\t\tVuoInputData(VuoImage) {spec['camel']}Image," for spec in NODES)
    sets = "\n\t".join(f'VuoShader_setUniform_VuoImage((*instance)->shader, "{spec["camel"]}Image", {spec["camel"]}Image ? {spec["camel"]}Image : VuoImage_makeColorImage((VuoColor){{0.02,0.02,0.025,1}}, renderWidth, renderHeight));' for spec in NODES)
    select = "texture2D(subdivisionStretchImage, localSt)"
    for idx, spec in reversed(list(enumerate(NODES[:-1]))):
        select = f"(band < {idx+1}.0 ? texture2D({spec['camel']}Image, localSt) : {select})"
    return f'''#include "VuoImageRenderer.h"
VuoModuleMetadata({{"title":"my_Batch38GlitchProof","description":"Proof-only compositor for Batch 38 glitch nodes.","keywords":["tixl","batch38","glitch","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]}});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
{uniforms}
\tvarying vec2 fragmentTextureCoordinate;
\tvoid main() {{
\t\tvec2 st = fragmentTextureCoordinate;
\t\tfloat band = floor(clamp(st.x, 0.0, 0.9999) * 4.0);
\t\tvec2 localSt = vec2(fract(st.x * 4.0), st.y);
\t\tvec4 color = {select};
\t\tfloat edge = step(localSt.x, 0.015) + step(0.985, localSt.x);
\t\tif (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0, 0.2, 0.95), 0.55);
\t\tgl_FragColor = color;
\t}}
);
struct nodeInstanceData {{ VuoShader shader; }};
struct nodeInstanceData * nodeInstanceInit(void) {{ struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("my_Batch38GlitchProof Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }}
void nodeInstanceEvent
(
\t\tVuoInstanceData(struct nodeInstanceData *) instance,
\t\tVuoInputEvent() renderTick,
{inputs}
\t\tVuoInputData(VuoInteger, {{"default":640}}) width,
\t\tVuoInputData(VuoInteger, {{"default":160}}) height,
\t\tVuoOutputData(VuoImage, {{"name":"Image"}}) image
)
{{ VuoInteger renderWidth=width<1?640:width; VuoInteger renderHeight=height<1?160:height; {sets} *image=VuoImageRenderer_render((*instance)->shader,renderWidth,renderHeight,VuoImageColorDepth_8); }}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''


def composition():
    defs = []
    edges = []
    proof_edges = []
    for i, spec in enumerate(NODES):
        y = -300 + i * 210
        input_port = spec["input"]
        size_attrs = {
            "GlitchDisplace": '_rows="90" _columns="18" _size="1.6" _amount="1.4" _threshold="0.12" _stretch="\\{{\\"x\\":3,\\"y\\":0.8\\}}" _offset="\\{{\\"x\\":0.5,\\"y\\":0\\}}" _scatter="\\{{\\"x\\":0.2,\\"y\\":0.1\\}}" _colorize="\\{{\\"r\\":1,\\"g\\":0,\\"b\\":0.8917217,\\"a\\":1\\}}" _colorRatio="0.22" _seed="7" _mode="3"',
            "RgbTV": '_visibility="1" _patternAmount="0.45" _imageBrightness="0.55" _blackLevel="-0.05" _imageContrast="1.25" _patternSize="0.018" _shiftColumns="0.9" _gaps="0.05" _glitchAmount="1.15" _noise="0.12" _buldge="0.12" _vignette="0.9"',
            "SortPixelGlitch": '_vertical="true" _scanHighlights="false" _threshold="0.08" _extend="0.6" _backgroundColor="\\{{\\"r\\":0,\\"g\\":0,\\"b\\":0,\\"a\\":1\\}}" _streakColor="\\{{\\"r\\":1,\\"g\\":0.9,\\"b\\":1,\\"a\\":1\\}}" _gradientBias="0.8" _scatterThreshold="0.15" _offset="0.1" _addGrain="0.05" _maxSteps="1600" _fadeStreaks="0" _lumaBias="0"',
            "SubdivisionStretch": '_maxSubdivisions="15" _threshold="0.08" _useAspectForSplit="false" _splitCenter="0.52" _splitCenterVariation="0.55" _directionBias="0.18" _scrollOffset="\\{{\\"x\\":0.1,\\"y\\":0\\}}" _randomPhase="0.4" _randomSeed="42" _gapWidth="0.015" _feather="0.004" _gapColor="\\{{\\"r\\":0,\\"g\\":0,\\"b\\":0,\\"a\\":1\\}}" _textureFx="0.7"',
        }[spec["name"]]
        extra_ports = "".join(f"|<{name}>{name}\\l" for _, name, _ in spec["inputs"])
        defs.append(f'{spec["name"]} [type="my.image.fx.glitch.{spec["camel"]}" version="1.0.0" label="my_{spec["name"]}|<renderTick>renderTick\\l|<{input_port}>{input_port}\\l{extra_ports}|<resolution>resolution\\l|<textureOutput>{spec["output"]}\\r" pos="-500,{y}" fillcolor="#9F008A" {size_attrs} _resolution="\\{{\\"x\\":160,\\"y\\":160\\}}"];')
        edges.append(f"DisplayRefresh:requestedFrame -> {spec['name']}:renderTick;")
        edges.append(f"Source:textureOutput -> {spec['name']}:{input_port};")
        proof_edges.append(f"{spec['name']}:textureOutput -> ProofImage:{spec['camel']}Image;")
    ports = "|".join(f"<{spec['camel']}Image>{spec['camel']}Image\\l" for spec in NODES) + "|"
    return f'''digraph G
{{
DisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\\r" pos="-1100,100" fillcolor="lime" _requestedFrame_eventThrottling="drop"];
Source [type="my.image.generate.basic.checkerBoard" version="1.0.0" label="my_CheckerBoard|<renderTick>renderTick\\l|<colorA>colorA\\l|<colorB>colorB\\l|<stretch>stretch\\l|<scale>scale\\l|<useAspectRatio>useAspectRatio\\l|<offset>offset\\l|<resolution>resolution\\l|<generateMips>generateMips\\l|<textureOutput>TextureOutput\\r" pos="-830,100" fillcolor="#9F008A" _colorA="\\{{\\"r\\":0.0,\\"g\\":0.95,\\"b\\":1.0,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":0.95,\\"g\\":0.0,\\"b\\":0.65,\\"a\\":1\\}}" _stretch="\\{{\\"x\\":1,\\"y\\":1\\}}" _scale="10" _useAspectRatio="true" _offset="\\{{\\"x\\":0,\\"y\\":0\\}}" _resolution="\\{{\\"x\\":160,\\"y\\":160\\}}" _generateMips="false"];
{chr(10).join(defs)}
ProofImage [type="my.image.batch.batch38GlitchProof" version="1.0.0" label="my_Batch38GlitchProof|<renderTick>renderTick\\l|{ports}<width>width\\l|<height>height\\l|<image>Image\\r" pos="120,100" fillcolor="#9F008A" _width="640" _height="160"];
RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch 38 Glitch|<refresh>refresh\\l|<image>image\\l|<setWindowDescription>setWindowDescription\\l|<updatedWindow>updatedWindow\\r" pos="720,100" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];
SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\\l|<url>url\\l|<saveImage>saveImage\\l|<ifExists>ifExists\\l|<format>format\\l|<done>done\\r" pos="720,-190" fillcolor="orange" _url="\\"\\\\/Users\\\\/chenbaiwei\\\\/Desktop\\\\/vibe coding\\\\/simple_world\\\\/artifacts\\\\/vuo_cli\\\\/batch-38-glitch-vuo-save\\"" _ifExists="1" _format="\\"PNG\\""];
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
    names = ", ".join(f'"{spec["name"]}"' for spec in NODES)
    node_rows = ",\n".join(f'    ["vuo-nodes/my.image.fx.glitch.{spec["camel"]}.c", "my_{spec["name"]}", "{spec["name"]}.cs", "{spec["shader"]}", "{spec["output"]}"]' for spec in NODES)
    sem = f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("TiXL glitch namespace has four Texture2D image nodes",()=>{{ for (const name of [{names}]) {{ assert.match(read(`external/tixl/Operators/Lib/image/fx/glitch/${{name}}.cs`),new RegExp(`sealed class ${{name}}`)); assert.match(read(`external/tixl/Operators/Lib/image/fx/glitch/${{name}}.t3`),/DefaultValue/); }} }});
test("SortPixelGlitch source evidence is compute-heavy and documented as bounded",()=>{{ assert.match(read("external/tixl/Operators/Lib/image/fx/glitch/SortPixelGlitch.t3"),/ComputeShader/); assert.match(read("vuo-nodes/my.image.fx.glitch.sortPixelGlitch.c"),/compute sorting/); }});
'''
    vuo = f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 38 Vuo glitch nodes preserve names, paths, shader cues, outputs, and bounded limits",()=>{{ const nodes=[\n{node_rows}\n  ]; for (const [file,title,donor,shader,output] of nodes) {{ const source=read(file); assert.match(source,new RegExp(`"title"\\\\s*:\\\\s*"${{title}}"`)); assert.match(source,new RegExp(`Source: external/tixl/Operators/Lib/image/fx/glitch/${{donor}}`)); assert.match(source,new RegExp(shader.replace(/[.*+?^${{}}()|[\\]\\\\]/g,"\\\\$&"))); assert.match(source,new RegExp(`Texture2D ${{output}}`)); assert.match(source,/ColorForTextures #9F008A/); assert.match(source,/single-pass visual proof/); }} }});
'''
    comp = '''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 38 proof wires glitch filters to a colored checkerboard source and save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo"),"utf8"); for (const title of ["my_GlitchDisplace","my_RgbTV","my_SortPixelGlitch","my_SubdivisionStretch","my_Batch38GlitchProof"]) assert.match(s,new RegExp(title)); assert.match(s,/Source:textureOutput -> GlitchDisplace:image/); assert.match(s,/Source:textureOutput -> SortPixelGlitch:texture2d/); assert.match(s,/batch-38-glitch-vuo-save/); });
'''
    return sem, vuo, comp


def doc():
    rows = "\n".join(f"| `Lib.image.fx.glitch.{spec['name']}` | {'D compute/state-heavy bounded adapter' if spec['name'] == 'SortPixelGlitch' else 'C bounded shader adapter'} | `my_{spec['name']}` | `vuo-nodes/my.image.fx.glitch.{spec['camel']}.c` | C# `external/tixl/Operators/Lib/image/fx/glitch/{spec['name']}.cs`; `.t3` `external/tixl/Operators/Lib/image/fx/glitch/{spec['name']}.t3`; shader `{spec['shader_path']}` | Batch 38 tests | `vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo` | done |" for spec in NODES)
    return f'''# Batch 38 Image FX Glitch Acceptance Matrix

Scope: finish `Lib.image.fx.glitch`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
{rows}

## Proof Notes

- Primary output type is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- `my_GlitchDisplace`, `my_RgbTV`, and `my_SubdivisionStretch` are bounded single-pass shader adapters of their TiXL image effects.
- `my_SortPixelGlitch` is accepted as a bounded visual adapter only: TiXL uses DX11 render targets plus `SortPixelsGlitch-cs.hlsl`, so exact compute-buffer pixel sorting is outside this Vuo body-layer proof.
- Vuo CLI proof target: `batch-38-glitch-proof`.
'''


def main():
    for spec in NODES:
        write(f"vuo-nodes/my.image.fx.glitch.{spec['camel']}.c", node(spec))
    write("vuo-nodes/my.image.batch.batch38GlitchProof.c", proof_node())
    write("vuo-compositions/generated/myworld-batch-38-glitch-proof.vuo", composition())
    sem, vuo, comp = tests()
    write("tests/tixl_batch38_glitch_semantics.test.js", sem)
    write("tests/tixl_batch38_glitch_vuo_nodes.test.js", vuo)
    write("tests/vuo_batch_38_glitch_composition.test.js", comp)
    write("docs/tixl-porting/batches/2026-06-05-batch-38-glitch.md", doc())


if __name__ == "__main__":
    main()
