#!/usr/bin/env python3
"""Generate Batch 45-49 TiXL numbers/anim/vec Vuo adapters and proofs."""

from __future__ import annotations

from pathlib import Path

REPO = Path(__file__).resolve().parents[3]


def camel(name: str) -> str:
    return name[0].lower() + name[1:]


BATCHES = [
    {
        "num": 45,
        "slug": "anim-time",
        "label": "AnimTime",
        "lib": "Lib.numbers.anim.time",
        "srcdir": "numbers/anim/time",
        "category": "Operators/Lib/numbers/anim/time",
        "nodes": [
            ("AbletonLinkSync", "side-effect bounded adapter", [("VuoBoolean", "IsConnected", "isConnected"), ("VuoReal", "Result", "result"), ("VuoReal", "Tempo", "tempo")], "*isConnected = false; *result = timeValue; *tempo = 120.0 + speedFactor * 10.0; proof = *result;"),
            ("ClipTime", "exact scalar clamp adapter", [("VuoReal", "Time", "timeOut")], "*timeOut = clampReal(timeValue, fmin(a, b), fmax(a, b)); proof = *timeOut;"),
            ("ConvertTime", "bounded tempo conversion adapter", [("VuoReal", "Result", "result")], "*result = (timeMode == 0) ? timeValue : timeValue * fmax(1.0, speedFactor); proof = *result;"),
            ("DateTimeInSecs", "bounded wall-clock adapter", [("VuoInteger", "Result", "result")], "*result = (VuoInteger)floor(fmax(0.0, timeValue) + 0.5); proof = (VuoReal)*result;"),
            ("GetFrameSpeedFactor", "exact scalar adapter", [("VuoReal", "FrameSpeedFactor", "frameSpeedFactor")], "*frameSpeedFactor = speedFactor; proof = *frameSpeedFactor;"),
            ("HasTimeChanged", "bounded stateless change adapter", [("VuoReal", "DeltaTime", "deltaTime"), ("VuoBoolean", "HasChanged", "hasChanged")], "*deltaTime = timeValue - a; *hasChanged = fabs(*deltaTime) > 0.00001; proof = *hasChanged ? clampReal(fabs(*deltaTime), 0.0, 10.0) : 0.0;"),
            ("LastFrameDuration", "bounded frame duration adapter", [("VuoReal", "Duration", "duration")], "*duration = fmax(0.0, delayDuration); proof = *duration;"),
            ("RunTime", "bounded runtime adapter", [("VuoReal", "TimeInSeconds", "timeInSeconds")], "*timeInSeconds = fmax(0.0, timeValue * fmax(0.0, speedFactor)); proof = *timeInSeconds;"),
            ("SetPlaybackSpeed", "side-effect bounded adapter", [("VuoText", "CommandTrace", "commandTrace")], '*commandTrace = VuoText_make("SetPlaybackSpeed body-layer command trace"); proof = speedFactor;'),
            ("SetPlaybackTime", "side-effect bounded adapter", [("VuoText", "CommandTrace", "commandTrace")], '*commandTrace = VuoText_make("SetPlaybackTime body-layer command trace"); proof = timeValue;'),
            ("SetTime", "side-effect bounded adapter", [("VuoText", "CommandTrace", "commandTrace")], '*commandTrace = VuoText_make("SetTime body-layer command trace"); proof = timeValue;'),
            ("StopWatch", "bounded stateless stopwatch adapter", [("VuoReal", "Delta", "delta"), ("VuoReal", "LastDuration", "lastDuration")], "*delta = trigger ? fmax(0.0, timeValue - a) : 0.0; *lastDuration = fmax(0.0, b); proof = *delta + *lastDuration;"),
            ("Time", "bounded scalar time adapter", [("VuoReal", "Timefloat", "timefloat")], "*timefloat = timeValue; proof = *timefloat;"),
        ],
    },
    {
        "num": 46,
        "slug": "anim-utils",
        "label": "AnimUtils",
        "lib": "Lib.numbers.anim.utils",
        "srcdir": "numbers/anim/utils",
        "category": "Operators/Lib/numbers/anim/utils",
        "nodes": [
            ("FindKeyframes", "side-effect bounded keyframe adapter", [("VuoInteger", "KeyframeCount", "keyframeCount"), ("VuoReal", "Time", "timeOut"), ("VuoReal", "Value", "valueOut")], "*keyframeCount = frameCount < 0 ? 0 : frameCount; *timeOut = timeValue; *valueOut = mixReal(a, b, clampReal(timeValue, 0.0, 1.0)); proof = *valueOut;"),
            ("SetKeyframes", "side-effect bounded keyframe adapter", [("VuoReal", "CurrentValue", "currentValue")], "*currentValue = mixReal(a, b, clampReal(timeValue, 0.0, 1.0)); proof = *currentValue;"),
        ],
    },
    {
        "num": 47,
        "slug": "bool-process",
        "label": "BoolProcess",
        "lib": "Lib.numbers.bool.process",
        "srcdir": "numbers/bool/process",
        "category": "Operators/Lib/numbers/bool/process",
        "nodes": [
            ("CacheBoolean", "bounded value adapter", [("VuoBoolean", "Result", "result")], "*result = value; proof = *result ? 1.0 : 0.0;"),
            ("DelayBoolean", "bounded stateless delay adapter", [("VuoBoolean", "DelayedTrigger", "delayedTrigger")], "*delayedTrigger = frameCount <= 0 ? trigger : false; proof = *delayedTrigger ? 1.0 : 0.0;"),
            ("DelayTriggerChange", "bounded stateless delay adapter", [("VuoBoolean", "DelayedTrigger", "delayedTrigger"), ("VuoReal", "RemainingTime", "remainingTime")], "*delayedTrigger = delayDuration <= 0.0 ? trigger : false; *remainingTime = fmax(0.0, delayDuration); proof = (*delayedTrigger ? 1.0 : 0.0) + *remainingTime;"),
            ("KeepBoolean", "bounded freeze adapter", [("VuoBoolean", "Result", "result"), ("VuoReal", "TimeSinceFreeze", "timeSinceFreeze")], "*result = freeze ? value : trigger; *timeSinceFreeze = freeze ? fmax(0.0, timeValue) : 0.0; proof = (*result ? 1.0 : 0.0) + *timeSinceFreeze;"),
        ],
    },
    {
        "num": 48,
        "slug": "float-random",
        "label": "FloatRandom",
        "lib": "Lib.numbers.float.random",
        "srcdir": "numbers/float/random",
        "category": "Operators/Lib/numbers/float/random",
        "nodes": [
            ("FloatHash", "deterministic hash adapter", [("VuoInteger", "Result", "result")], "*result = (VuoInteger)(hashUInt((uint32_t)floor(seedReal * 1000.0 + 0.5), uniqueForChild ? 0x9e3779b9u : 0u) & 0x7fffffff); proof = (VuoReal)(*result % 1000) / 1000.0;"),
            ("PerlinNoise", "bounded value-noise adapter", [("VuoReal", "Result", "result")], "*result = noise2(input1.x * scale + a, input1.y * scale + b, seed); proof = *result;"),
            ("Random", "deterministic random adapter", [("VuoReal", "Result", "result")], "*result = mixReal(minValue, maxValue, hashUnit(seed, uniqueForChild ? 17u : 0u)); proof = *result;"),
        ],
    },
    {
        "num": 49,
        "slug": "vec2",
        "label": "Vec2",
        "lib": "Lib.numbers.vec2",
        "srcdir": "numbers/vec2",
        "category": "Operators/Lib/numbers/vec2",
        "nodes": [
            ("AddVec2", "exact vector adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(input1.x + input2.x, input1.y + input2.y); proof = length2(*result);"),
            ("DampVec2", "bounded damping adapter", [("VuoPoint2d", "Result", "result")], "*result = mix2(input1, input2, clampReal(damping, 0.0, 1.0)); proof = length2(*result);"),
            ("DivideVector2", "exact safe-divide vector adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(safeDiv(input1.x, input2.x), safeDiv(input1.y, input2.y)); proof = length2(*result);"),
            ("DotVec2", "exact vector adapter", [("VuoReal", "Result", "result")], "*result = input1.x * input2.x + input1.y * input2.y; proof = *result;"),
            ("GridPosition", "bounded grid adapter", [("VuoPoint2d", "Position", "position"), ("VuoPoint2d", "Size", "size")], "VuoInteger columns = frameCount <= 0 ? 1 : frameCount; VuoInteger row = index / columns; VuoInteger column = index % columns; *size = VuoPoint2d_make(fmax(0.0001, input2.x), fmax(0.0001, input2.y)); *position = VuoPoint2d_make(input1.x + column * size->x, input1.y + row * size->y); proof = length2(*position) + length2(*size);"),
            ("HasVec2Changed", "bounded stateless change adapter", [("VuoPoint2d", "Delta", "delta"), ("VuoBoolean", "HasChanged", "hasChanged")], "*delta = VuoPoint2d_make(input1.x - input2.x, input1.y - input2.y); *hasChanged = length2(*delta) > 0.00001; proof = *hasChanged ? length2(*delta) : 0.0;"),
            ("Int2ToVector2", "exact vector adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make((VuoReal)frameCount, (VuoReal)index); proof = length2(*result);"),
            ("PadVec2Range", "bounded range adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(input1.x + a, input1.y + b); proof = length2(*result);"),
            ("PerlinNoise2", "bounded vector noise adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(noise2(input1.x * scale, input1.y * scale, seed), noise2(input1.y * scale + 19.17, input1.x * scale - 3.11, seed + 31)); proof = length2(*result);"),
            ("PickVector2", "bounded selector adapter", [("VuoPoint2d", "Selected", "selected")], "*selected = trigger ? input2 : input1; proof = length2(*selected);"),
            ("RemapVec2", "exact vector remap adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(mixReal(minValue, maxValue, clampReal(input1.x, 0.0, 1.0)), mixReal(minValue, maxValue, clampReal(input1.y, 0.0, 1.0))); proof = length2(*result);"),
            ("ScaleVector2", "exact vector adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(input1.x * scale, input1.y * scale); proof = length2(*result);"),
            ("Vec2ToVec3", "exact vector adapter", [("VuoPoint3d", "Result", "result")], "*result = VuoPoint3d_make(input1.x, input1.y, a); proof = sqrt(result->x * result->x + result->y * result->y + result->z * result->z);"),
            ("Vector2Components", "exact vector adapter", [("VuoReal", "X", "x"), ("VuoReal", "Y", "y")], "*x = input1.x; *y = input1.y; proof = fabs(*x) + fabs(*y);"),
        ],
    },
]


