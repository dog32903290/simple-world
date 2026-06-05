/**
 * @file
 * my.image.generate.noise.shardNoise node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_ShardNoise
 * - Category: Operators/Lib/image/generate/noise
 * - Source: external/tixl/Operators/Lib/image/generate/noise/ShardNoise.cs
 * - Default: ColorA=(0,0,0,1), ColorB=(1,1,1,1), GainAndBias=(0.5,0.5), Scale=10, Stretch=(2,2), Offset=(0,0), Direction=(0,0), Phase=0, Sharpen=1, Rate=2, Method=0, Resolution=(256,256), GenerateMips=false from ShardNoise.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/ShardNoise.hlsl.
 * Vuo body-layer limit: TiXL octaves are reduced to a compact shard/cell approximation.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_ShardNoise",
					 "description" : "TiXL ShardNoise bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/noise/ShardNoise.cs. Category: Operators/Lib/image/generate/noise. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: ColorA=(0,0,0,1), ColorB=(1,1,1,1), GainAndBias=(0.5,0.5), Scale=10, Stretch=(2,2), Offset=(0,0), Direction=(0,0), Phase=0, Sharpen=1, Rate=2, Method=0, Resolution=(256,256), GenerateMips=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "noise", "shard", "ShardNoise.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec2 gainAndBias;
	uniform float scale;
	uniform vec2 stretch;
	uniform vec2 offset;
	uniform vec2 direction;
	uniform float phase;
	uniform float sharpen;
	uniform float rate;
	uniform int method;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float hash(vec3 p)
	{
		p = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)));
		return fract(sin(p.x + p.y + p.z) * 43758.5453);
	}

	float shard(vec3 p, float sharpness)
	{
		vec3 ip = floor(p);
		vec3 fp = fract(p);
		float v = 0.0;
		float t = 0.0;
		for (int z = -1; z <= 1; ++z)
		for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			vec3 o = vec3(float(x), float(y), float(z));
			vec3 h = vec3(hash(ip + o), hash(ip + o + 11.0), hash(ip + o + 31.0));
			vec3 r = fp - (o + h);
			float w = exp(-6.28318 * dot(r, r));
			float s = sharpness * dot(r, vec3(hash(ip + o + 47.0), hash(ip + o + 71.0), hash(ip + o + 97.0)) - 0.5);
			v += w * s * inversesqrt(1.0 + s * s);
			t += w;
		}
		return (v / max(t, 0.0001)) * 0.5 + 0.5;
	}

	float applyGainAndBias(float v, vec2 gb)
	{
		float bias = clamp(gb.y, 0.0001, 0.9999);
		float gain = clamp(gb.x, 0.0001, 0.9999);
		v = pow(clamp(v, 0.00001, 0.99999), log(bias) / log(0.5));
		return v < 0.5 ? 0.5 * pow(2.0 * v, log(gain) / log(0.5)) : 1.0 - 0.5 * pow(2.0 - 2.0 * v, log(gain) / log(0.5));
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		p = (p + offset * vec2(-1.0, 1.0) + direction * phase * vec2(-1.0, 1.0)) / max(stretch, vec2(0.0001));
		vec3 uv = vec3(p, phase * 0.05 * rate);
		float base = applyGainAndBias(shard(max(scale, 0.0001) * uv, max(sharpen, 0.0001) * 128.0), gainAndBias);
		float octave = applyGainAndBias(shard(4.0 * uv, 4.0) * 0.5 + shard(8.0 * uv, 4.0) * 0.25 + shard(16.0 * uv, 4.0) * 0.125, gainAndBias);
		float c = method == 1 ? base * octave : (method == 2 ? octave : base);
		gl_FragColor = mix(colorA, colorB, clamp(c, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_ShardNoise Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 256;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.000001,"b":0.000001,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.99999,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) gainAndBias,
		VuoInputData(VuoReal, {"default":10.0}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":2.0,"y":2.0}}) stretch,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offset,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) direction,
		VuoInputData(VuoReal, {"default":0.0}) phase,
		VuoInputData(VuoReal, {"default":1.0}) sharpen,
		VuoInputData(VuoReal, {"default":2.0}) rate,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":2,"suggestedStep":1}) method,
		VuoInputData(VuoPoint2d, {"default":{"x":256.0,"y":256.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", gainAndBias);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "direction", direction);
	VuoShader_setUniform_VuoReal((*instance)->shader, "phase", phase);
	VuoShader_setUniform_VuoReal((*instance)->shader, "sharpen", sharpen);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rate", rate);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "method", method);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
