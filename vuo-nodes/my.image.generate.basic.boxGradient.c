/**
 * @file
 * my.image.generate.basic.boxGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_BoxGradient
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/BoxGradient.cs
 * - Default: Image=null, Rotation=0, Center=(0,0), Size=(0.25,0.25), UniformScale=1, CornersRadius=(0,0,0,0), Gradient=white-to-black, GradientWidth=1, Offset=0, PingPong=true, Repeat=false, GainAndBias=(0.5,0.5), BlendMode=0, Resolution=(0,0) from BoxGradient.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/BoxGradient.hlsl.
 * Vuo body-layer limit: TiXL Gradient datatype is represented as colorA/colorB.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_BoxGradient",
					 "description" : "TiXL BoxGradient bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/BoxGradient.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Rotation=0, Center=(0,0), Size=(0.25,0.25), UniformScale=1, CornersRadius=(0,0,0,0), Gradient=white-to-black, GradientWidth=1, Offset=0, PingPong=true, Repeat=false, GainAndBias=(0.5,0.5), BlendMode=0, Resolution=(0,0).",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "box", "gradient", "BoxGradient.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec2 center;
	uniform vec2 size;
	uniform vec4 cornersRadius;
	uniform float rotation;
	uniform float uniformScale;
	uniform float width;
	uniform float offset;
	uniform bool pingPong;
	uniform bool repeat;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float pingPongRepeat(float x)
	{
		float baseValue = x;
		float repeatValue = fract(baseValue);
		float pingPongValue = 1.0 - abs(fract(x * 0.5) * 2.0 - 1.0);
		float singlePingPong = abs(x);
		float value = repeat ? repeatValue : baseValue;
		value = pingPong ? (repeat ? pingPongValue : singlePingPong) : value;
		return repeat ? value : clamp(value, 0.0, 1.0);
	}

	float sdRoundedBox(vec2 p, vec2 b, vec4 r)
	{
		r.xy = (p.x > 0.0) ? r.xy : r.zw;
		r.x = (p.y > 0.0) ? r.x : r.y;
		vec2 q = abs(p) - b + r.x;
		return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		p += center * vec2(-1.0, 1.0);
		float a = rotation / 180.0 * 3.141578;
		float ca = cos(a);
		float sa = sin(a);
		p = vec2(p.x * ca + p.y * sa, p.x * sa - p.y * ca);
		float c = sdRoundedBox(p, size * uniformScale, cornersRadius * uniformScale) * 2.0 - offset * width;
		c = pingPongRepeat(c / max(abs(width), 0.0001));
		gl_FragColor = mix(colorA, colorB, clamp(c, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_BoxGradient Shader");
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
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.99999,"b":1.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.000001,"b":0.000001,"a":1.0}}) colorB,
		VuoInputData(VuoReal, {"default":0.0}) rotation,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) center,
		VuoInputData(VuoPoint2d, {"default":{"x":0.25,"y":0.25}}) size,
		VuoInputData(VuoReal, {"default":1.0}) uniformScale,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":0.0}}) cornersRadius,
		VuoInputData(VuoReal, {"default":1.0}) gradientWidth,
		VuoInputData(VuoReal, {"default":0.0}) offset,
		VuoInputData(VuoBoolean, {"default":true}) pingPong,
		VuoInputData(VuoBoolean, {"default":false}) repeat,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) gainAndBias,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "center", center);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "size", size);
	VuoShader_setUniform_VuoColor((*instance)->shader, "cornersRadius", cornersRadius);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotation", rotation);
	VuoShader_setUniform_VuoReal((*instance)->shader, "uniformScale", uniformScale);
	VuoShader_setUniform_VuoReal((*instance)->shader, "width", gradientWidth);
	VuoShader_setUniform_VuoReal((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "pingPong", pingPong);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "repeat", repeat);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