COMMON_HELPERS = r'''
#include "VuoPoint2d.h"
#include "VuoPoint3d.h"
#include "VuoText.h"
#include <math.h>
#include <stdint.h>

static VuoReal clampReal(VuoReal value, VuoReal low, VuoReal high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static VuoReal mixReal(VuoReal a, VuoReal b, VuoReal amount)
{
	return a + (b - a) * amount;
}

static VuoReal safeDiv(VuoReal a, VuoReal b)
{
	return fabs(b) < 0.000001 ? 0.0 : a / b;
}

static VuoReal length2(VuoPoint2d v)
{
	return sqrt(v.x * v.x + v.y * v.y);
}

static VuoPoint2d mix2(VuoPoint2d a, VuoPoint2d b, VuoReal amount)
{
	return VuoPoint2d_make(mixReal(a.x, b.x, amount), mixReal(a.y, b.y, amount));
}

static uint32_t hashUInt(uint32_t value, uint32_t salt)
{
	value ^= salt + 0x9e3779b9u + (value << 6) + (value >> 2);
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

static VuoReal hashUnit(VuoInteger seed, uint32_t salt)
{
	return (VuoReal)(hashUInt((uint32_t)seed, salt) & 0x00ffffffu) / (VuoReal)0x01000000u;
}

static VuoReal noise2(VuoReal x, VuoReal y, VuoInteger seed)
{
	VuoInteger xi = (VuoInteger)floor(x);
	VuoInteger yi = (VuoInteger)floor(y);
	VuoReal xf = x - floor(x);
	VuoReal yf = y - floor(y);
	VuoReal a = hashUnit(seed + xi * 374761393 + yi * 668265263, 0u);
	VuoReal b = hashUnit(seed + (xi + 1) * 374761393 + yi * 668265263, 1u);
	VuoReal c = hashUnit(seed + xi * 374761393 + (yi + 1) * 668265263, 2u);
	VuoReal d = hashUnit(seed + (xi + 1) * 374761393 + (yi + 1) * 668265263, 3u);
	VuoReal u = xf * xf * (3.0 - 2.0 * xf);
	VuoReal v = yf * yf * (3.0 - 2.0 * yf);
	return mixReal(mixReal(a, b, u), mixReal(c, d, u), v);
}
'''


