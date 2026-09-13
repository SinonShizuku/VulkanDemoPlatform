#version 450
#pragma shader_stage(fragment)

// 余弦卷积（Lambertian irradiance）：移植自 D:\Vulkan\shaders\glsl\pbrtexture\irradiancecube.frag
layout(location = 0) in vec3 in_direction;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform samplerCube env_cube;

layout(push_constant) uniform PushConsts {
    uint face;
    float delta_phi;
    float delta_theta;
    float param2;
} pc;

const float PI = 3.14159265359;

void main() {
    const vec3 N = normalize(in_direction);
    const vec3 up = vec3(0.0, 1.0, 0.0);
    const vec3 right = normalize(cross(up, N));
    const vec3 forward = cross(N, right);

    const float TWO_PI = PI * 2.0;
    const float HALF_PI = PI * 0.5;

    vec3 color = vec3(0.0);
    uint sample_count = 0u;
    for (float phi = 0.0; phi < TWO_PI; phi += pc.delta_phi) {
        for (float theta = 0.0; theta < HALF_PI; theta += pc.delta_theta) {
            const vec3 temp = cos(phi) * right + sin(phi) * forward;
            const vec3 sample_dir = cos(theta) * N + sin(theta) * temp;
            color += texture(env_cube, sample_dir).rgb * cos(theta) * sin(theta);
            ++sample_count;
        }
    }
    out_color = vec4(PI * color / float(sample_count), 1.0);
}