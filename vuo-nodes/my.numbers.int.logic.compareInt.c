/**
 * @file
 * my.numbers.int.logic.compareInt node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/logic/CompareInt.
 */

VuoModuleMetadata({
					 "title" : "my_CompareInt",
					 "description" : "TiXL CompareInt adapter. Source: external/tixl/Operators/Lib/numbers/int/logic/CompareInt.cs. Category: Operators/Lib/numbers/int/logic. Primary output: bool (ColorForValues #868C8D). Modes: 0 smaller, 1 equal, 2 larger, 3 not equal; ResultValue selects ResultForTrue/False.",
					 "keywords" : [ "tixl", "numbers", "int", "logic", "compare", "mode", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger myClampMode(VuoInteger mode)
{
	if (mode < 0)
		return 0;
	if (mode > 3)
		return 3;
	return mode;
}

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) value,
		VuoInputData(VuoInteger, {"default":0}) testValue,
		VuoInputData(VuoInteger, {"default":1, "suggestedMin":0, "suggestedMax":3}) mode,
		VuoInputData(VuoInteger, {"default":1}) resultForTrue,
		VuoInputData(VuoInteger, {"default":0}) resultForFalse,
		VuoOutputData(VuoBoolean, {"name":"IsTrue"}) isTrue,
		VuoOutputData(VuoInteger, {"name":"ResultValue"}) resultValue
)
{
	switch (myClampMode(mode))
	{
		case 0:
			*isTrue = value < testValue;
			break;
		case 1:
			*isTrue = value == testValue;
			break;
		case 2:
			*isTrue = value > testValue;
			break;
		case 3:
		default:
			*isTrue = value != testValue;
			break;
	}

	*resultValue = *isTrue ? resultForTrue : resultForFalse;
}