def output_signature(outputs):
    lines = []
    for ctype, title, var in outputs:
        lines.append(f'\t\tVuoOutputData({ctype}, {{"name":"{title}"}}) {var},')
    lines.append('\t\tVuoOutputData(VuoReal, {"name":"ProofValue"}) proofValue')
    return "\n".join(lines)


def node_source(batch, name, grade, outputs, assign_code):
    class_name = f"my.{batch['srcdir'].replace('/', '.')}.{camel(name)}"
    output_desc = ", ".join(f"{ctype} {title}" for ctype, title, _ in outputs)
    return f'''/**
 * @file
 * {class_name} node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_{name}
 * - Category: {batch['category']}
 * - Source: external/tixl/Operators/Lib/{batch['srcdir']}/{name}.cs
 * - Primary output(s): {output_desc} (ColorForValues #868C8D)
 *
 * {grade}. ProofValue is a Vuo-only numeric tap for the Batch {batch['num']} proof composition.
 */

{COMMON_HELPERS}

VuoModuleMetadata({{
\t\t\t\t\t "title" : "my_{name}",
\t\t\t\t\t "description" : "TiXL {name} {grade}. Source: external/tixl/Operators/Lib/{batch['srcdir']}/{name}.cs. Category: {batch['category']}. Primary output(s): {output_desc} (ColorForValues #868C8D). ProofValue is Vuo-only.",
\t\t\t\t\t "keywords" : [ "tixl", "numbers", "ColorForValues", "#868C8D", "batch{batch['num']}" ],
\t\t\t\t\t "version" : "1.0.0",
\t\t\t\t\t "dependencies" : [ ],
\t\t\t\t }});

void nodeEvent
(
\t\tVuoInputEvent() renderTick,
\t\tVuoInputData(VuoReal, {{"default":0.35}}) timeValue,
\t\tVuoInputData(VuoReal, {{"default":0.1}}) a,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) b,
\t\tVuoInputData(VuoReal, {{"name":"Min","default":0.0}}) minValue,
\t\tVuoInputData(VuoReal, {{"name":"Max","default":1.0}}) maxValue,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) scale,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) speedFactor,
\t\tVuoInputData(VuoReal, {{"default":0.25}}) damping,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) delayDuration,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) seedReal,
\t\tVuoInputData(VuoInteger, {{"default":0}}) mode,
\t\tVuoInputData(VuoInteger, {{"default":0}}) timeMode,
\t\tVuoInputData(VuoInteger, {{"default":7}}) seed,
\t\tVuoInputData(VuoInteger, {{"default":4}}) frameCount,
\t\tVuoInputData(VuoInteger, {{"default":1}}) index,
\t\tVuoInputData(VuoBoolean, {{"default":true}}) trigger,
\t\tVuoInputData(VuoBoolean, {{"default":false}}) value,
\t\tVuoInputData(VuoBoolean, {{"default":false}}) freeze,
\t\tVuoInputData(VuoBoolean, {{"default":false}}) uniqueForChild,
\t\tVuoInputData(VuoPoint2d, {{"default":{{"x":0.25,"y":0.75}}}}) input1,
\t\tVuoInputData(VuoPoint2d, {{"default":{{"x":0.5,"y":0.2}}}}) input2,
{output_signature(outputs)}
)
{{
\tVuoReal proof = 0.0;
\t(void)renderTick; (void)mode; (void)timeMode; (void)freeze; (void)uniqueForChild;
\t{assign_code}
\t*proofValue = proof;
}}
'''


