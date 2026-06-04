/**
 * @file
 * my.mesh.draw.drawMeshPbrProof node implementation.
 *
 * Vuo body-layer proof adapter for the Material/PBR -> DrawMesh command contract.
 * This is not TiXL DrawMesh parity. It consumes the my_SetMaterial contract text
 * and renders a deterministic preview image so the runtime contract can be
 * tested as real Vuo wiring.
 */

#include "VuoImageRenderer.h"
#include "VuoText.h"
#include <stdio.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_DrawMeshPbrProof",
					 "description" : "Consumes a my_SetMaterial PBR contract and renders a VuoImage proof. This is a Vuo wiring adapter, not full TiXL DrawMesh / MeshBuffers parity.",
					 "keywords" : [ "tixl", "drawmesh", "pbr", "material", "proof", "renderer" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec2 resolution;
	uniform vec4 baseColor;
	uniform float roughness;
	uniform float metal;
	uniform float specular;
	varying vec2 fragmentTextureCoordinate;

	float diamond(vec2 p)
	{
		vec2 q = abs(p);
		return q.x + q.y;
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec2 uv = st * 2.0 - 1.0;
		uv.x *= resolution.x / max(resolution.y, 1.0);
		float d = diamond(uv);

		vec3 background = vec3(0.0);
		if (d > 0.86)
		{
			gl_FragColor = vec4(background, 1.0);
			return;
		}

		vec3 normal = normalize(vec3(-uv.x * 0.55, uv.y * 0.55, 1.0));
		vec3 lightDirection = normalize(vec3(-0.45, 0.72, 0.55));
		float diffuse = max(dot(normal, lightDirection), 0.0);
		float rim = pow(1.0 - max(normal.z, 0.0), 2.0);
		float gloss = pow(max(dot(reflect(-lightDirection, normal), vec3(0.0, 0.0, 1.0)), 0.0), mix(8.0, 64.0, 1.0 - roughness));
		vec3 metalTint = mix(vec3(1.0), baseColor.rgb, clamp(metal, 0.0, 1.0));
		vec3 shaded = baseColor.rgb * (0.18 + diffuse * mix(0.75, 0.45, metal));
		shaded += metalTint * gloss * specular * 0.28;
		shaded += vec3(0.25, 0.42, 0.8) * rim * 0.45;

		float edge = smoothstep(0.86, 0.78, d);
		gl_FragColor = vec4(shaded * edge, baseColor.a);
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

struct MaterialContract
{
	float baseR;
	float baseG;
	float baseB;
	float baseA;
	float roughness;
	float specular;
	float metal;
};

static void setDefaultMaterial(struct MaterialContract *material)
{
	material->baseR = 0.45f;
	material->baseG = 0.80f;
	material->baseB = 1.0f;
	material->baseA = 1.0f;
	material->roughness = 0.25f;
	material->specular = 1.0f;
	material->metal = 0.0f;
}

static bool parseColorObject(const char *start, float *r, float *g, float *b, float *a)
{
	int matches = sscanf(start, "\"baseColor\":{\"r\":%f,\"g\":%f,\"b\":%f,\"a\":%f}", r, g, b, a);
	return matches == 4;
}

static void parseMaterialContract(VuoText materialText, struct MaterialContract *material)
{
	setDefaultMaterial(material);
	if (!VuoText_isPopulated(materialText))
		return;

	const char *baseColor = strstr(materialText, "\"baseColor\":{\"r\":");
	if (baseColor)
		parseColorObject(baseColor, &material->baseR, &material->baseG, &material->baseB, &material->baseA);

	const char *roughnessToken = strstr(materialText, "\"roughness\":");
	const char *specularToken = strstr(materialText, "\"specular\":");
	const char *metalToken = strstr(materialText, "\"metal\":");
	if (roughnessToken)
		sscanf(roughnessToken, "\"roughness\":%f", &material->roughness);
	if (specularToken)
		sscanf(specularToken, "\"specular\":%f", &material->specular);
	if (metalToken)
		sscanf(metalToken, "\"metal\":%f", &material->metal);

	if (material->roughness < 0.f)
		material->roughness = 0.f;
	if (material->roughness > 1.f)
		material->roughness = 1.f;
	if (material->specular < 0.f)
		material->specular = 0.f;
	if (material->metal < 0.f)
		material->metal = 0.f;
	if (material->metal > 1.f)
		material->metal = 1.f;
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_DrawMeshPbrProof Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoText, {"name":"Material","default":""}) Material,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) Width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) Height,
		VuoOutputData(VuoImage, {"name":"Image"}) Image
)
{
	VuoInteger renderWidth = Width < 1 ? 1 : Width;
	VuoInteger renderHeight = Height < 1 ? 1 : Height;
	struct MaterialContract material;
	parseMaterialContract(Material, &material);

	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "resolution", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoColor  ((*instance)->shader, "baseColor", (VuoColor){material.baseR, material.baseG, material.baseB, material.baseA});
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "roughness", material.roughness);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "specular", material.specular);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "metal", material.metal);

	*Image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
