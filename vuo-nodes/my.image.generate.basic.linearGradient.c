/**
 * @file
 * my.image.generate.basic.linearGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_LinearGradient
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/LinearGradient.cs
 * - Default: Gradient=black-to-white, Width=1, SizeMode=0, Offset=0, OffsetMode=0, PingPong=false, Repeat=false, Rotate=90, Center=(0,0), GainAndBias=(0.5,0.5), BlendMode=0, Resolution=(0,0), GenerateMips=false, TextureFormat=R16G16B16A16_Float, Image=null from LinearGradient.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/LinearGradient.hlsl.
 * Vuo body-layer limit: TiXL Gradient datatype is represented as colorA/colorB.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_LinearGradient",
					 "description" : "TiXL LinearGradient bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/LinearGradient.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Gradient=black-to-white, Width=1, SizeMode=0, Offset=0, OffsetMode=0, PingPong=false, Repeat=false, Rotate=90, Center=(0,0), GainAndBias=(0.5,0.5), BlendMode=0, Resolution=(0,0), GenerateMips=false, TextureFormat=R16G16B16A16_Float, Image=null.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "linear", "gradient", "LinearGradient.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec2 center;
	uniform float width;
	uniform float rotation;
	uniform bool pingPong;
	uniform bool repeat;
	uniform vec2 gainAndBias;
	uniform float offset;
	uniform int sizeMode;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float pingPongRepeat(float x)
	{
		float baseValue = x + 0.5;
		float repeatValue = fract(baseValue);
		float pingPongValue = 1.0 - abs(fract(x * 0.5) * 2.0 - 1.0);
		float singlePingPong = abs(x);
		float pingPongOutput = repeat ? pingPongValue : singlePingPong;
		float value = repeat ? repeatValue : baseValue;
		value = pingPong ? pingPongOutput : value;
		return repeat ? value : clamp(value, 0.0, 1.0);
	}

	float applyGainAndBias(float v, vec2 gb)
	{
		float bias = clamp(gb.y, 0.0001, 0.9999);
		float gain = clamp(gb.x, 0.0001, 0.9999);
		v = pow(v, log(bias) / log(0.5));
		return v < 0.5 ? pow(2.0 * v, log(gain) / log(0.5)) * 0.5 : 1.0 - pow(2.0 - 2.0 * v, log(gain) / log(0.5)) * 0.5;
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		if (sizeMode == 0) p.x *= aspectRatio; else p.y /= aspectRatio;
		float radians = rotation / 180.0 * 3.141578;
		vec2 angle = vec2(sin(radians), cos(radians));
		float c = dot(p - center, angle) + offset;
		c = pingPongRepeat(c / max(abs(width), 0.0001));
		c = applyGainAndBias(clamp(c, 0.000001, 0.99999), gainAndBias);
		gl_FragColor = mix(colorA, colorB, c);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_LinearGradient Shader");
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
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.000001,"b":0.000001,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.99999,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoReal, {"default":1.0}) width,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":1,"suggestedStep":1}) sizeMode,
		VuoInputData(VuoReal, {"default":0.0}) offset,
		VuoInputData(VuoBoolean, {"default":false}) pingPong,
		VuoInputData(VuoBoolean, {"default":false}) repeat,
		VuoInputData(VuoReal, {"default":90.0}) rotate,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) center,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) gainAndBias,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "center", center);
	VuoShader_setUniform_VuoReal((*instance)->shader, "width", width);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotation", rotate);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "pingPong", pingPong);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "repeat", repeat);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", gainAndBias);
	VuoShader_setUniform_VuoReal((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "sizeMode", sizeMode);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