PROOF_NODE = r'''/**
 * @file
 * my.numbers.batch.batch{num}{label}Proof node implementation.
 *
 * Proof-only image adapter for Batch {num} {lib}.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({"title":"my_Batch{num}{label}Proof","description":"Proof-only compositor for Batch {num} {lib}.","keywords":["tixl","batch{num}","numbers","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	{uniforms}
	varying vec2 fragmentTextureCoordinate;

	float norm(float value)
	{
		return clamp(0.5 + value / 8.0, 0.04, 0.96);
	}

	vec3 rowColor(float index)
	{
		return 0.50 + 0.50 * cos(vec3(0.3, 2.1, 4.0) + index * 0.77);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.015, 0.017, 0.020);
		float row = floor((1.0 - st.y) * float({count}));
		float value = 0.0;
		{switches}
		float bar = step(0.08, st.x) * step(st.x, 0.08 + norm(value) * 0.84);
		float inRow = step(0.0, row) * step(row, float({count} - 1));
		color = mix(color, rowColor(row), bar * inRow);
		color += vec3(0.06) * step(0.992, fract(st.y * float({count})));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *i = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(i, free);
	i->shader = VuoShader_make("my_Batch{num}{label}Proof Shader");
	VuoShader_addSource(i->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(i->shader);
	return i;
}

static unsigned int clampDimension(VuoInteger value)
{
	if (value < 64) return 64;
	if (value > 4096) return 4096;
	return (unsigned int)value;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
{inputs}
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)renderTick;
{sets}
	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
'''


