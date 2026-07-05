// runtime/buffer_ops_executerawbufferupdate — the ExecuteRawBufferUpdate golden's bug seam + selftest decl.
// The op itself self-registers (BufferOp) in the .cpp; only the test seams need a header.
#pragma once

namespace sw {

// GOLDEN tooth: when true, ExecuteRawBufferUpdate forwards the WRONG port (UpdateCommands instead of the
// `Buffer` input) — corrupting the real port-selection so the golden's -bug bites. OFF in production.
bool& executeRawBufferForwardWrongPortBug();

// --selftest-executerawbufferupdate: forwards the `Buffer` input (NOT UpdateCommands) on BOTH cook legs.
int runExecuteRawBufferUpdateSelfTest(bool injectBug);

}  // namespace sw
