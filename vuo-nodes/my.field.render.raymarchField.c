/**
 * @file
 * my.field.render.raymarchField node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/field/render/RaymarchField.
 */

#include "VuoImageRenderer.h"
#include "VuoText.h"
#include <stdio.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_RaymarchField",
					 "description" : "TiXL RaymarchField render adapter. Category: Operators/Lib/field/render. Primary TiXL output: Command.",
					 "keywords" : [ "tixl", "field", "sdf", "raymarch", "command", "shadergraphnode" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec2 resolution;
	uniform int fieldMode;
	uniform int combineMethod;
	uniform float k;
	uniform vec3 center;
	uniform float radius;
	uniform vec3 boxCenter;
	uniform vec3 boxSize;
	uniform float boxUniformScale;
	uniform float boxEdgeRadius;
	uniform vec4 color;
	varying vec2 fragmentTextureCoordinate;

	float sdSphere(vec3 p, vec3 c, float r)
	{
		return length(p - c) - r;
	}

	float sdBox(vec3 p, vec3 c, vec3 size, float uniformScale, float edgeRadius)
	{
		vec3 halfSize = max(size * uniformScale * 0.5, vec3(0.001));
		float safeEdge = max(edgeRadius, 0.0);
		vec3 q = abs(p - c) - halfSize + safeEdge;
		return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0) - safeEdge;
	}

	float fOpUnionRound(float a, float b, float r)
	{
		if (r <= 0.0001)
			return min(a, b);
		vec2 u = max(vec2(r - a, r - b), vec2(0.0));
		return max(r, min(a, b)) - length(u);
	}

	float fOpUnionSmooth(float a, float b, float smoothK)
	{
		if (smoothK <= 0.0001)
			return min(a, b);
		float h = max(smoothK - abs(a - b), 0.0);
		return min(a, b) - (h * h) / (4.0 * smoothK);
	}

	float fOpUnionChamfer(float a, float b, float r)
	{
		return min(min(a, b), (a - r + b) * sqrt(0.5));
	}

	float fOpIntersectionRound(float a, float b, float r)
	{
		if (r <= 0.0001)
			return max(a, b);
		vec2 u = max(vec2(r + a, r + b), vec2(0.0));
		return min(-r, max(a, b)) + length(u);
	}

	float fOpDifferenceRound(float a, float b, float r)
	{
		return fOpIntersectionRound(a, -b, r);
	}

	float combineSdf(float a, float b, int method, float blendK)
	{
		if (method == 1)
			return fOpUnionSmooth(a, b, blendK);
		if (method == 2)
			return fOpUnionRound(a, b, blendK);
		if (method == 3)
			return fOpUnionChamfer(a, b, blendK);
		if (method == 4)
			return fOpUnionSmooth(a, b, blendK);
		if (method == 5)
			return max(a, -b);
		if (method == 6)
			return fOpDifferenceRound(a, b, blendK);
		if (method == 7)
			return max(max(a, -b), (a + blendK - b) * sqrt(0.5));
		if (method == 8)
			return max(a, b);
		if (method == 9)
			return fOpIntersectionRound(a, b, blendK);
		if (method == 10)
			return max(max(a, b), (a + blendK + b) * sqrt(0.5));
		if (method == 11)
			return length(vec2(a, b)) - blendK;
		if (method == 12)
			return max(a, (a + blendK - abs(b)) * sqrt(0.5));
		return min(a, b);
	}

	float fieldDistance(vec3 p)
	{
		float sphereDistance = sdSphere(p, center, radius);
		if (fieldMode == 1)
		{
			float boxDistance = sdBox(p, boxCenter, boxSize, boxUniformScale, boxEdgeRadius);
			return combineSdf(sphereDistance, boxDistance, combineMethod, k);
		}
		if (fieldMode == 2)
			return sdBox(p, boxCenter, boxSize, boxUniformScale, boxEdgeRadius);
		return sphereDistance;
	}

	vec3 estimateNormal(vec3 p)
	{
		float e = 0.0015;
		return normalize(vec3(
				fieldDistance(p + vec3(e, 0.0, 0.0)) - fieldDistance(p - vec3(e, 0.0, 0.0)),
				fieldDistance(p + vec3(0.0, e, 0.0)) - fieldDistance(p - vec3(0.0, e, 0.0)),
				fieldDistance(p + vec3(0.0, 0.0, e)) - fieldDistance(p - vec3(0.0, 0.0, e))));
	}

	float raymarch(vec3 rayOrigin, vec3 rayDirection)
	{
		float totalDistance = 0.;
		for (int i = 0; i < 100; ++i)
		{
			vec3 p = rayOrigin + rayDirection * totalDistance;
			float distanceToField = fieldDistance(p);
			if (distanceToField < 0.002)
				return totalDistance;
			totalDistance += max(distanceToField, 0.001);
			if (totalDistance > 300.)
				break;
		}
		return -1.;
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec2 uv = st * 2. - 1.;
		uv.x *= resolution.x / max(resolution.y, 1.);
		vec3 background = mix(vec3(0.08, 0.12, 0.25), vec3(0.55, 0.12, 0.75), clamp(st.y, 0., 1.));
		background += vec3(clamp(st.x, 0., 1.) * 0.18, 0., 0.08);

		vec3 rayOrigin = vec3(0., 0., -3.);
		vec3 rayDirection = normalize(vec3(uv, 1.8));
		float hit = raymarch(rayOrigin, rayDirection);

		if (hit < 0.)
		{
			float grid = step(0.985, max(fract(st.x * 12.), fract(st.y * 8.)));
			gl_FragColor = vec4(background + grid * 0.12, 1.);
			return;
		}

		vec3 p = rayOrigin + rayDirection * hit;
		vec3 normal = estimateNormal(p);
		vec3 lightDirection = normalize(vec3(-0.4, 0.8, -0.6));
		float diffuse = max(dot(normal, lightDirection), 0.);
		float rim = pow(1. - max(dot(normal, -rayDirection), 0.), 2.5);
		vec3 shaded = color.rgb * (0.28 + diffuse * 0.95) + rim * vec3(0.4, 0.7, 1.0);
		gl_FragColor = vec4(mix(background, shaded, 0.92), color.a);
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

struct SdfContract
{
	int fieldMode;
	int combineMethod;
	float k;
	float sphereCenterX;
	float sphereCenterY;
	float sphereCenterZ;
	float sphereRadius;
	float boxCenterX;
	float boxCenterY;
	float boxCenterZ;
	float boxSizeX;
	float boxSizeY;
	float boxSizeZ;
	float boxUniformScale;
	float boxEdgeRadius;
};

/* Contract tags accepted by this bounded adapter: "node":"CombineSDF", "node":"BoxSDF". */

static void setDefaultSdfContract(struct SdfContract *contract)
{
	contract->fieldMode = 0;
	contract->combineMethod = 2;
	contract->k = 0.f;
	contract->sphereCenterX = 0.f;
	contract->sphereCenterY = 0.f;
	contract->sphereCenterZ = 0.f;
	contract->sphereRadius = 0.5f;
	contract->boxCenterX = 0.f;
	contract->boxCenterY = 0.f;
	contract->boxCenterZ = 0.f;
	contract->boxSizeX = 1.f;
	contract->boxSizeY = 1.f;
	contract->boxSizeZ = 1.f;
	contract->boxUniformScale = 1.f;
	contract->boxEdgeRadius = 0.05f;
}

static bool parseSphereSdfContract(const char *sdfField, float *centerX, float *centerY, float *centerZ, float *radius)
{
	if (!VuoText_isPopulated(sdfField))
		return false;

	const char *center = strstr(sdfField, "\"center\":{\"x\":");
	const char *radiusToken = strstr(sdfField, "\"radius\":");
	if (!center || !radiusToken)
		return false;

	int centerMatches = sscanf(center, "\"center\":{\"x\":%f,\"y\":%f,\"z\":%f}", centerX, centerY, centerZ);
	int radiusMatches = sscanf(radiusToken, "\"radius\":%f", radius);
	return centerMatches == 3 && radiusMatches == 1;
}

static bool parseBoxSdfContract(const char *sdfField, float *centerX, float *centerY, float *centerZ, float *sizeX, float *sizeY, float *sizeZ, float *uniformScale, float *edgeRadius)
{
	if (!VuoText_isPopulated(sdfField))
		return false;

	const char *boxToken = strstr(sdfField, "\"node\":\"BoxSDF\"");
	if (!boxToken)
		return false;

	const char *center = strstr(boxToken, "\"center\":{\"x\":");
	const char *size = strstr(boxToken, "\"size\":{\"x\":");
	const char *uniformScaleToken = strstr(boxToken, "\"uniformScale\":");
	const char *edgeRadiusToken = strstr(boxToken, "\"edgeRadius\":");
	if (!center || !size || !uniformScaleToken || !edgeRadiusToken)
		return false;

	int centerMatches = sscanf(center, "\"center\":{\"x\":%f,\"y\":%f,\"z\":%f}", centerX, centerY, centerZ);
	int sizeMatches = sscanf(size, "\"size\":{\"x\":%f,\"y\":%f,\"z\":%f}", sizeX, sizeY, sizeZ);
	int uniformScaleMatches = sscanf(uniformScaleToken, "\"uniformScale\":%f", uniformScale);
	int edgeRadiusMatches = sscanf(edgeRadiusToken, "\"edgeRadius\":%f", edgeRadius);
	return centerMatches == 3 && sizeMatches == 3 && uniformScaleMatches == 1 && edgeRadiusMatches == 1;
}

static bool parseCombineSdfContract(const char *sdfField, int *combineMethod, float *k)
{
	if (!VuoText_isPopulated(sdfField))
		return false;

	if (!strstr(sdfField, "\"node\":\"CombineSDF\""))
		return false;

	const char *combineMethodToken = strstr(sdfField, "\"combineMethod\":");
	const char *kToken = strstr(sdfField, "\"k\":");
	if (!combineMethodToken || !kToken)
		return false;

	int methodMatches = sscanf(combineMethodToken, "\"combineMethod\":%d", combineMethod);
	int kMatches = sscanf(kToken, "\"k\":%f", k);
	return methodMatches == 1 && kMatches == 1;
}

static void parseSdfContract(VuoText sdfField, struct SdfContract *contract)
{
	setDefaultSdfContract(contract);

	bool hasSphere = parseSphereSdfContract(sdfField, &contract->sphereCenterX, &contract->sphereCenterY, &contract->sphereCenterZ, &contract->sphereRadius);
	bool hasBox = parseBoxSdfContract(sdfField, &contract->boxCenterX, &contract->boxCenterY, &contract->boxCenterZ, &contract->boxSizeX, &contract->boxSizeY, &contract->boxSizeZ, &contract->boxUniformScale, &contract->boxEdgeRadius);

	if (parseCombineSdfContract(sdfField, &contract->combineMethod, &contract->k) && hasSphere && hasBox)
		contract->fieldMode = 1;
	else if (hasBox && !hasSphere)
		contract->fieldMode = 2;
	else
		contract->fieldMode = 0;

	if (contract->combineMethod < 0)
		contract->combineMethod = 0;
	if (contract->combineMethod > 12)
		contract->combineMethod = 12;
	if (contract->k < 0.f)
		contract->k = 0.f;
	if (contract->sphereRadius < 0.001f)
		contract->sphereRadius = 0.001f;
	if (contract->boxUniformScale < 0.001f)
		contract->boxUniformScale = 0.001f;
	if (contract->boxEdgeRadius < 0.f)
		contract->boxEdgeRadius = 0.f;
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_RaymarchField Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoText, {"name":"SdfField","default":""}) sdfField,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"DrawCommand"}) drawCommand
)
{
	VuoInteger renderWidth = width < 1 ? 1 : width;
	VuoInteger renderHeight = height < 1 ? 1 : height;
	struct SdfContract contract;
	parseSdfContract(sdfField, &contract);
	float renderRadius = contract.sphereRadius;

	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "resolution", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoInteger ((*instance)->shader, "fieldMode", contract.fieldMode);
	VuoShader_setUniform_VuoInteger ((*instance)->shader, "combineMethod", contract.combineMethod);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "k", contract.k);
	VuoShader_setUniform_VuoPoint3d((*instance)->shader, "center", VuoPoint3d_make(contract.sphereCenterX, contract.sphereCenterY, contract.sphereCenterZ));
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "radius", renderRadius);
	VuoShader_setUniform_VuoPoint3d((*instance)->shader, "boxCenter", VuoPoint3d_make(contract.boxCenterX, contract.boxCenterY, contract.boxCenterZ));
	VuoShader_setUniform_VuoPoint3d((*instance)->shader, "boxSize", VuoPoint3d_make(contract.boxSizeX, contract.boxSizeY, contract.boxSizeZ));
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "boxUniformScale", contract.boxUniformScale);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "boxEdgeRadius", contract.boxEdgeRadius);
	VuoShader_setUniform_VuoColor  ((*instance)->shader, "color", color);

	*drawCommand = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
