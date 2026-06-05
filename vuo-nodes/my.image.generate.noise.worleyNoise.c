/**
 * @file
 * my.image.generate.noise.worleyNoise node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_WorleyNoise
 * - Category: Operators/Lib/image/generate/noise
 * - Source: external/tixl/Operators/Lib/image/generate/noise/WorleyNoise.cs
 * - Default: Texture=null, TextureBlend=1, ColorA=(1,1,1,1), ColorB=(0,0,0,1), Scale=5, Stretch=(1,1), Offset=(0,0), Phase=5, Randomness=12.6, Clamping=(0,1), GainAndBias=(0.5,0.5), Method=0, Resolution=(512,512), GenerateMips=false from WorleyNoise.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/WorleyNoise.hlsl.
 * Vuo body-layer limit: source texture multiplication is omitted when Texture is null.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_WorleyNoise",
					 "description" : "TiXL WorleyNoise bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/noise/WorleyNoise.cs. Category: Operators/Lib/image/generate/noise. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Texture=null, TextureBlend=1, ColorA=(1,1,1,1), ColorB=(0,0,0,1), Scale=5, Stretch=(1,1), Offset=(0,0), Phase=5, Randomness=12.6, Clamping=(0,1), GainAndBias=(0.5,0.5), Method=0, Resolution=(512,512), GenerateMips=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "noise", "worley", "cellular", "WorleyNoise.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform float scale;
	uniform vec2 stretch;
	uniform vec2 offset;
	uniform float phase;
	uniform float randomness;
	uniform vec2 clamping;
	uniform vec2 gainAndBias;
	uniform int method;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	vec2 hash22(vec2 p)
	{
		vec2 q = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
		return fract(sin(q + phase) * 43758.5453);
	}

	float applyGainAndBias(float v, vec2 gb)
	{
		float bias = clamp(gb.y, 0.0001, 0.9999);
		float g = clamp(gb.x, 0.0001, 0.9999);
		v = pow(clamp(v, 0.00001, 0.99999), log(bias) / log(0.5));
		return v < 0.5 ? 0.5 * pow(2.0 * v, log(g) / log(0.5)) : 1.0 - 0.5 * pow(2.0 - 2.0 * v, log(g) / log(0.5));
	}

	float metric(vec2 a, vec2 b, int kind)
	{
		vec2 d = abs(a - b);
		if (kind == 1) return d.x + d.y;
		if (kind == 2) return max(d.x, d.y);
		return length(d);
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 uv = fragmentTextureCoordinate + vec2(0.5);
		uv = uv * max(stretch, vec2(0.0001)) * max(scale, 0.0001);
		uv.x *= aspectRatio;
		vec2 q = uv - offset * vec2(1.0, -1.0) * scale * stretch;
		int metricKind = method == 1 || method == 4 ? 1 : (method == 2 || method == 5 ? 2 : 0);
		bool f2MinusF1 = method >= 3;
		float f1 = 9999.0;
		float f2 = 9999.0;
		for (int j = -1; j <= 1; ++j)
		for (int i = -1; i <= 1; ++i)
		{
			vec2 cell = floor(q) + vec2(float(i), float(j));
			vec2 h = hash22(cell);
			vec2 g = cell + 0.5 + 0.5 * sin(h * randomness);
			float d = metric(q, g, metricKind);
			if (d < f1)
			{
				f2 = f1;
				f1 = d;
			}
			else if (d < f2)
			{
				f2 = d;
			}
		}
		float v = f2MinusF1 ? (f2 - f1) : f1;
		v = applyGainAndBias(v, gainAndBias);
		v = clamp(v, clamping.x, clamping.y);
		gl_FragColor = mix(colorB, colorA, v);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_WorleyNoise Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 512;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) texture,
		VuoInputData(VuoReal, {"default":1.0}) textureBlend,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.9999899,"b":0.9999899,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) colorB,
		VuoInputData(VuoReal, {"default":5.0}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offset,
		VuoInputData(VuoReal, {"default":5.0}) phase,
		VuoInputData(VuoReal, {"default":12.6}) randomness,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":1.0}}) clamping,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) gainAndBias,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":5,"suggestedStep":1}) method,
		VuoInputData(VuoPoint2d, {"default":{"x":512.0,"y":512.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoReal((*instance)->shader, "phase", phase);
	VuoShader_setUniform_VuoReal((*instance)->shader, "randomness", randomness);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "clamping", clamping);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", gainAndBias);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "method", method);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
