// io_video_decode_register — CLI registration seam for --selftest-io-video-decode.
//
// The self-test IMPLEMENTATION lives in platform/video_decode_selftest.mm (ObjC/AVFoundation, must be a
// .mm). REGISTER_SELFTESTS is a C++ file-scope initializer that pairs cleanly with a .cpp (the io-video-
// timing / io-visca-protocol goldens self-register the same way from their .cpp). So this thin .cpp just
// forward-declares runVideoDecodeSelfTest (defined in the .mm) and registers it — keeping the ObjC out of
// the registration macro while matching the io-* family's self-registration posture.
#include "runtime/selftest_registry.h"

namespace sw {
int runVideoDecodeSelfTest(bool injectBug);  // platform/video_decode_selftest.mm
}

// Register as --selftest-io-video-decode (order 715, after io-onvif-digest at 714).
REGISTER_SELFTESTS(/*orderBase=*/715, {"io-video-decode", sw::runVideoDecodeSelfTest});
