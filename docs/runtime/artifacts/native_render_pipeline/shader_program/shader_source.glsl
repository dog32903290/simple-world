// My World E1 ShaderGraphNode shell
// Source: TiXL SphereSDF -> RaymarchField
// This GLSL-like artifact is for contract inspection, not final renderer output.

struct MyWorldField {
    vec3 xyz;
    float w;
};

float sdSphere(vec3 p, vec3 center, float radius) {
    return length(p - center) - radius;
}

MyWorldField myworld_field(vec3 p) {
    MyWorldField f;
    f.w = sdSphere(p, vec3(0.0, 0.0, 0.0), 0.5);
    f.xyz = p;
    return f;
}

float raymarch_field_1(vec3 rayOrigin, vec3 rayDirection) {
    float totalDistance = 0.0;
    for (int i = 0; i < 100; ++i) {
        vec3 p = rayOrigin + rayDirection * totalDistance;
        float distanceToField = myworld_field(p).w;
        if (distanceToField < 0.002) {
            return totalDistance;
        }
        totalDistance += distanceToField;
        if (totalDistance > 300.0) {
            break;
        }
    }
    return -1.0;
}
