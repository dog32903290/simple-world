/**
 * @file
 * my.image.batch.batch30PostfxSource node implementation.
 *
 * Proof-only source image for Batch 30 post-fx nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch30PostfxSource",
					 "description" : "Proof-only source texture for Batch 30 Fxaa/NormalMap/DepthBufferAsGrayScale validation.",
					 "keywords" : [ "tixl", "batch30", "texture2d", "image", "source", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform int mode;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		if (mode == 1)
		{
			float depth = mix(0.05, 0.95, st.x);
			gl_FragColor = vec4(depth, depth, depth, 1.0);
			return;
		}
		float stripe = step(0.5, fract(st.x * 12.0));
		float ramp = st.y;
		vec3 color = mix(vec3(ramp), vec3(1.0 - ramp, 0.15, ramp), stripe);
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Batch30PostfxSource Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger clampDimension(VuoInteger value)
{
	if (value < 1) return 1;
	if (value > 4096) return 4096;
	return value;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":1,"suggestedStep":1}) mode,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoShader_setUniform_VuoInteger((*instance)->shader, "mode", mode);
	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
