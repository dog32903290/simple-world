/**
 * @file
 * my.image.generate.pattern.ryojiPattern2 node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_RyojiPattern2
 * - Category: Operators/Lib/image/generate/pattern
 * - Source: external/tixl/Operators/Lib/image/generate/pattern/RyojiPattern2.cs
 * - Default: Image=null, Background=(0,0,0,0), Foreground=(1,1,1,1), MixOriginal=0, Contrast=0.5, ForgroundRatio=0.50333333, Highlight=(1,0,0,1), HighlightProbability=0.01, HighlightSeed=0, Splits=(0,0), SplitB=(0,0), SplitC=(0,0), SplitProbability=(0,0), ScrollSpeed=(0,0), ScrollProbability=(0,0), ScrollOffset=0, Padding=(0,0), Seed=42, Resolution=(0,0) from RyojiPattern2.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/{fx or generate}/RyojiPattern2.hlsl.
 * Vuo body-layer limit: complex TiXL image-input modulation, blend modes, resource formats, and full shader-specific parameter set are represented by shared pattern controls.
 */

VuoModuleMetadata({
					 "title" : "my_RyojiPattern2",
					 "description" : "TiXL RyojiPattern2 bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/pattern/RyojiPattern2.cs. Category: Operators/Lib/image/generate/pattern. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Background=(0,0,0,0), Foreground=(1,1,1,1), MixOriginal=0, Contrast=0.5, ForgroundRatio=0.50333333, Highlight=(1,0,0,1), HighlightProbability=0.01, HighlightSeed=0, Splits=(0,0), SplitB=(0,0), SplitC=(0,0), SplitProbability=(0,0), ScrollSpeed=(0,0), ScrollProbability=(0,0), ScrollOffset=0, Padding=(0,0), Seed=42, Resolution=(0,0).",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "pattern", "RyojiPattern2", "RyojiPattern2.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>


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


struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_RyojiPattern2 Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 160;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) colorB,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":0.0}}) background,
		VuoInputData(VuoReal, {"default":1.0}) scale,
		VuoInputData(VuoReal, {"default":0.0}) rotate,
		VuoInputData(VuoReal, {"default":12.0}) density,
		VuoInputData(VuoReal, {"default":0.08}) lineWidth,
		VuoInputData(VuoReal, {"default":0.01}) feather,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
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
	VuoShader_setUniform_VuoInteger((*instance)->shader, "patternKind", 5);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
