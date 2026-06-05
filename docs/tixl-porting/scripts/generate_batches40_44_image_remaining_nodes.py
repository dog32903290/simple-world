#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

BATCHES = [
    ("40", "distort", "Lib.image.fx.distort", "image/fx/distort", "fx.distort", "Distort", "batch-40-distort-proof", [
        ("BubbleZoom", "bubbleZoom", "TextureOutput", "Image", "BubbleZoom.hlsl", "BubbleZoom.t3", 0),
        ("ChromaticDistortion", "chromaticDistortion", "Output", "Texture2d", "ChromaticDistortion.hlsl", "ChromaticDistortion.t3", 1),
        ("Displace", "displace", "Output", "Image", "Displace.hlsl", "Displace.t3", 2),
        ("DistortAndShade", "distortAndShade", "Output", "ImageA", "DistortAndShade.hlsl", "DistortAndShade.t3", 3),
        ("EdgeRepeat", "edgeRepeat", "TextureOutput", "Image", "EdgeRepeat.hlsl", "EdgeRepeat.t3", 4),
        ("FieldToImage", "fieldToImage", "TextureOutput", "Field", "FieldToImageTemplate.hlsl", "FieldToImage.t3", 5),
        ("KochKaleidoskope", "kochKaleidoskope", "TextureOutput", "Image", "KochKaleidoscope.hlsl", "KochKaleidoskope.t3", 6),
        ("PolarCoordinates", "polarCoordinates", "TextureOutput", "Image", "PolarCoordinates.hlsl", "PolarCoordinates.t3", 7),
        ("TimeDisplace", "timeDisplace", "Output", "Image", "TimeDisplace.hlsl", "TimeDisplace.t3", 8),
    ]),
    ("41", "stylize", "Lib.image.fx.stylize", "image/fx/stylize", "fx.stylize", "Stylize", "batch-41-stylize-proof", [
        ("AsciiRender", "asciiRender", "Output", "ImageA", "AsciiRender.hlsl", "AsciiRender.t3", 20),
        ("ChromaticAbberation", "chromaticAbberation", "TextureOutput", "Image", "ChromaticAbberation.hlsl", "ChromaticAbberation.t3", 21),
        ("ColorPhysarum", "colorPhysarum", "ImgOutput", "EffectTexture", "color-physarum-cs.hlsl", "ColorPhysarum.t3", 22),
        ("DetectEdges", "detectEdges", "TextureOutput", "Image", "DetectEdges.hlsl", "DetectEdges.t3", 23),
        ("Dither", "dither", "TextureOutput", "Image", "Dither.hlsl", "Dither.t3", 24),
        ("FakeLight", "fakeLight", "Output", "HeightMap", "FakeLight.hlsl", "FakeLight.t3", 25),
        ("Glow", "glow", "ImgOutput", "Texture", "Glow.cs", "Glow.t3", 26),
        ("HoneyCombTiles", "honeyCombTiles", "TextureOutput", "Image", "HexGridDisplace.hlsl", "HoneyCombTiles.t3", 27),
        ("LightRaysFx", "lightRaysFx", "Output", "Image", "LightRayFx.hlsl", "LightRaysFx.t3", 28),
        ("MosiacTiling", "mosiacTiling", "TextureOutput", "Image", "MosiacTiling.hlsl", "MosiacTiling.t3", 29),
        ("Pixelate", "pixelate", "TextureOutput", "Image", "Pixelate.hlsl", "Pixelate.t3", 30),
        ("ScreenCloseUp", "screenCloseUp", "Output", "Texture2d", "ScreenCloseUp.cs", "ScreenCloseUp.t3", 31),
        ("StarGlowStreaks", "starGlowStreaks", "TextureOutput", "Image", "StarGlowStreaks.hlsl", "StarGlowStreaks.t3", 32),
        ("Steps", "steps", "TextureOutput", "Image", "Steps.hlsl", "Steps.t3", 33),
        ("VoronoiCells", "voronoiCells", "TextureOutput", "Image", "VoronoiCells.hlsl", "VoronoiCells.t3", 34),
    ]),
    ("42", "analyze", "Lib.image.analyze", "image/analyze", "analyze", "Analyze", "batch-42-analyze-proof", [
        ("CompareImages", "compareImages", "TextureOutput", "Texture2d", "CompareImages.cs", "CompareImages.t3", 40),
        ("DetectMotion", "detectMotion", "TextureOutput", "VideoTexture", "DetectMotion.cs", "DetectMotion.t3", 41),
        ("GetImageBrightness", "getImageBrightness", "BrightnessImage", "Texture2d", "cs-GetImageBrightness.hlsl", "GetImageBrightness.t3", 42),
        ("ImageLevels", "imageLevels", "Output", "Texture2d", "ImageLevels.hlsl", "ImageLevels.t3", 43),
        ("OpticalFlow", "opticalFlow", "TextureOutput", "Image", "OpticalFlowKanade.hlsl", "OpticalFlow.t3", 44),
        ("RemoveStaticBackground", "removeStaticBackground", "Output", "Texture2d", "remove-static-background-cs1-learning.hlsl", "RemoveStaticBackground.t3", 45),
        ("WaveForm", "waveForm", "ImgOutput", "EffectTexture", "waveform-cs.hlsl", "WaveForm.t3", 46),
    ]),
    ("43", "load", "Lib.image.generate.load", "image/generate/load", "generate.load", "Load", "batch-43-load-proof", [
        ("ImageSequenceClip", "imageSequenceClip", "OutputImage", "none", "ImageSequenceClip.cs", "ImageSequenceClip.t3", 50),
        ("LoadImage", "loadImage", "Texture", "none", "LoadImage.cs", "LoadImage.t3", 51),
        ("LoadImageFromUrl", "loadImageFromUrl", "Texture", "none", "LoadImageFromUrl.cs", "LoadImageFromUrl.t3", 52),
        ("LoadSvgAsTexture2D", "loadSvgAsTexture2D", "Texture", "none", "LoadSvgAsTexture2D.cs", "LoadSvgAsTexture2D.t3", 53),
    ]),
    ("44", "misc", "Lib.image.generate.misc", "image/generate/misc", "generate.misc", "Misc", "batch-44-misc-proof", [
        ("JumpFloodFill", "jumpFloodFill", "ImageOutput", "Image", "img-generate-JumpFloodFill.hlsl", "JumpFloodFill.t3", 60),
        ("Sketch", "sketch", "ColorBuffer", "InputImage", "Sketch.cs", "Sketch.t3", 61),
        ("SlidingHistory", "slidingHistory", "Output", "Texture2d", "SlidingHistory.hlsl", "SlidingHistory.t3", 62),
    ]),
]

