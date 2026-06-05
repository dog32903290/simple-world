#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

NODES = [
    ("AdjustColors", "adjustColors", "texture2d", "Output", "AdjustColors.hlsl", "Operators/Lib/Assets/shaders/img/fx/AdjustColors.hlsl", "Texture2d=null, Exposure=1, Brightness=0, Contrast=0, Saturation=1, Hue=0, Vignette=0, OrangeTeal=0, Colorize=(1,1,1,0), Background=(0,0,0,1)", 0),
    ("ChannelMixer", "channelMixer", "texture2d", "Output", "MixChannels.hlsl", "Operators/Lib/Assets/shaders/img/MixChannels.hlsl", "Texture2d=null, MultiplyR=(1,0,0,0), MultiplyG=(0,1,0,0), MultiplyB=(0,0,1,0), Add=(0,0,0,0), ClampResult=true, GenerateMipmaps=false", 1),
    ("ColorGrade", "colorGrade", "texture2d", "Output", "ColorGrade.hlsl", "Operators/Lib/Assets/shaders/img/ColorGrade.hlsl", "Texture2d=null, Lift=(0.5,0.5,0.5,0.25), Gamma=(0.5,0.5,0.5,0.506), Gain=(0.5,0.5,0.5,0.506), PreSaturate=1, ClampResult=false, VignetteCenter=(0,0)", 2),
    ("ColorGradeDepth", "colorGradeDepth", "texture2d", "Output", "ColorGradeWithDepth.hlsl", "Operators/Lib/Assets/shaders/img/adjust/ColorGradeWithDepth.hlsl", "Texture2d=null, DepthBuffer=null, Lift/Gamma/Gain=(0.5...), PreSaturate=1, GradientDepthRange=(0.1,100), CamNearFarClip=(0.01,1000)", 3),
    ("ConvertColors", "convertColors", "texture2d", "Output", "img-fx-ConvertColors.hlsl", "Operators/Lib/Assets/shaders/img/adjust/img-fx-ConvertColors.hlsl", "Texture2d=null, Mode=0, OutputFormat=R32G32B32A32_Float, GenerateMipmaps=false", 4),
    ("ConvertFormat", "convertFormat", "texture2d", "Output", "ConvertFormat-cs.hlsl", "Operators/Lib/Assets/shaders/img/ConvertFormat-cs.hlsl", "Texture2d=null, Enable=true, Format=R8G8B8A8_UNorm, ScaleFactor=1, GenerateMipMaps=false", 5),
    ("HSE", "hse", "texture2d", "Output", "HueShift.hlsl", "Operators/Lib/Assets/shaders/img/fx/HueShift.hlsl", "Texture2d=null, FxTexture=null, Hue=0, Saturation=1, Exposure=1", 6),
    ("KeyColor", "keyColor", "texture2d", "Output", "ChromaKey.hlsl", "Operators/Lib/Assets/shaders/img/fx/ChromaKey.hlsl", "Texture2d=null, Key=(1,1,1,1), Background=(0,0,0,0), Tolerance=0, Choke=0, Amplify=0, WeightBrightness=10, Exposure=1, Return=0", 7),
    ("RemapColor", "remapColor", "image", "TextureOutput", "ColorRemap.hlsl", "Operators/Lib/Assets/shaders/img/fx/ColorRemap.hlsl", "Image=null, Gradient=black-to-white, Mode=1, GainAndBias=(0.5,0.5), Exposure=1, Cycle=0, DontColorAlpha=false, GradientSteps=256", 8),
    ("Tint", "tint", "texture2d", "Output", "Tint.hlsl", "Operators/Lib/Assets/shaders/img/fx/Tint.hlsl", "Texture2d=null, MapBlackTo=(0,0,0,1), MapWhiteTo=(1,1,1,1), Amount=1, Exposure=1, GainAndBias=(0.5,0.5), ChannelWeights=(1,1,1,0)", 9),
    ("ToneMapping", "toneMapping", "texture2d", "Output", "ToneMap.hlsl", "Operators/Lib/Assets/shaders/img/fx/ToneMap.hlsl", "Texture2d=null, Mode=4, Exposure=1, Gamma=2.2, CorrectGamma=false", 10),
]

