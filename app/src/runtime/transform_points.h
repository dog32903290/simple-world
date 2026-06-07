#pragma once
namespace sw {
// Headless proof for line B: generate radial points (with tangential velocity),
// then run TransformPoints once with a known deltaTime, and assert each point
// moved by exactly velocity*dt (Euler step). injectBug=true feeds deltaTime=0 so
// nothing moves -> the eye must FAIL. Returns process exit code (0=PASS).
int runTransformPointsSelfTest(bool injectBug);
}  // namespace sw