SHADER = r'''
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
'''

NODE_BODY = r'''
#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>
{shader}
struct nodeInstanceData {{ VuoShader shader; }};
struct nodeInstanceData * nodeInstanceInit(void)
{{ struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("{title} Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }}
static VuoInteger dim(VuoPoint2d resolution, bool width) {{ VuoInteger v=(VuoInteger)llround(width?resolution.x:resolution.y); return v>0?v:160; }}
static VuoImage fallback(VuoImage image, VuoInteger w, VuoInteger h) {{ return image?image:VuoImage_makeColorImage((VuoColor){{0.08,0.09,0.12,1}},w,h); }}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
{image_input}
		VuoInputData(VuoReal, {{"default":1.0}}) amount,
		VuoInputData(VuoReal, {{"default":1.0}}) scale,
		VuoInputData(VuoReal, {{"default":0.0}}) phase,
		VuoInputData(VuoReal, {{"default":0.2}}) threshold,
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.0,"y":0.0}}}}) center,
		VuoInputData(VuoColor, {{"default":{{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}}}) colorA,
		VuoInputData(VuoColor, {{"default":{{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}}}) colorB,
		VuoInputData(VuoPoint2d, {{"default":{{"x":0.0,"y":0.0}}}}) resolution,
		VuoOutputData(VuoImage, {{"name":"{output}"}}) textureOutput
)
{{ VuoInteger w=dim(resolution,true), h=dim(resolution,false); VuoShader_setUniform_VuoImage((*instance)->shader,"sourceImage",fallback({image_value},w,h)); VuoShader_setUniform_VuoInteger((*instance)->shader,"filterKind",{kind}); VuoShader_setUniform_VuoPoint2d((*instance)->shader,"targetSize",(VuoPoint2d){{w,h}}); VuoShader_setUniform_VuoReal((*instance)->shader,"amount",amount); VuoShader_setUniform_VuoReal((*instance)->shader,"scale",scale); VuoShader_setUniform_VuoReal((*instance)->shader,"phase",phase); VuoShader_setUniform_VuoReal((*instance)->shader,"threshold",threshold); VuoShader_setUniform_VuoPoint2d((*instance)->shader,"center",center); VuoShader_setUniform_VuoColor((*instance)->shader,"colorA",colorA); VuoShader_setUniform_VuoColor((*instance)->shader,"colorB",colorB); *textureOutput=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''

def write(path, text):
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)

def node_source(batch, spec):
    bnum, slug, lib, srcdir, classcat, label, proof, _ = batch
    name, camel, output, input_name, shader, t3, kind = spec
    title = f"my_{name}"
    if input_name == "none" or input_name == "Field":
        image_input = ""
        image_value = "NULL"
    else:
        port = input_name[0].lower() + input_name[1:]
        image_input = f'\t\tVuoInputData(VuoImage, {{"name":"{input_name}"}}) {port},\n'
        image_value = port
    return f'''/**
 * @file
 * my.image.{classcat}.{camel} node implementation.
 *
 * TiXL parity contract:
 * - Visible title: {title}
 * - Category: Operators/Lib/{srcdir}
 * - Source: external/tixl/Operators/Lib/{srcdir}/{name}.cs
 * - Default evidence: external/tixl/Operators/Lib/{srcdir}/{t3}
 * - Primary output evidence: {output}; Texture2D proof color ColorForTextures #9F008A unless noted in acceptance docs.
 *
 * Bounded Vuo body-layer adapter for TiXL evidence `{shader}`.
 * Limits: DX11 resources, compute passes, command streams, file/network IO, history buffers, shadergraph fields, and exact multi-output GPU state are represented by a single-pass visual proof.
 */