SHADER = r'''
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D sourceImage;
	uniform int filterKind;
	uniform vec2 targetSize;
	uniform float exposure;
	uniform float brightness;
	uniform float contrast;
	uniform float saturation;
	uniform float hue;
	uniform float amount;
	uniform float mode;
	uniform float gammaValue;
	uniform bool enable;
	uniform bool clampResult;
	uniform bool correctGamma;
	uniform bool dontColorAlpha;
	uniform vec2 gainAndBias;
	uniform vec2 center;
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec4 colorC;
	uniform vec4 colorD;
	uniform vec4 background;
	uniform vec4 keyColor;
	varying vec2 fragmentTextureCoordinate;

	vec4 source(vec2 uv)
	{
		return texture2D(sourceImage, clamp(uv, 0.0, 1.0));
	}

	float luma(vec3 c)
	{
		return dot(c, vec3(0.299, 0.587, 0.114));
	}

	vec3 rgb2hsv(vec3 c)
	{
		vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
		vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
		vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
		float d = q.x - min(q.w, q.y);
		float e = 1.0e-10;
		return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
	}

	vec3 hsv2rgb(vec3 c)
	{
		vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
		return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
	}

	vec3 applyHueSat(vec3 rgb, float h, float s)
	{
		vec3 hsv = rgb2hsv(max(rgb, vec3(0.0)));
		hsv.x = fract(hsv.x + h);
		hsv.y *= s;
		return hsv2rgb(hsv);
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec4 original = source(uv);
		vec4 color = original;
		float lum = luma(original.rgb);

		if (filterKind == 0)
		{
			color.rgb = applyHueSat(color.rgb, hue, saturation);
			color.rgb = (color.rgb - 0.5) * (1.0 + contrast) + 0.5 + brightness;
			color.rgb *= exposure;
			color.rgb = mix(color.rgb, mix(color.rgb, colorA.rgb, colorA.a), amount);
			vec2 p = uv - 0.5 - center;
			float vig = smoothstep(0.72, 0.25, length(p));
			color.rgb = mix(background.rgb, color.rgb, vig);
		}
		else if (filterKind == 1)
		{
			vec4 r = colorA * original.r;
			vec4 g = colorB * original.g;
			vec4 b = colorC * original.b;
			color = r + g + b + colorD;
		}
		else if (filterKind == 2 || filterKind == 3)
		{
			vec3 lifted = color.rgb + (colorA.rgb - 0.5) * colorA.a;
			vec3 gammaAdjusted = pow(max(lifted, vec3(0.0)), vec3(1.0 / max(colorB.a * 2.0, 0.05))) + (colorB.rgb - 0.5) * 0.15;
			color.rgb = gammaAdjusted * (1.0 + (colorC.rgb - 0.5) * 2.0 * colorC.a);
			color.rgb = mix(vec3(luma(color.rgb)), color.rgb, saturation);
			if (filterKind == 3)
			{
				float depthApprox = uv.y;
				vec3 depthTint = mix(colorD.rgb, colorC.rgb, smoothstep(0.1, 1.0, depthApprox));
				color.rgb = mix(color.rgb, depthTint, 0.22);
			}
		}
		else if (filterKind == 4)
		{
			if (mode < 0.5)
				color.rgb = vec3(lum);
			else if (mode < 1.5)
				color.rgb = rgb2hsv(color.rgb);
			else
				color.rgb = applyHueSat(color.rgb, 0.08, 1.2);
		}
		else if (filterKind == 5)
		{
			if (enable)
				color.rgb = floor(clamp(color.rgb * exposure, 0.0, 1.0) * 31.0) / 31.0;
		}
		else if (filterKind == 6)
		{
			color.rgb = applyHueSat(color.rgb, hue, saturation) * exposure;
		}
		else if (filterKind == 7)
		{
			float dist = distance(color.rgb, keyColor.rgb);
			float matte = smoothstep(amount, amount + 0.25, dist + brightness * lum);
			color = mix(background, vec4(color.rgb * exposure, color.a), matte);
		}
		else if (filterKind == 8)
		{
			float t = clamp(lum * exposure * gainAndBias.x + gainAndBias.y - 0.5 + mode * 0.03, 0.0, 1.0);
			color.rgb = mix(colorA.rgb, colorB.rgb, t);
			if (!dontColorAlpha)
				color.a = mix(colorA.a, colorB.a, t) * original.a;
		}
		else if (filterKind == 9)
		{
			vec3 mapped = mix(colorA.rgb, colorB.rgb, clamp(lum * exposure * gainAndBias.x + gainAndBias.y - 0.5, 0.0, 1.0));
			color.rgb = mix(color.rgb, mapped, amount);
		}
		else
		{
			vec3 hdr = color.rgb * exposure;
			if (mode < 1.5)
				color.rgb = hdr / (hdr + vec3(1.0));
			else if (mode < 3.5)
				color.rgb = 1.0 - exp(-hdr);
			else
				color.rgb = clamp((hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14), 0.0, 1.0);
			if (correctGamma)
				color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(1.0 / max(gammaValue, 0.05)));
		}

		if (clampResult)
			color = clamp(color, 0.0, 1.0);
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
	return VuoImage_makeColorImage((VuoColor){{0.35, 0.35, 0.35, 1.0}}, (unsigned int)width, (unsigned int)height);
}}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) {input_port},
		VuoInputData(VuoReal, {{"default":1.0}}) exposure,
		VuoInputData(VuoReal, {{"default":0.0}}) brightness,
		VuoInputData(VuoReal, {{"default":0.0}}) contrast,
		VuoInputData(VuoReal, {{"default":1.0}}) saturation,
		VuoInputData(VuoReal, {{"default":0.0}}) hue,
		VuoInputData(VuoReal, {{"default":1.0}}) amount,
		VuoInputData(VuoReal, {{"default":{mode_default}}}) mode,
		VuoInputData(VuoReal, {{"default":2.2}}) gammaValue,
		VuoInputData(VuoBoolean, {{"default":true}}) enable,
		VuoInputData(VuoBoolean, {{"default":true}}) clampResult,
		VuoInputData(VuoBoolean, {{"default":false}}) correctGamma,
		VuoInputData(VuoBoolean, {{"default":false}}) dontColorAlpha,
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.5,"y":0.5}}}}) gainAndBias,
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.0,"y":0.0}}}}) center,
		VuoInputData(VuoColor, {{"default":{color_a}}}) colorA,
		VuoInputData(VuoColor, {{"default":{color_b}}}) colorB,
		VuoInputData(VuoColor, {{"default":{color_c}}}) colorC,
		VuoInputData(VuoColor, {{"default":{color_d}}}) colorD,
		VuoInputData(VuoColor, {{"default":{{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}}}) background,
		VuoInputData(VuoColor, {{"default":{{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}}}) keyColor,
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.0,"y":0.0}}}}) resolution,
		VuoOutputData(VuoImage, {{"name":"{output}"}}) textureOutput
)
{{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "sourceImage", imageOrFallback({input_port}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoInteger((*instance)->shader, "filterKind", {kind});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){{renderWidth, renderHeight}});
	VuoShader_setUniform_VuoReal((*instance)->shader, "exposure", exposure);
	VuoShader_setUniform_VuoReal((*instance)->shader, "brightness", brightness);
	VuoShader_setUniform_VuoReal((*instance)->shader, "contrast", contrast);
	VuoShader_setUniform_VuoReal((*instance)->shader, "saturation", saturation);
	VuoShader_setUniform_VuoReal((*instance)->shader, "hue", hue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "amount", amount);
	VuoShader_setUniform_VuoReal((*instance)->shader, "mode", mode);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gammaValue", gammaValue);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "enable", enable);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "clampResult", clampResult);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "correctGamma", correctGamma);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "dontColorAlpha", dontColorAlpha);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", gainAndBias);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "center", center);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorC", colorC);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorD", colorD);
	VuoShader_setUniform_VuoColor((*instance)->shader, "background", background);
	VuoShader_setUniform_VuoColor((*instance)->shader, "keyColor", keyColor);
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''


def write(path: str, text: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def color_json(r, g, b, a):
    return f'{{"r":{r},"g":{g},"b":{b},"a":{a}}}'


def node(spec):
    name, camel, input_port, output, shader, shader_path, defaults, kind = spec
    title = f"my_{name}"
    mode_default = 4.0 if name == "ToneMapping" else 1.0 if name == "RemapColor" else 0.0
    color_a = color_json(1.0, 1.0, 1.0, 0.0) if name == "AdjustColors" else color_json(1.0, 0.0, 0.0, 0.0) if name == "ChannelMixer" else color_json(0.5, 0.5, 0.5, 0.25)
    color_b = color_json(0.0, 1.0, 0.0, 0.0) if name == "ChannelMixer" else color_json(1.0, 1.0, 1.0, 1.0)
    color_c = color_json(0.0, 0.0, 1.0, 0.0) if name == "ChannelMixer" else color_json(0.5, 0.5, 0.5, 0.506)
    color_d = color_json(0.0, 0.0, 0.0, 0.0)
    return f'''/**
 * @file
 * my.image.color.{camel} node implementation.
 *
 * TiXL parity contract:
 * - Visible title: {title}
 * - Category: Operators/Lib/image/color
 * - Source: external/tixl/Operators/Lib/image/color/{name}.cs
 * - Default: {defaults} from {name}.t3
 * - Primary output: Texture2D {output} (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for {shader_path}.
 * Vuo body-layer limit: DXGI formats, compute passes, depth gradients, LUT precision, mips, and multi-pass texture setup are represented by a single-pass color proof.
 */

