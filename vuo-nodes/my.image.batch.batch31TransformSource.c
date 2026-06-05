/**
 * @file
 * my.image.batch.batch31TransformSource node implementation.
 *
 * Proof-only source image for Batch 31 image/transform nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch31TransformSource",
					 "description" : "Proof-only source texture for Batch 31 Crop/MakeTileableImage/MirrorRepeat/TransformImage validation.",
					 "keywords" : [ "tixl", "batch31", "texture2d", "image", "source", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float grid = max(step(0.92, fract(st.x * 8.0)), step(0.92, fract(st.y * 8.0)));
		float diagonal = smoothstep(0.015, 0.0, abs(st.y - st.x));
		vec3 ramp = vec3(st.x, st.y, 1.0 - st.x);
		vec3 color = mix(ramp, vec3(1.0, 0.95, 0.1), grid);
		color = mix(color, vec3(0.1, 0.9, 1.0), diagonal);
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Batch31TransformSource Shader");
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
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