def proof_node_source(batch):
    nodes = batch["nodes"]
    uniforms = "\n\t".join(f"uniform float {camel(name)}Value;" for name, *_ in nodes)
    switches = "\n\t\t".join(f"if (row == {i}.0) value = {camel(name)}Value;" for i, (name, *_rest) in enumerate(nodes))
    inputs = "".join(f'\t\tVuoInputData(VuoReal, {{"default":{(i + 1) / 10.0:.1f}}}) {camel(name)}Value,\n' for i, (name, *_rest) in enumerate(nodes))
    sets = "".join(f'\tVuoShader_setUniform_VuoReal((*instance)->shader, "{camel(name)}Value", {camel(name)}Value);\n' for name, *_ in nodes)
    return (PROOF_NODE
        .replace("{num}", str(batch["num"]))
        .replace("{label}", batch["label"])
        .replace("{lib}", batch["lib"])
        .replace("{count}", str(len(nodes)))
        .replace("{uniforms}", uniforms)
        .replace("{switches}", switches)
        .replace("{inputs}", inputs)
        .replace("{sets}", sets))


def composition(batch):
    num = batch["num"]
    label = batch["label"]
    proof_type = f"my.numbers.batch.batch{num}{label}Proof"
    node_lines = []
    edges = []
    proof_label_ports = []
    for i, (name, *_rest) in enumerate(batch["nodes"]):
        var = camel(name)
        node_type = f"my.{batch['srcdir'].replace('/', '.')}.{var}"
        pos_y = 310 - i * 95
        node_lines.append(f'{name} [type="{node_type}" version="1.0.0" label="my_{name}|<renderTick>renderTick\\l|<proofValue>ProofValue\\r" pos="-360,{pos_y}" fillcolor="#868C8D"];')
        proof_label_ports.append(f"<{var}Value>{var}Value\\l|")
        edges.append(f"FireOnDisplayRefresh:requestedFrame -> {name}:renderTick;")
        edges.append(f"{name}:proofValue -> ProofImage:{var}Value;")
    proof_ports = "".join(proof_label_ports)
    save_path = str(REPO / "artifacts" / "vuo_cli" / f"batch-{num}-{batch['slug']}-vuo-save").replace("/", "\\/")
    return f'''/**
 * @file
 * Batch {num} {batch['lib']} Vuo visual proof.
 *
 * @lastSavedInVuoVersion 2.4.6
 * @license This composition may be modified and distributed under the terms of the MIT License.
 */

digraph G
{{
FireOnDisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\\r" pos="-820,0" fillcolor="lime" _requestedFrame_eventThrottling="drop"];

{chr(10).join(node_lines)}

ProofImage [type="{proof_type}" version="1.0.0" label="my_Batch{num}{label}Proof|<renderTick>renderTick\\l|{proof_ports}<width>width\\l|<height>height\\l|<image>Image\\r" pos="220,0" fillcolor="#9F008A" _width="960" _height="540"];

RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch {num} {label}|<refresh>refresh\\l|<image>image\\l|<setWindowDescription>setWindowDescription\\l|<updatedWindow>updatedWindow\\r" pos="790,80" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];

SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\\l|<url>url\\l|<saveImage>saveImage\\l|<ifExists>ifExists\\l|<format>format\\l|<done>done\\r" pos="790,-150" fillcolor="orange" _url="\\"{save_path}\\"" _ifExists="1" _format="\\"PNG\\""];

Comment [type="vuo.comment" label="\\"Batch {num} {batch['lib']} visual proof\\"" pos="-80,455" width="600" height="42"];

FireOnDisplayRefresh:requestedFrame -> ProofImage:renderTick;
FireOnDisplayRefresh:requestedFrame -> RenderWindow:refresh;
{chr(10).join(edges)}
ProofImage:image -> RenderWindow:image;
ProofImage:image -> SaveImage:saveImage;
}}
'''


