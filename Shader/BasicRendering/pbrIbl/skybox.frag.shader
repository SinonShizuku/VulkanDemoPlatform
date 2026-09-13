#version 450
#pragma shader_stage(fragment)

layout(location = 0) in vec3 in_direction;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform samplerCube environment_cube;

void main() {
    out_color = vec4(texture(environment_cube, normalize(in_direction)).rgb, 1.0);
}