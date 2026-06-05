/**
 * @file
 * my.image.generate.basic.roundedRect node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_RoundedRect
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/RoundedRect.cs
 * - Default: Image=null, Color=(1,1,1,1), Background=(0,0,0,0), Position=(0,0), Stretch=(1,1), Scale=0.5, Rotate=0, Round=0.5, StrokeColor=(1,1,1,1), Stroke=0, Feather=0, FeatherBias=-0.001, Resolution=(0,0), GenerateMips=false from RoundedRect.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/RoundedRect.hlsl.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_RoundedRect",
					 "description" : "TiXL RoundedRect Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/RoundedRect.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Color=(1,1,1,1), Background=(0,0,0,0), Position=(0,0), Stretch=(1,1), Scale=0.5, Rotate=0, Round=0.5, StrokeColor=(1,1,1,1), Stroke=0, Feather=0, FeatherBias=-0.001, Resolution=(0,0), GenerateMips=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "rounded", "rect", "RoundedRect.hlsl", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 fill;
	uniform vec4 outlineColor;
	uniform vec4 background;
	uniform vec2 stretch;
	uniform vec2 center;
	uniform float scale;
	uniform float roundness;
	uniform float stroke;
	uniform float feather;
	uniform float gradientBias;
	uniform float rotate;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float sdBox(vec2 p, vec2 b)
	{
		vec2 d = abs(p) - b;
		return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		float imageRotationRad = (-rotate - 90.0) / 180.0 * 3.141578;
		float sina = sin(-imageRotationRad - 3.141578 / 2.0);
		float cosa = cos(-imageRotationRad - 3.141578 / 2.0);
		p -= center * vec2(1.0, -1.0);
		p = vec2(cosa * p.x - sina * p.y, cosa * p.y + sina * p.x);
		vec2 size = stretch * scale;
		float minSize = min(size.x, size.y);
		float roundOffset = minSize * roundness;
		vec2 rsize = size - roundOffset;
		float d = sdBox(p, rsize / 2.0);
		d = gradientBias >= 0.0 ? pow(max(d, 0.0), gradientBias + 1.0) : 1.0 - pow(clamp(1.0 - d, 0.0, 10.0), -gradientBias + 1.0);
		float f = max(scale * feather / 2.0, 0.0005);
		float dInside = smoothstep(-f, f, d - roundOffset / 2.0);
		float s = max(stroke * minSize, 0.0);
		float dStroke = smoothstep(-f, f, d - roundOffset / 2.0 - s);
		float showStroke = clamp(abs(s) * 100.0, 0.0, 1.0);
		vec4 activeOutline = mix(fill, outlineColor, showStroke);
		vec4 cInside = mix(fill, activeOutline, dInside);
		vec4 cStroke = mix(background, activeOutline, 1.0 - dStroke);
		gl_FragColor = mix(cInside, cStroke, dStroke);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_RoundedRect Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	if (requested > 0) return requested;
	return 160;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":0.0}}) background,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) position,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoReal, {"default":0.5}) scale,
		VuoInputData(VuoReal, {"default":0.0}) rotate,
		VuoInputData(VuoReal, {"default":0.5}) round,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) strokeColor,
		VuoInputData(VuoReal, {"default":0.0}) stroke,
		VuoInputData(VuoReal, {"default":0.0}) feather,
		VuoInputData(VuoReal, {"default":-0.001}) featherBias,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "fill", color);
	VuoShader_setUniform_VuoColor((*instance)->shader, "outlineColor", strokeColor);
	VuoShader_setUniform_VuoColor((*instance)->shader, "background", background);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "center", position);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoReal((*instance)->shader, "roundness", round);
	VuoShader_setUniform_VuoReal((*instance)->shader, "stroke", stroke);
	VuoShader_setUniform_VuoReal((*instance)->shader, "feather", feather);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gradientBias", featherBias);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotate", rotate);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
