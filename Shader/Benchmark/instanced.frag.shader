#version 450
#pragma shader_stage(fragment)
layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 light_dir = normalize(vec3(0.4, 0.8, 0.3));
    float diffuse = clamp(dot(normalize(in_normal), light_dir), 0.0, 1.0);
    out_color = vec4(in_color * (0.15 + 0.85 * diffuse), 1.0);
}
