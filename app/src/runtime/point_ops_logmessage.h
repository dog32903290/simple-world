// runtime/point_ops_logmessage — the LogMessage Command-rail op's process-scoped Message text + log sink.
//
// LogMessage's Message ([Input]<string>, LogMessage.cs:67) travels on the String channel. CmdCookCtx carries
// only Float params, so the op resolves WHICH text to emit from this process-scoped "current message", set by
// the cook driver from the node's strParams/strInputs (flat: Node::strParams, resident: ResidentNode::strInputs)
// right before the op cook — the SAME shape the driver already uses to thread VariableName into SetVarCmd. This
// header lets the two cook drivers (point_graph_command_cook / point_graph_resident_command_cook) set the text
// without pulling the at-cap point_ops.h. Bodies live in point_ops_logmessage.cpp.
#pragma once
#include <string>

namespace sw {

// The process-scoped Message text the LogMessage op emits (LogMessage.cs:26-36 Message.GetValue + the
// fallback "Log" when empty). The cook driver sets this from the node's resolved String "Message" param
// before cooking the LogMessage op; defaults to "Log" off any LogMessage path (faithful: empty → "Log").
std::string& logMessageCurrentText();

// logMessageSkipMessageThread — the --selftest-logmessage Message-thread TEETH hook. false = production
// (the cook driver threads the node's strParams/strInputs "Message" into logMessageCurrentText before the
// op cook). true = the pre-fan-out degeneracy: the driver SKIPS the thread → the op emits the stale/default
// text instead of the node's Message → the golden's "sink.lastMessage == node Message" assertion goes RED.
// OFF in production; the golden flips it for the bug leg then resets.
bool& logMessageSkipMessageThread();

}  // namespace sw
