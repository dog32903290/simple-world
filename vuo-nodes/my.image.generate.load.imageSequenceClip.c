/**
 * @file
 * my.image.generate.load.imageSequenceClip node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_ImageSequenceClip
 * - Category: Operators/Lib/image/generate/load
 * - Source: external/tixl/Operators/Lib/image/generate/load/ImageSequenceClip.cs
 * - Default evidence: external/tixl/Operators/Lib/image/generate/load/ImageSequenceClip.t3
 * - Primary output evidence: OutputImage; Texture2D proof color ColorForTextures #9F008A unless noted in acceptance docs.
 *
 * Bounded Vuo body-layer adapter for TiXL evidence `ImageSequenceClip.cs`.
 * Limits: DX11 resources, compute passes, command streams, file/network IO, history buffers, shadergraph fields, and exact multi-output GPU state are represented by a single-pass visual proof.
 */

VuoModuleMetadata({
 "title":"my_ImageSequenceClip",
 "description":"TiXL ImageSequenceClip bounded Vuo adapter. Source: external/tixl/Operators/Lib/image/generate/load/ImageSequenceClip.cs. Category: Operators/Lib/image/generate/load. Primary output evidence: OutputImage. Shader/resource evidence: ImageSequenceClip.cs.",
 "keywords":["tixl","image","Texture2D","ImageSequenceClip","ImageSequenceClip.cs","bounded approximation","ColorForTextures","#9F008A"],
 "version":"1.0.0",
 "dependencies":["VuoImageRenderer"]
});

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D sourceImage;
	uniform int filterKind;
	uniform vec2 targetSize;
	uniform float amount;
	uniform float scale;
	uniform float phase;
	uniform float threshold;
	uniform vec2 center;
	uniform vec4 colorA;
	uniform vec4 colorB;
	varying vec2 fragmentTextureCoordinate;

	float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + float(filterKind) * 0.123) * 43758.5453); }
	vec4 source(vec2 uv) { return texture2D(sourceImage, clamp(uv, 0.0, 1.0)); }
	float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec2 p = uv - 0.5 - center * 0.2;
		vec2 px = 1.0 / max(targetSize, vec2(1.0));
		vec4 original = source(uv);
		vec4 color = original;
		float k = float(filterKind);
		float family = floor(k / 10.0);
		float local = mod(k, 10.0);

		if (family == 0.0)
		{
			if (local == 0.0) { float r=length(p); vec2 z=p/(1.0+amount*smoothstep(0.45,0.02,r)); color=source(z+0.5); }
			else if (local == 1.0) { float d=length(p); color.r=source(uv+p*d*0.08*amount).r; color.g=source(uv).g; color.b=source(uv-p*d*0.08*amount).b; }
			else if (local == 2.0 || local == 8.0) { vec2 m=vec2(source(uv+vec2(0.03,0)).r, source(uv+vec2(0,0.03)).g)-0.5; color=source(uv+m*amount*0.18); }
			else if (local == 3.0) { vec2 m=vec2(luma(source(uv+px*4.0).rgb)-luma(source(uv-px*4.0).rgb)); color=source(uv+m*amount*0.2); color.rgb+=m.x*colorA.rgb; }
			else if (local == 4.0) { float a=atan(p.y,p.x)+phase; vec2 q=vec2(abs(p.x), p.y); color=source(q+0.5+vec2(sin(a)*0.05,0)); }
			else if (local == 5.0) { float v=sin((uv.x+phase)*scale*12.0)*sin((uv.y-phase)*scale*12.0); color=mix(colorA,colorB,0.5+0.5*v); }
			else if (local == 6.0) { float a=atan(p.y,p.x); float seg=6.0; a=mod(a,6.28318/seg); a=abs(a-3.14159/seg); vec2 q=vec2(cos(a),sin(a))*length(p)*scale+0.5; color=source(q); }
			else { float r=length(p)*scale; float a=atan(p.y,p.x)/6.28318+0.5; color=source(vec2(a,r)); }
		}
		else if (family == 2.0 || family == 3.0)
		{
			if (local == 0.0) { float cell=floor(luma(original.rgb)*8.0); color=mix(colorA,colorB,mod(cell,2.0)); }
			else if (local == 1.0) { float d=length(p); color.r=source(uv+p*d*0.08).r; color.g=source(uv).g; color.b=source(uv-p*d*0.08).b; }
			else if (local == 2.0) { float n=hash(floor(uv*targetSize/8.0)); color=mix(original, vec4(n*colorA.rgb,1),0.45); }
			else if (local == 3.0) { vec3 sx=source(uv+vec2(px.x*2.0,0)).rgb-source(uv-vec2(px.x*2.0,0)).rgb; vec3 sy=source(uv+vec2(0,px.y*2.0)).rgb-source(uv-vec2(0,px.y*2.0)).rgb; color=vec4(vec3(length(sx)+length(sy))*colorA.rgb,1); }
			else if (local == 4.0) { float g=floor(luma(original.rgb)*4.0)/3.0; float d=step(fract((uv.x+uv.y)*80.0),g); color=mix(colorA,colorB,d); }
			else if (local == 5.0) { float h=luma(source(uv+vec2(0.02,0)).rgb)-luma(source(uv-vec2(0.02,0)).rgb); color.rgb=original.rgb+h*colorA.rgb*amount; }
			else if (local == 6.0) { vec4 b=source(uv+px*8.0); float glow=max(luma(b.rgb)-threshold,0.0); color.rgb+=glow*colorA.rgb*amount; }
			else if (local == 7.0) { vec2 gv=fract(uv*scale*8.0)-0.5; float edge=smoothstep(0.44,0.48,abs(gv.x)+abs(gv.y)); color=mix(original,colorA,edge*0.7); }
			else if (local == 8.0) { vec2 dir=normalize(center+vec2(0.3,0.2)); vec4 rays=vec4(0); for(int i=0;i<8;i++) rays+=source(uv-dir*float(i)*0.02); color=mix(original,rays/8.0*colorA*amount,0.75); }
			else { vec2 q=floor(uv*scale*10.0)/(scale*10.0); color=source(q); }
		}
		else if (family == 4.0)
		{
			if (local == 0.0) color=abs(original-source(uv+vec2(0.04,0.02)))*amount;
			else if (local == 1.0) color=vec4(vec3(abs(luma(original.rgb)-luma(source(uv+vec2(0.03,0)).rgb))*8.0),1);
			else if (local == 2.0) color=vec4(vec3(luma(original.rgb)),1);
			else if (local == 3.0) { float y=clamp(luma(original.rgb),0,1); color=mix(original, vec4(vec3(step(abs(uv.y-y),0.015)),1),0.8); }
			else if (local == 4.0) { vec2 flow=vec2(luma(source(uv+px*6.0).rgb)-luma(original.rgb), luma(source(uv+px.yx*6.0).rgb)-luma(original.rgb)); color=vec4(flow*4.0+0.5,0.5,1); }
			else if (local == 5.0) { float fg=smoothstep(threshold,threshold+0.2,distance(original.rgb,source(uv+vec2(0.05,0.02)).rgb)); color=mix(vec4(0,0,0,1),original,fg); }
			else { float y=luma(original.rgb); color=mix(original, vec4(y, y*y, 1.0-y, 1),0.65); }
		}
		else if (family == 5.0)
		{
			if (local == 0.0) { float bar=step(abs(uv.y-0.5),0.2); color=mix(vec4(0),colorA,bar); }
			else if (local == 1.0) { color=mix(colorA,colorB,uv.x); color.rgb*=0.55+0.45*sin(uv.y*80.0); }
			else if (local == 2.0) { color=mix(colorB,colorA,hash(floor(uv*16.0))); }
			else { float ring=smoothstep(0.35,0.32,length(p)); color=mix(colorA,colorB,ring); }
		}
		else
		{
			if (local == 0.0) { float d=min(length(p), abs(p.x)+abs(p.y)); color=vec4(vec3(smoothstep(0.0,0.5,d*scale)),1); }
			else if (local == 1.0) { color=source(uv); float line=step(fract((uv.x+uv.y+phase)*24.0),0.08); color=mix(color, colorA, line); }
			else { vec2 q=vec2(uv.x, fract(uv.y+phase)); color=source(q); }
		}
		gl_FragColor=clamp(color,0.0,1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{ struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("my_ImageSequenceClip Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }
static VuoInteger dim(VuoPoint2d resolution, bool width) { VuoInteger v=(VuoInteger)llround(width?resolution.x:resolution.y); return v>0?v:160; }
static VuoImage fallback(VuoImage image, VuoInteger w, VuoInteger h) { return image?image:VuoImage_makeColorImage((VuoColor){0.08,0.09,0.12,1},w,h); }
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,

		VuoInputData(VuoReal, {"default":1.0}) amount,
		VuoInputData(VuoReal, {"default":1.0}) scale,
		VuoInputData(VuoReal, {"default":0.0}) phase,
		VuoInputData(VuoReal, {"default":0.2}) threshold,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) center,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) colorB,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"OutputImage"}) textureOutput
)
{ VuoInteger w=dim(resolution,true), h=dim(resolution,false); VuoShader_setUniform_VuoImage((*instance)->shader,"sourceImage",fallback(NULL,w,h)); VuoShader_setUniform_VuoInteger((*instance)->shader,"filterKind",50); VuoShader_setUniform_VuoPoint2d((*instance)->shader,"targetSize",(VuoPoint2d){w,h}); VuoShader_setUniform_VuoReal((*instance)->shader,"amount",amount); VuoShader_setUniform_VuoReal((*instance)->shader,"scale",scale); VuoShader_setUniform_VuoReal((*instance)->shader,"phase",phase); VuoShader_setUniform_VuoReal((*instance)->shader,"threshold",threshold); VuoShader_setUniform_VuoPoint2d((*instance)->shader,"center",center); VuoShader_setUniform_VuoColor((*instance)->shader,"colorA",colorA); VuoShader_setUniform_VuoColor((*instance)->shader,"colorB",colorB); *textureOutput=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