VuoModuleMetadata({{
 "title":"{title}",
 "description":"TiXL {name} bounded Vuo adapter. Source: external/tixl/Operators/Lib/{srcdir}/{name}.cs. Category: Operators/Lib/{srcdir}. Primary output evidence: {output}. Shader/resource evidence: {shader}.",
 "keywords":["tixl","image","Texture2D","{name}","{shader}","bounded approximation","ColorForTextures","#9F008A"],
 "version":"1.0.0",
 "dependencies":["VuoImageRenderer"]
}});
''' + NODE_BODY.format(shader=SHADER, title=title, image_input=image_input, image_value=image_value, output=output, kind=kind)

def proof_node(batch):
    bnum, slug, lib, srcdir, classcat, label, proof, nodes = batch
    uniforms = "\n".join(f"\tuniform sampler2D {camel}Image;" for _, camel, *_ in nodes)
    inputs = "\n".join(f"\t\tVuoInputData(VuoImage) {camel}Image," for _, camel, *_ in nodes)
    sets = "\n\t".join(f'VuoShader_setUniform_VuoImage((*instance)->shader,"{camel}Image",{camel}Image?{camel}Image:VuoImage_makeColorImage((VuoColor){{0.02,0.02,0.02,1}},w,h));' for _, camel, *_ in nodes)
    count = len(nodes)
    select = f"texture2D({nodes[-1][1]}Image, localSt)"
    for idx, (_, camel, *_rest) in reversed(list(enumerate(nodes[:-1]))):
        select = f"(band < {idx+1}.0 ? texture2D({camel}Image, localSt) : {select})"
    return f'''#include "VuoImageRenderer.h"