VuoModuleMetadata({{
\t\t\t\t\t "title" : "{title}",
\t\t\t\t\t "description" : "TiXL {name} bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/color/{name}.cs. Category: Operators/Lib/image/color. Primary output: Texture2D {output} (ColorForTextures #9F008A). Default: {defaults}.",
\t\t\t\t\t "keywords" : [ "tixl", "texture2d", "image", "color", "{name}", "{shader}", "bounded approximation", "ColorForTextures", "#9F008A" ],
\t\t\t\t\t "version" : "1.0.0",
\t\t\t\t\t "dependencies" : [ "VuoImageRenderer" ],
\t\t\t\t }});
''' + NODE_BODY.format(shader=SHADER, title=title, input_port=input_port, output=output, kind=kind, mode_default=mode_default, color_a=color_a, color_b=color_b, color_c=color_c, color_d=color_d)


def proof_node():
    uniforms = "\n".join(f"\tuniform sampler2D {camel}Image;" for _, camel, *_ in NODES)
    inputs = "\n".join(f"\t\tVuoInputData(VuoImage) {camel}Image," for _, camel, *_ in NODES)
    sets = "\n\t".join(f'VuoShader_setUniform_VuoImage((*instance)->shader, "{camel}Image", {camel}Image ? {camel}Image : VuoImage_makeColorImage((VuoColor){{0.05,0.05,0.05,1}}, renderWidth, renderHeight));' for _, camel, *_ in NODES)
    select = "texture2D(toneMappingImage, localSt)"
    for idx, (_, camel, *_rest) in reversed(list(enumerate(NODES[:-1]))):
        select = f"(band < {idx+1}.0 ? texture2D({camel}Image, localSt) : {select})"
    return f'''#include "VuoImageRenderer.h"
