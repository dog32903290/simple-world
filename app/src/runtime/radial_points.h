#pragma once
namespace sw {
// Headless proof for the first particle line: run the RadialPoints compute
// kernel, read the position buffer back, assert every point lies on radius R.
// injectBug=true generates a wrong radius so the eye must FAIL (RED) before we
// trust a PASS (GREEN). Returns a process exit code (0 = PASS, 1 = FAIL).
int runRadialPointsSelfTest(bool injectBug);
}  // namespace sw