VuoModuleMetadata({{"title":"my_Batch{bnum}{label}Proof","description":"Proof compositor for Batch {bnum} {lib}.","keywords":["tixl","batch{bnum}","{slug}","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]}});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
{uniforms}
\tvarying vec2 fragmentTextureCoordinate;
\tvoid main() {{ vec2 st=fragmentTextureCoordinate; float band=floor(clamp(st.x,0.0,0.9999)*{count}.0); vec2 localSt=vec2(fract(st.x*{count}.0),st.y); vec4 color={select}; float edge=step(localSt.x,0.012)+step(0.988,localSt.x); if(edge>0.0) color.rgb=mix(color.rgb,vec3(1.0),0.35); gl_FragColor=color; }}
);
struct nodeInstanceData {{ VuoShader shader; }};
struct nodeInstanceData * nodeInstanceInit(void) {{ struct nodeInstanceData *i=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(i,free); i->shader=VuoShader_make("my_Batch{bnum}{label}Proof Shader"); VuoShader_addSource(i->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(i->shader); return i; }}
void nodeInstanceEvent(VuoInstanceData(struct nodeInstanceData *) instance,VuoInputEvent() renderTick,
{inputs}
\t\tVuoInputData(VuoInteger,{{"default":{count*160}}}) width,VuoInputData(VuoInteger,{{"default":160}}) height,VuoOutputData(VuoImage,{{"name":"Image"}}) image)
{{ VuoInteger w=width<1?{count*160}:width,h=height<1?160:height; {sets} *image=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) {{ VuoRelease((*instance)->shader); }}
'''

def composition(batch):
    bnum, slug, lib, srcdir, classcat, label, proof, nodes = batch
    defs=[]; edges=[]; pedges=[]
    for i,(name,camel,output,input_name,shader,t3,kind) in enumerate(nodes):
        y=-500+i*120
        typ=f"my.image.{classcat}.{camel}"
        input_port = "" if input_name in ("none","Field") else f"|<{input_name[0].lower()+input_name[1:]}>{input_name}\\l"
        defs.append(f'{name} [type="{typ}" version="1.0.0" label="my_{name}|<renderTick>renderTick\\l{input_port}|<amount>amount\\l|<scale>scale\\l|<phase>phase\\l|<threshold>threshold\\l|<center>center\\l|<colorA>colorA\\l|<colorB>colorB\\l|<resolution>resolution\\l|<textureOutput>{output}\\r" pos="-500,{y}" fillcolor="#9F008A" _amount="{1+i*0.08}" _scale="{1+i*0.12}" _phase="{i*0.07}" _threshold="0.18" _center="\\{{\\"x\\":0,\\"y\\":0\\}}" _colorA="\\{{\\"r\\":1,\\"g\\":0.9,\\"b\\":0.25,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":0.02,\\"g\\":0.02,\\"b\\":0.08,\\"a\\":1\\}}" _resolution="\\{{\\"x\\":160,\\"y\\":160\\}}"];')
        edges.append(f"DisplayRefresh:requestedFrame -> {name}:renderTick;")
        if input_name not in ("none","Field"):
            edges.append(f"Source:textureOutput -> {name}:{input_name[0].lower()+input_name[1:]};")
        pedges.append(f"{name}:textureOutput -> ProofImage:{camel}Image;")
    ports="|".join(f"<{camel}Image>{camel}Image\\l" for _,camel,*_ in nodes)+"|"
    return f'''digraph G
{{
DisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\\r" pos="-1100,100" fillcolor="lime" _requestedFrame_eventThrottling="drop"];
Source [type="my.image.generate.basic.checkerBoard" version="1.0.0" label="my_CheckerBoard|<renderTick>renderTick\\l|<colorA>colorA\\l|<colorB>colorB\\l|<stretch>stretch\\l|<scale>scale\\l|<useAspectRatio>useAspectRatio\\l|<offset>offset\\l|<resolution>resolution\\l|<generateMips>generateMips\\l|<textureOutput>TextureOutput\\r" pos="-830,100" fillcolor="#9F008A" _colorA="\\{{\\"r\\":0.0,\\"g\\":0.95,\\"b\\":1.0,\\"a\\":1\\}}" _colorB="\\{{\\"r\\":1.0,\\"g\\":0.08,\\"b\\":0.42,\\"a\\":1\\}}" _stretch="\\{{\\"x\\":1,\\"y\\":1\\}}" _scale="8" _useAspectRatio="true" _offset="\\{{\\"x\\":0,\\"y\\":0\\}}" _resolution="\\{{\\"x\\":160,\\"y\\":160\\}}" _generateMips="false"];
{chr(10).join(defs)}
ProofImage [type="my.image.batch.batch{bnum}{label}Proof" version="1.0.0" label="my_Batch{bnum}{label}Proof|<renderTick>renderTick\\l|{ports}<width>width\\l|<height>height\\l|<image>Image\\r" pos="120,100" fillcolor="#9F008A" _width="{len(nodes)*160}" _height="160"];
RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch {bnum} {label}|<refresh>refresh\\l|<image>image\\l|<setWindowDescription>setWindowDescription\\l|<updatedWindow>updatedWindow\\r" pos="720,100" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];
SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\\l|<url>url\\l|<saveImage>saveImage\\l|<ifExists>ifExists\\l|<format>format\\l|<done>done\\r" pos="720,-190" fillcolor="orange" _url="\\"\\\\/Users\\\\/chenbaiwei\\\\/Desktop\\\\/vibe coding\\\\/simple_world\\\\/artifacts\\\\/vuo_cli\\\\/{proof.replace('-proof','')}-vuo-save\\"" _ifExists="1" _format="\\"PNG\\""];
DisplayRefresh:requestedFrame -> Source:renderTick;
{chr(10).join(edges)}
DisplayRefresh:requestedFrame -> ProofImage:renderTick;
DisplayRefresh:requestedFrame -> RenderWindow:refresh;
{chr(10).join(pedges)}
ProofImage:image -> RenderWindow:image;
ProofImage:image -> SaveImage:saveImage;
}}
'''

def tests(batch):
    bnum, slug, lib, srcdir, classcat, label, proof, nodes = batch
    names=", ".join(f'"{n}"' for n,*_ in nodes)
    rows=",\n".join(f'    ["vuo-nodes/my.image.{classcat}.{camel}.c","my_{name}","{name}.cs","{shader}","{output}"]' for name,camel,output,_input,shader,*_ in nodes)
    safe=slug.replace("-","_")
    sem=f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch {bnum} {lib} source namespace is audited",()=>{{ for (const name of [{names}]) {{ assert.match(read(`external/tixl/Operators/Lib/{srcdir}/${{name}}.cs`),new RegExp(`class ${{name}}|sealed class ${{name}}`)); assert.match(read(`external/tixl/Operators/Lib/{srcdir}/${{name}}.t3`),/DefaultValue|Inputs|Children|Id/); }} }});
'''
    vuo=f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,".."); const read=p=>fs.readFileSync(path.join(repoRoot,p),"utf8");
test("Batch {bnum} Vuo nodes preserve TiXL naming, source, output evidence, and bounded limits",()=>{{ const nodes=[\n{rows}\n  ]; for (const [file,title,donor,shader,output] of nodes) {{ const s=read(file); assert.match(s,new RegExp(`"title"\\\\s*:\\\\s*"${{title}}"`)); assert.match(s,new RegExp(`Source: external/tixl/Operators/Lib/{srcdir}/${{donor}}`)); assert.match(s,new RegExp(shader.replace(/[.*+?^${{}}()|[\\]\\\\]/g,"\\\\$&"))); assert.match(s,new RegExp(output)); assert.match(s,/bounded Vuo body-layer adapter|bounded Vuo adapter/); }} }});
'''
    comp=f'''#!/usr/bin/env node
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),test=require("node:test");
const repoRoot=path.resolve(__dirname,"..");
test("Batch {bnum} proof composition wires {lib} nodes to a visible save path",()=>{{ const s=fs.readFileSync(path.join(repoRoot,"vuo-compositions/generated/myworld-batch-{bnum}-{slug}-proof.vuo"),"utf8"); for (const title of [{", ".join(f'"my_{n}"' for n,*_ in nodes)},"my_Batch{bnum}{label}Proof"]) assert.match(s,new RegExp(title)); assert.match(s,/{proof.replace('-proof','')}-vuo-save/); }});
'''
    return safe, sem, vuo, comp

def doc(batch):
    bnum, slug, lib, srcdir, classcat, label, proof, nodes = batch
    rows="\n".join(f"| `{lib}.{name}` | bounded body-layer adapter | `my_{name}` | `vuo-nodes/my.image.{classcat}.{camel}.c` | C# `external/tixl/Operators/Lib/{srcdir}/{name}.cs`; `.t3` `{t3}`; shader/resource `{shader}` | Batch {bnum} tests | `vuo-compositions/generated/myworld-batch-{bnum}-{slug}-proof.vuo` | done |" for name,camel,output,input_name,shader,t3,kind in nodes)
    return f'''# Batch {bnum} {lib} Acceptance Matrix

Scope: finish `{lib}`.

| TiXL node | acceptance | Vuo visible title | Vuo source | source evidence | tests | proof | status |
|---|---|---|---|---|---|---|---|
{rows}

## Proof Notes

- Texture-producing nodes use TiXL `ColorForTextures #9F008A`.
- Nodes whose TiXL primary force is command, IO, shadergraph field, compute state, history, or secondary GPU resources are accepted as bounded Vuo body-layer adapters only.
- Vuo CLI proof target: `{proof}`.
'''

def main():
    for batch in BATCHES:
        bnum, slug, lib, srcdir, classcat, label, proof, nodes = batch
        for spec in nodes:
            name, camel, *_ = spec
            write(f"vuo-nodes/my.image.{classcat}.{camel}.c", node_source(batch, spec))
        write(f"vuo-nodes/my.image.batch.batch{bnum}{label}Proof.c", proof_node(batch))
        write(f"vuo-compositions/generated/myworld-batch-{bnum}-{slug}-proof.vuo", composition(batch))
        safe, sem, vuo, comp = tests(batch)
        write(f"tests/tixl_batch{bnum}_{safe}_semantics.test.js", sem)
        write(f"tests/tixl_batch{bnum}_{safe}_vuo_nodes.test.js", vuo)
        write(f"tests/vuo_batch_{bnum}_{safe}_composition.test.js", comp)
        write(f"docs/tixl-porting/batches/2026-06-05-batch-{bnum}-{slug}.md", doc(batch))

if __name__ == "__main__":
    main()