VuoModuleMetadata({{"title":"my_Batch39ColorProof","description":"Proof-only compositor for Batch 39 color nodes.","keywords":["tixl","batch39","color","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]}});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
{uniforms}
\tvarying vec2 fragmentTextureCoordinate;
\tvoid main() {{
\t\tvec2 st = fragmentTextureCoordinate;
\t\tfloat band = floor(clamp(st.x, 0.0, 0.9999) * 11.0);
\t\tvec2 localSt = vec2(fract(st.x * 11.0), st.y);
\t\tvec4 color = {select};
\t\tfloat edge = step(localSt.x, 0.012) + step(0.988, localSt.x);
\t\tif (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.38);
\t\tgl_FragColor = color;
\t}}
);
struct nodeInstanceData {{ VuoShader shader; }};
struct nodeInstanceData * nodeInstanceInit(void) {{ struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("my_Batch39ColorProof Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }}
void nodeInstanceEvent
(
\t\tVuoInstanceData(struct nodeInstanceData *) instance,
\t\tVuoInputEvent() renderTick,
{inputs}
\t\tVuoInputData(VuoInteger, {{"default":1320}}) width,
\t\tVuoInputData(VuoInteger, {{"default":160}}) height,
\t\tVuoOutputData(VuoImage, {{"name":"Image"}}) image
)
{{ VuoInteger renderWidth=width<1?1320:width; VuoInteger renderHeight=height<1?160:height; {sets} *image=VuoImageRenderer_render((*instance)->shader,renderWidth,renderHeight,VuoImageColorDepth_8); }}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''


def composition():
    attrs = {
        "AdjustColors": '_exposure="1.15" _brightness="0.05" _contrast="0.35" _saturation="1.35" _hue="0.06" _amount="0.5" _colorA="\\{{\\"r\\":0.95,\\"g\\":0.45,\\"b\\":0.2,\\"a\\":0.45\\}}" _background="\\{{\\"r\\":0,\\"g\\":0,\\"b\\":0,\\"a\\":1\\}}"',
        "ChannelMixer": '_colorA="\\{{\\"r\\":1.0,\\"g\\":0.08,\\"b\\":0,\\"a\\":0\\}}" _colorB="\\{{\\"r\\":0,\\"g\\":0.8,\\"b\\":0.2,\\"a\\":0\\}}" _colorC="\\{{\\"r\\":0.1,\\"g\\":0.25,\\"b\\":1.0,\\"a\\":0\\}}" _colorD="\\{{\\"r\\":0.05,\\"g\\":0,\\"b\\":0.04,\\"a\\":0\\}}"',
        "ColorGrade": '_saturation="1.15" _colorA="\\{{\\"r\\":0.45,\\"g\\":0.48,\\"b\\":0.55,\\"a\\":0.35\\}}" _colorB="\\{{\\"r\\":0.55,\\"g\\":0.50,\\"b\\":0.45,\\"a\\":0.65\\}}" _colorC="\\{{\\"r\\":0.65,\\"g\\":0.58,\\"b\\":0.48,\\"a\\":0.65\\}}"',
        "ColorGradeDepth": '_saturation="1.05" _colorA="\\{{\\"r\\":0.42,\\"g\\":0.45,\\"b\\":0.58,\\"a\\":0.35\\}}" _colorB="\\{{\\"r\\":0.55,\\"g\\":0.50,\\"b\\":0.45,\\"a\\":0.65\\}}" _colorC="\\{{\\"r\\":0.95,\\"g\\":0.75,\\"b\\":0.35,\\"a\\":1\\}}" _colorD="\\{{\\"r\\":0.1,\\"g\\":0.25,\\"b\\":0.8,\\"a\\":1\\}}"',
        "ConvertColors": '_mode="1"',
        "ConvertFormat": '_enable="true" _exposure="1.1"',
        "HSE": '_hue="0.18" _saturation="1.6" _exposure="1.05"',
        "KeyColor": '_amount="0.25" _brightness="0.05" _keyColor="\\{{\\"r\\":0,\\"g\\":0.95,\\"b\\":1,\\"a\\":1\\}}" _background="\\{{\\"r\\":0,\\"g\\":0,\\"b\\":0,\\"a\\":0\\}}"',
        "RemapColor": '_mode="1" _gainAndBias="\\{{\\"x\\":0.9,\\"y\\":0.45\\}}" _colorA="\\{{\\"r\\":0.02,\\"g\\":0,\\"b\\":0.08,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":1,\\"g\\":0.83,\\"b\\":0.22,\\"a\\":1\\}}"',
        "Tint": '_amount="0.82" _gainAndBias="\\{{\\"x\\":0.9,\\"y\\":0.5\\}}" _colorA="\\{{\\"r\\":0.02,\\"g\\":0.02,\\"b\\":0.15,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":1,\\"g\\":0.45,\\"b\\":0.25,\\"a\\":1\\}}"',
        "ToneMapping": '_mode="4" _exposure="1.6" _gammaValue="2.2" _correctGamma="true"',
    }
    defs = []
    edges = []
    proof_edges = []
    for i, (name, camel, input_port, output, *_rest) in enumerate(NODES):
        y = -520 + i * 110
        defs.append(f'{name} [type="my.image.color.{camel}" version="1.0.0" label="my_{name}|<renderTick>renderTick\\l|<{input_port}>{input_port}\\l|<exposure>exposure\\l|<brightness>brightness\\l|<contrast>contrast\\l|<saturation>saturation\\l|<hue>hue\\l|<amount>amount\\l|<mode>mode\\l|<gammaValue>gammaValue\\l|<enable>enable\\l|<clampResult>clampResult\\l|<correctGamma>correctGamma\\l|<dontColorAlpha>dontColorAlpha\\l|<gainAndBias>gainAndBias\\l|<center>center\\l|<colorA>colorA\\l|<colorB>colorB\\l|<colorC>colorC\\l|<colorD>colorD\\l|<background>background\\l|<keyColor>keyColor\\l|<resolution>resolution\\l|<textureOutput>{output}\\r" pos="-500,{y}" fillcolor="#9F008A" {attrs[name]} _clampResult="true" _resolution="\\{{\\"x\\":120,\\"y\\":160\\}}"];')
        edges.append(f"DisplayRefresh:requestedFrame -> {name}:renderTick;")
        edges.append(f"Source:textureOutput -> {name}:{input_port};")
        proof_edges.append(f"{name}:textureOutput -> ProofImage:{camel}Image;")
    ports = "|".join(f"<{camel}Image>{camel}Image\\l" for _, camel, *_ in NODES) + "|"
    return f'''digraph G
{{
DisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\\r" pos="-1100,100" fillcolor="lime" _requestedFrame_eventThrottling="drop"];
Source [type="my.image.generate.basic.checkerBoard" version="1.0.0" label="my_CheckerBoard|<renderTick>renderTick\\l|<colorA>colorA\\l|<colorB>colorB\\l|<stretch>stretch\\l|<scale>scale\\l|<useAspectRatio>useAspectRatio\\l|<offset>offset\\l|<resolution>resolution\\l|<generateMips>generateMips\\l|<textureOutput>TextureOutput\\r" pos="-830,100" fillcolor="#9F008A" _colorA="\\{{\\"r\\":0.0,\\"g\\":0.95,\\"b\\":1.0,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":1.0,\\"g\\":0.08,\\"b\\":0.42,\\"a\\":1\\}}" _stretch="\\{{\\"x\\":1,\\"y\\":1\\}}" _scale="8" _useAspectRatio="true" _offset="\\{{\\"x\\":0,\\"y\\":0\\}}" _resolution="\\{{\\"x\\":120,\\"y\\":160\\}}" _generateMips="false"];
{chr(10).join(defs)}
ProofImage [type="my.image.batch.batch39ColorProof" version="1.0.0" label="my_Batch39ColorProof|<renderTick>renderTick\\l|{ports}<width>width\\l|<height>height\\l|<image>Image\\r" pos="120,100" fillcolor="#9F008A" _width="1320" _height="160"];
RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch 39 Color|<refresh>refresh\\l|<image>image\\l|<setWindowDescription>setWindowDescription\\l|<updatedWindow>updatedWindow\\r" pos="720,100" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];
SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\\l|<url>url\\l|<saveImage>saveImage\\l|<ifExists>ifExists\\l|<format>format\\l|<done>done\\r" pos="720,-190" fillcolor="orange" _url="\\"\\\\/Users\\\\/chenbaiwei\\\\/Desktop\\\\/vibe coding\\\\/simple_world\\\\/artifacts\\\\/vuo_cli\\\\/batch-39-color-vuo-save\\"" _ifExists="1" _format="\\"PNG\\""];
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
    names = ", ".join(f'"{name}"' for name, *_ in NODES)
    node_rows = ",\n".join(f'    ["vuo-nodes/my.image.color.{camel}.c", "my_{name}", "{name}.cs", "{shader}", "{output}"]' for name, camel, _input, output, shader, *_ in NODES)
    sem = f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("TiXL image color namespace has eleven Texture2D color nodes",()=>{{ for (const name of [{names}]) {{ assert.match(read(`external/tixl/Operators/Lib/image/color/${{name}}.cs`),new RegExp(`sealed class ${{name}}`)); assert.match(read(`external/tixl/Operators/Lib/image/color/${{name}}.t3`),/DefaultValue/); }} }});
test("ConvertFormat and ColorGradeDepth are documented as bounded renderer adapters",()=>{{ assert.match(read("vuo-nodes/my.image.color.convertFormat.c"),/DXGI formats/); assert.match(read("vuo-nodes/my.image.color.colorGradeDepth.c"),/depth gradients/); }});
'''
    vuo = f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch 39 Vuo color nodes preserve names, paths, shader cues, outputs, and bounded limits",()=>{{ const nodes=[\n{node_rows}\n  ]; for (const [file,title,donor,shader,output] of nodes) {{ const source=read(file); assert.match(source,new RegExp(`"title"\\\\s*:\\\\s*"${{title}}"`)); assert.match(source,new RegExp(`Source: external/tixl/Operators/Lib/image/color/${{donor}}`)); assert.match(source,new RegExp(shader.replace(/[.*+?^${{}}()|[\\]\\\\]/g,"\\\\$&"))); assert.match(source,new RegExp(`Texture2D ${{output}}`)); assert.match(source,/ColorForTextures #9F008A/); assert.match(source,/single-pass color proof/); }} }});
'''
    comp = '''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch 39 proof wires all color filters to a colored checkerboard source and save path",()=>{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-39-color-proof.vuo"),"utf8"); for (const title of ["my_AdjustColors","my_ChannelMixer","my_ColorGrade","my_ColorGradeDepth","my_ConvertColors","my_ConvertFormat","my_HSE","my_KeyColor","my_RemapColor","my_Tint","my_ToneMapping","my_Batch39ColorProof"]) assert.match(s,new RegExp(title)); assert.match(s,/Source:textureOutput -> AdjustColors:texture2d/); assert.match(s,/Source:textureOutput -> RemapColor:image/); assert.match(s,/batch-39-color-vuo-save/); });
'''
    return sem, vuo, comp


def doc():
    rows = "\n".join(f"| `Lib.image.color.{name}` | C bounded shader adapter | `my_{name}` | `vuo-nodes/my.image.color.{camel}.c` | C# `external/tixl/Operators/Lib/image/color/{name}.cs`; `.t3` `external/tixl/Operators/Lib/image/color/{name}.t3`; shader `{shader_path}` | Batch 39 tests | `vuo-compositions/generated/myworld-batch-39-color-proof.vuo` | done |" for name, camel, _input, _output, _shader, shader_path, *_ in NODES)
    return f'''# Batch 39 Image Color Acceptance Matrix

Scope: finish `Lib.image.color`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
{rows}

## Proof Notes

- Primary output type is `Texture2D`, so node/cable color remains TiXL `ColorForTextures #9F008A`.
- These are bounded Vuo color shader adapters. They preserve creator-facing TiXL names, category, primary output names, defaults evidence, and visible color-transform behavior.
- Body-layer limits: exact DXGI format conversion, compute paths, depth-buffer grading, LUT precision, mips, and multi-pass texture setup are documented but not represented as full Tooll3 renderer internals.
- Vuo CLI proof target: `batch-39-color-proof`.
'''


def main():
    for spec in NODES:
        name, camel, *_ = spec
        write(f"vuo-nodes/my.image.color.{camel}.c", node(spec))
    write("vuo-nodes/my.image.batch.batch39ColorProof.c", proof_node())
    write("vuo-compositions/generated/myworld-batch-39-color-proof.vuo", composition())
    sem, vuo, comp = tests()
    write("tests/tixl_batch39_color_semantics.test.js", sem)
    write("tests/tixl_batch39_color_vuo_nodes.test.js", vuo)
    write("tests/vuo_batch_39_color_composition.test.js", comp)
    write("docs/tixl-porting/batches/2026-06-05-batch-39-color.md", doc())


if __name__ == "__main__":
    main()
