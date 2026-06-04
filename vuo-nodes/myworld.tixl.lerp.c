/**
 * @file
 * myworld.tixl.lerp node implementation.
 *
 * This node mirrors TiXL's Lib.numbers.float.process.Lerp operator.
 */

VuoModuleMetadata({
					 "title" : "TiXL Lerp",
					 "description" : "Blends between two values using TiXL-compatible clamp behavior.",
					 "keywords" : [ "tixl", "interpolate", "blend", "mix", "linear", "amount", "factor" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoReal myworldClamp(VuoReal value, VuoReal min, VuoReal max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) a,
		VuoInputData(VuoReal, {"default":1.0}) b,
		VuoInputData(VuoReal, {"default":0.0}) f,
		VuoInputData(VuoBoolean, {"default":false}) clamp,
		VuoOutputData(VuoReal) result
)
{
	VuoReal factor = f;

	if (clamp)
		factor = myworldClamp(factor, 0.0, 1.0);

	*result = a + (b - a) * factor;
}