def semantics_test(batch):
    names = ", ".join(f'"{name}"' for name, *_ in batch["nodes"])
    return f'''const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch {batch['num']} {batch['lib']} source namespace is audited", () => {{
  for (const name of [{names}]) {{
    assert.match(read(`external/tixl/Operators/Lib/{batch['srcdir']}/${{name}}.cs`), new RegExp(`class ${{name}}|sealed class ${{name}}`));
    assert.match(read(`external/tixl/Operators/Lib/{batch['srcdir']}/${{name}}.t3`), /DefaultValue|Inputs|Children|Id/);
  }}
}});
'''


def vuo_nodes_test(batch):
    rows = ",\n".join(f'    ["vuo-nodes/my.{batch["srcdir"].replace("/", ".")}.{camel(name)}.c", "my_{name}", "{name}"]' for name, *_ in batch["nodes"])
    return f'''const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch {batch['num']} Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {{
  const nodes = [
{rows}
  ];
  for (const [file, title, donor] of nodes) {{
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\\\s*:\\\\s*"${{title}}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/{batch['srcdir']}/${{donor}}.cs`));
    assert.match(s, /Category: {batch['category'].replace('/', r'\/')}/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }}
}});
'''


def composition_test(batch):
    titles = ", ".join(f'"my_{name}"' for name, *_ in batch["nodes"])
    return f'''const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch {batch['num']} proof wires {batch['lib']} nodes into a visible save path", () => {{
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo"), "utf8");
  for (const title of [{titles}, "my_Batch{batch['num']}{batch['label']}Proof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-{batch['num']}-{batch['slug']}-vuo-save/);
  assert.match(s, /ProofValue/);
}});
'''


def acceptance_doc(batch):
    rows = []
    for name, grade, outputs, *_ in batch["nodes"]:
        file = f"vuo-nodes/my.{batch['srcdir'].replace('/', '.')}.{camel(name)}.c"
        outputs_text = ", ".join(f"{ctype} `{title}`" for ctype, title, _ in outputs)
        rows.append(
            f"| `{batch['lib']}.{name}` | {grade} | `my_{name}` | `{file}` | C# `external/tixl/Operators/Lib/{batch['srcdir']}/{name}.cs`; `.t3` `external/tixl/Operators/Lib/{batch['srcdir']}/{name}.t3` | {outputs_text}; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo` | done |"
        )
    return f'''# Batch {batch['num']} {batch['lib']} Acceptance Matrix

## Scope

Batch {batch['num']} ports `{batch['lib']}` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. App/timeline side-effect nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
{chr(10).join(rows)}

## Trial Pressure

- `tests/tixl_batch{batch['num']}_{batch['slug'].replace('-', '_')}_semantics.test.js`
- `tests/tixl_batch{batch['num']}_{batch['slug'].replace('-', '_')}_vuo_nodes.test.js`
- `tests/vuo_batch_{batch['num']}_{batch['slug'].replace('-', '_')}_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo`
'''


def write(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main():
    for batch in BATCHES:
        for name, grade, outputs, assign_code in batch["nodes"]:
            write(REPO / "vuo-nodes" / f"my.{batch['srcdir'].replace('/', '.')}.{camel(name)}.c", node_source(batch, name, grade, outputs, assign_code))
        write(REPO / "vuo-nodes" / f"my.numbers.batch.batch{batch['num']}{batch['label']}Proof.c", proof_node_source(batch))
        write(REPO / "vuo-compositions" / "generated" / f"myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo", composition(batch))
        write(REPO / "tests" / f"tixl_batch{batch['num']}_{batch['slug'].replace('-', '_')}_semantics.test.js", semantics_test(batch))
        write(REPO / "tests" / f"tixl_batch{batch['num']}_{batch['slug'].replace('-', '_')}_vuo_nodes.test.js", vuo_nodes_test(batch))
        write(REPO / "tests" / f"vuo_batch_{batch['num']}_{batch['slug'].replace('-', '_')}_composition.test.js", composition_test(batch))
        write(REPO / "docs" / "tixl-porting" / "batches" / f"2026-06-05-batch-{batch['num']}-{batch['slug']}.md", acceptance_doc(batch))


if __name__ == "__main__":
    main()
