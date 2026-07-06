// io_video_capture_register — CLI registration seam for --selftest-io-video-capture.
//
// The self-test IMPLEMENTATION lives in platform/video_capture.mm (ObjC/AVFoundation). REGISTER_SELFTESTS
// is a C++ file-scope initializer, so this thin .cpp forward-declares runVideoCaptureSelfTest and
// registers it — matching the io-* family's self-registration posture (io_video_decode_register.cpp).
#include "runtime/selftest_registry.h"

namespace sw {
int runVideoCaptureSelfTest(bool injectBug);  // platform/video_capture.mm
}

// Register as --selftest-io-video-capture (order 716, after io-video-decode at 715).
REGISTER_SELFTESTS(/*orderBase=*/716, {"io-video-capture", sw::runVideoCaptureSelfTest});
