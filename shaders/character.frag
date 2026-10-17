#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

const vec3 kLightDir = normalize(vec3(0.4, 1.0, 0.3));

void main() {
    vec3 n = normalize(fragNormal);
    float diffuse = max(dot(n, kLightDir), 0.0);
    float ambient = 0.4;
    vec3 lit = fragColor * (ambient + diffuse * 0.7);
    outColor = vec4(lit, 1.0);
}
