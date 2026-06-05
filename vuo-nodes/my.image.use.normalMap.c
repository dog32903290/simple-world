/**
 * @file
 * my.image.use.normalMap node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_NormalMap
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/NormalMap.cs
 * - Default: Impact=1, SampleRadius=2, Resolution=(0,0), Twist=180, Mode=0 from NormalMap.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/fx/NormalMap.hlsl.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_NormalMap",
					 "description" : "TiXL NormalMap Vuo shader adapter. Source: external/tixl/Operators/Lib/image/use/NormalMap.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: Impact=1, SampleRadius=2, Resolution=(0,0), Twist=180, Mode=0.",
					 "keywords" : [ "tixl", "texture2d", "image", "normal", "NormalMap.hlsl", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D lightMap;
	uniform vec2 texelSize;
	uniform float impact;
	uniform float sampleRadius;
	uniform float twist;
	uniform int mode;
	varying vec2 fragmentTextureCoordinate;

	float gray(vec4 c)
	{
		return (c.r + c.g + c.b) / 3.0;
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec2 offset = texelSize * sampleRadius;
		vec4 cx1 = texture2D(lightMap, uv + vec2(offset.x, 0.0));
		vec4 cx2 = texture2D(lightMap, uv - vec2(offset.x, 0.0));
		vec4 cy1 = texture2D(lightMap, uv + vec2(0.0, offset.y));
		vec4 cy2 = texture2D(lightMap, uv - vec2(0.0, offset.y));
		vec4 source = texture2D(lightMap, uv);
		vec2 d = mode > 3 ? vec2(gray(cx1) - gray(cx2), gray(cy1) - gray(cy2)) : vec2(cx1.r - cx2.r, cy1.r - cy2.r);
		float angle = (d.x == 0.0 && d.y == 0.0) ? 0.0 : atan(d.x, d.y) + twist / 180.0 * 3.141592;
		float len = length(d);
		vec2 direction = vec2(sin(angle), cos(angle));
		if (mode < 1)
		{
			vec3 normal = normalize(vec3(len * direction * impact, 1.0));
			normal.y = -normal.y;
			gl_FragColor = vec4(normal / 2.0 + 0.5, 1.0);
		}
		else if (mode < 2)
		{
			gl_FragColor = vec4(normalize(vec3(len * direction * impact, 1.0)), 1.0);
		}
		else if (mode < 3)
		{
			gl_FragColor = vec4(mod(-angle, 2.0 * 3.141592), len * impact, 0.0, 1.0);
		}
		else
		{
			gl_FragColor = vec4(len * direction * impact + 0.5, source.b, source.a);
		}
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_NormalMap Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, VuoImage image, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	if (requested > 0) return requested;
	if (image) return width ? image->pixelsWide : image->pixelsHigh;
	return 160;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) lightMap,
		VuoInputData(VuoReal, {"default":1.0,"suggestedMin":0.0,"suggestedMax":10.0,"suggestedStep":0.01}) impact,
		VuoInputData(VuoReal, {"default":2.0,"suggestedMin":0.0,"suggestedMax":32.0,"suggestedStep":0.25}) sampleRadius,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoReal, {"default":180.0,"suggestedMin":-360.0,"suggestedMax":360.0,"suggestedStep":1.0}) twist,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) mode,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	if (!lightMap)
	{
		*output = NULL;
		return;
	}
	VuoInteger renderWidth = renderDimension(resolution, lightMap, true);
	VuoInteger renderHeight = renderDimension(resolution, lightMap, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "lightMap", lightMap);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "texelSize", (VuoPoint2d){1.0 / (VuoReal)renderWidth, 1.0 / (VuoReal)renderHeight});
	VuoShader_setUniform_VuoReal((*instance)->shader, "impact", impact);
	VuoShader_setUniform_VuoReal((*instance)->shader, "sampleRadius", sampleRadius);
	VuoShader_setUniform_VuoReal((*instance)->shader, "twist", twist);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "mode", mode);
	*output = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImage_getColorDepth(lightMap));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
