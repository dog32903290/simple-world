/**
 * @file
 * my.numbers.bool.logic.pickBool node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/bool/logic/PickBool.
 */

VuoModuleMetadata({
					 "title" : "my_PickBool",
					 "description" : "TiXL source: external/tixl/Operators/Lib/numbers/bool/logic/PickBool.cs. Category: Operators/Lib/numbers/bool/logic. Primary output: Selected bool. Primary output type color: ColorForValues #868C8D. Vuo body-layer adapter limitation: TiXL MultiInputSlot<bool> BoolValues is exposed here as two VuoBoolean inputs.",
					 "keywords" : [ "tixl", "numbers", "bool", "pick", "select", "multiinput", "adapter", "value", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger myPositiveMod(VuoInteger value, VuoInteger count)
{
	VuoInteger result = value % count;
	return result < 0 ? result + count : result;
}

void nodeEvent
(
		VuoInputData(VuoBoolean, {"default":false}) boolValue0,
		VuoInputData(VuoBoolean, {"default":false}) boolValue1,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoOutputData(VuoBoolean, {"name":"Selected"}) selected
)
{
	VuoInteger normalizedIndex = myPositiveMod(index, 2);
	*selected = normalizedIndex == 0 ? boolValue0 : boolValue1;
}
