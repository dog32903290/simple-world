/**
 * @file
 * my.numbers.floats.process.analyzeFloatList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_AnalyzeFloatList
 * - Category: Operators/Lib/numbers/floats/process
 * - Source: external/tixl/Operators/Lib/numbers/floats/process/AnalyzeFloatList.cs
 * - Default: Input=[5.0,17.0] from AnalyzeFloatList.t3
 * - Primary output: float Min (ColorForValues #868C8D)
 *
 * TiXL's DirtyFlag early-return is an evaluation cache guard. This Vuo body
 * recomputes on incoming events and preserves the data contract.
 */

#include "VuoList_VuoReal.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_AnalyzeFloatList",
					 "description" : "Analyzes a float list, matching TiXL finite-value min/max and full-count mean behavior.",
					 "keywords" : [ "tixl", "numbers", "floats", "list", "process", "analyze", "mean", "valid", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) input,
		VuoOutputData(VuoReal, {"name":"Min"}) min,
		VuoOutputData(VuoReal, {"name":"Max"}) max,
		VuoOutputData(VuoReal, {"name":"AverageMean"}) averageMean,
		VuoOutputData(VuoBoolean, {"name":"AllValid"}) allValid
)
{
	unsigned long count = input ? VuoListGetCount_VuoReal(input) : 0;
	if (count == 0)
	{
		*min = NAN;
		*max = NAN;
		*averageMean = NAN;
		*allValid = false;
		return;
	}

	VuoReal sum = 0.0;
	VuoReal minValue = INFINITY;
	VuoReal maxValue = -INFINITY;
	VuoBoolean valid = true;

	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoReal value = VuoListGetValue_VuoReal(input, i);
		if (!isfinite(value))
		{
			valid = false;
			continue;
		}
		if (value < minValue)
			minValue = value;
		if (value > maxValue)
			maxValue = value;
		sum += value;
	}

	*min = minValue;
	*max = maxValue;
	*averageMean = sum / count;
	*allValid = valid;
}
