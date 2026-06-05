/**
 * @file
 * my.image.generate.basic.radialGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_RadialGradient
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/RadialGradient.cs
 * - Default: Gradient=white-to-black, Width=1, Stretch=(1,1), Offset=0, PingPong=false, Repeat=false, Center=(0,0), PolarOrientation=false, BiasAndGain=(0.5,0.5), Noise=0, BlendMode=0, Resolution=(0,0), TextureFormat=R16G16B16A16_Float, GenerateMipMaps=false, Image=null from RadialGradient.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/RadialGradient.hlsl.
 * Vuo body-layer limit: TiXL Gradient datatype is represented as colorA/colorB.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_RadialGradient",
					 "description" : "TiXL RadialGradient bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/RadialGradient.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Gradient=white-to-black, Width=1, Stretch=(1,1), Offset=0, PingPong=false, Repeat=false, Center=(0,0), PolarOrientation=false, BiasAndGain=(0.5,0.5), Noise=0, BlendMode=0, Resolution=(0,0), TextureFormat=R16G16B16A16_Float, GenerateMipMaps=false, Image=null.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "radial", "gradient", "RadialGradient.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec2 center;
	uniform float width;
	uniform float offset;
	uniform bool pingPong;
	uniform bool repeat;
	uniform bool polarOrientation;
	uniform vec2 gainAndBias;
	uniform vec2 stretch;
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

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		float w = max(abs(width), 0.000001);
		float c;
		if (!polarOrientation)
		{
			vec2 d = (p - center * vec2(1.0, -1.0)) / max(abs(stretch), vec2(0.0001));
			c = length(d) * 2.0 / w - (pingPong ? 1.0 : 0.5);
		}
		else
		{
			p += center * vec2(-1.0, 1.0);
			p /= max(abs(stretch), vec2(0.0001));
			c = atan(p.x, p.y) / 3.141578 / w;
		}
		c = pingPongRepeat(c - offset);
		if (width < 0.0) c = 1.0 - c;
		gl_FragColor = mix(colorA, colorB, clamp(c, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_RadialGradient Shader");
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
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.99999,"b":1.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.000001,"b":0.000001,"a":1.0}}) colorB,
		VuoInputData(VuoReal, {"default":1.0}) width,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoReal, {"default":0.0}) offset,
		VuoInputData(VuoBoolean, {"default":false}) pingPong,
		VuoInputData(VuoBoolean, {"default":false}) repeat,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) center,
		VuoInputData(VuoBoolean, {"default":false}) polarOrientation,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) biasAndGain,
		VuoInputData(VuoReal, {"default":0.0}) noise,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMipMaps,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "center", center);
	VuoShader_setUniform_VuoReal((*instance)->shader, "width", width);
	VuoShader_setUniform_VuoReal((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "pingPong", pingPong);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "repeat", repeat);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "polarOrientation", polarOrientation);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", biasAndGain);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
