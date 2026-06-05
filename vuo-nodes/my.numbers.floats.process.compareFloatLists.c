/**
 * @file
 * my.numbers.floats.process.compareFloatLists node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_CompareFloatLists
 * - Category: Operators/Lib/numbers/floats/process
 * - Source: external/tixl/Operators/Lib/numbers/floats/process/CompareFloatLists.cs
 * - Default: ListA=[], ListB=[], Threshold=0.0 from CompareFloatLists.t3
 * - Primary output: float Difference (ColorForValues #868C8D)
 *
 * TiXL-source bug / bounded adapter:
 * The C# length-mismatch guard uses `list.Count < index`, so a non-empty
 * shorter list can be indexed out of range at index == Count. This Vuo body
 * keeps same-length and empty-list behavior exact, and treats missing elements
 * as different for a creator-usable bounded adapter.
 */

#include "VuoList_VuoReal.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_CompareFloatLists",
					 "description" : "Compares two float lists by thresholded absolute difference, with bounded mismatch handling.",
					 "keywords" : [ "tixl", "numbers", "floats", "list", "process", "compare", "difference", "threshold", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) listA,
		VuoInputData(VuoList_VuoReal) listB,
		VuoInputData(VuoReal, {"default":0.0}) threshold,
		VuoOutputData(VuoReal, {"name":"Difference"}) difference
)
{
	unsigned long countA = listA ? VuoListGetCount_VuoReal(listA) : 0;
	unsigned long countB = listB ? VuoListGetCount_VuoReal(listB) : 0;
	if (countA == 0 || countB == 0)
	{
		*difference = 1.0;
		return;
	}

	unsigned long maxCount = countA > countB ? countA : countB;
	unsigned long differentElementCount = 0;

	for (unsigned long index = 0; index < maxCount; ++index)
	{
		if (index >= countA || index >= countB)
		{
			differentElementCount++;
			continue;
		}

		VuoReal a = VuoListGetValue_VuoReal(listA, index + 1);
		VuoReal b = VuoListGetValue_VuoReal(listB, index + 1);
		if (fabs(a - b) > threshold)
			differentElementCount++;
	}

	*difference = (VuoReal)differentElementCount / (VuoReal)maxCount;
}
