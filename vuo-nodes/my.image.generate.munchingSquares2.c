/**
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
