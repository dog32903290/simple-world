/**
 * @file
 * my.image.fx.blur.sharpen node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Sharpen
 * - Category: Operators/Lib/image/fx/blur
 * - Source: external/tixl/Operators/Lib/image/fx/blur/Sharpen.cs
 * - Default: Image=null, SampleRadius=1, Strength=1, Clamping=false from Sharpen.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for TiXL blur shader evidence including Sharpen.hlsl.
 * Vuo body-layer limit: multi-pass downsample/upsample, gradients, wrap modes, mips, and auxiliary FxTextures are approximated in a single-pass image filter.
 */

VuoModuleMetadata({
					 "title" : "my_Sharpen",
					 "description" : "TiXL Sharpen bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/fx/blur/Sharpen.cs. Category: Operators/Lib/image/fx/blur. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, SampleRadius=1, Strength=1, Clamping=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "fx", "blur", "Sharpen", "Sharpen.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>


static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D image;
	uniform float size;
	uniform float samples;
	uniform float angle;
	uniform float intensity;
	uniform float threshold;
	uniform int filterKind;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	vec4 source(vec2 uv)
	{
		return texture2D(image, clamp(uv, 0.0, 1.0));
	}

	vec4 boxBlur(vec2 uv, vec2 dir, float radius, int count)
	{
		vec4 sum = vec4(0.0);
		float norm = 0.0;
		for (int i = -8; i <= 8; ++i)
		{
			if (abs(i) <= count)
			{
				float w = 1.0 - abs(float(i)) / float(count + 1);
				sum += source(uv + dir * float(i) * radius) * w;
				norm += w;
			}
		}
		return sum / max(norm, 0.0001);
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec2 px = 1.0 / max(targetSize, vec2(1.0));
		vec4 original = source(uv);
		vec4 color = original;
		if (filterKind == 0)
		{
			vec4 blurred = boxBlur(uv, px, max(size, 0.0) * 2.5, 8);
			float luma = dot(blurred.rgb, vec3(0.299, 0.587, 0.114));
			vec3 glow = max(blurred.rgb - threshold, 0.0) * intensity;
			color = vec4(clamp(original.rgb + glow * smoothstep(threshold, 1.0, luma), 0.0, 1.0), original.a);
		}
		else if (filterKind == 1)
		{
			int count = int(clamp(samples, 1.0, 8.0));
			color = mix(original, boxBlur(uv, px, max(size, 0.0) * 2.0, count), 0.92);
		}
		else if (filterKind == 2)
		{
			float a = angle / 180.0 * 3.14159265;
			vec2 dir = vec2(cos(a), sin(a)) * px;
			int count = int(clamp(samples, 1.0, 8.0));
			color = boxBlur(uv, dir, max(size, 0.0) * 3.0, count);
		}
		else if (filterKind == 3)
		{
			vec4 a = boxBlur(uv, px, max(size, 1.0) * 3.0, 6);
			vec4 b = boxBlur(uv, px.yx, max(size, 1.0) * 3.0, 6);
			color = (a + b) * 0.5;
		}
		else
		{
			vec4 blurred = boxBlur(uv, px, max(size, 0.0) * 1.5, 4);
			color = vec4(clamp(original.rgb + (original.rgb - blurred.rgb) * intensity, 0.0, 1.0), original.a);
		}
		gl_FragColor = color;
	}
);

struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Sharpen Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}
static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 320;
}
static VuoImage imageOrFallback(VuoImage image, VuoInteger width, VuoInteger height)
{
	if (image) return image;
	return VuoImage_makeColorImage((VuoColor){0.5, 0.5, 0.5, 1.0}, (unsigned int)width, (unsigned int)height);
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoReal, {"default":1.0}) size,
		VuoInputData(VuoReal, {"default":8.0}) samples,
		VuoInputData(VuoReal, {"default":0.0}) angle,
		VuoInputData(VuoReal, {"default":6.0}) intensity,
		VuoInputData(VuoReal, {"default":0.5}) threshold,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "image", imageOrFallback(image, renderWidth, renderHeight));
	VuoShader_setUniform_VuoReal((*instance)->shader, "size", size);
	VuoShader_setUniform_VuoReal((*instance)->shader, "samples", samples);
	VuoShader_setUniform_VuoReal((*instance)->shader, "angle", angle);
	VuoShader_setUniform_VuoReal((*instance)->shader, "intensity", intensity);
	VuoShader_setUniform_VuoReal((*instance)->shader, "threshold", threshold);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "filterKind", 4);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
