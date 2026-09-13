#version 450
#pragma shader_stage(fragment)

// GGX 预滤波（split-sum 的 prefiltered environment）：移植自
// D:\Vulkan\shaders\glsl\pbrtexture\prefilterenvmap.frag（含重要性采样与 mip 选择的偏置）
layout(location = 0) in vec3 in_direction;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform samplerCube env_cube;

layout(push_constant) uniform PushConsts {
    uint face;
    float roughness;
    float sample_count;
    float param2;
} pc;

const float PI = 3.14159265359;

float random(vec2 co) {
    const float a = 12.9898;
    const float b = 78.233;
    const float c = 43758.5453;
    const float dt = dot(co.xy, vec2(a, b));
    const float sn = mod(dt, 3.14);
    return fract(sin(sn) * c);
}

vec2 hammersley2d(uint i, uint n) {
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    const float rdi = float(bits) * 2.3283064365386963e-10;
    return vec2(float(i) / float(n), rdi);
}

vec3 importance_sample_ggx(vec2 xi, float roughness, vec3 normal) {
    const float alpha = roughness * roughness;
    const float phi = 2.0 * PI * xi.x + random(normal.xz) * 0.1;
    const float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    const float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
    const vec3 h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

    const vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    const vec3 tangent_x = normalize(cross(up, normal));
    const vec3 tangent_y = normalize(cross(normal, tangent_x));
    return normalize(tangent_x * h.x + tangent_y * h.y + normal * h.z);
}

float d_ggx(float dot_nh, float roughness) {
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float denom = dot_nh * dot_nh * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

void main() {
    const vec3 N = normalize(in_direction);
    const vec3 V = N;
    const uint num_samples = uint(pc.sample_count + 0.5);
    const float env_map_dim = float(textureSize(env_cube, 0).s);

    vec3 color = vec3(0.0);
    float total_weight = 0.0;
    for (uint i = 0u; i < num_samples; ++i) {
        const vec2 xi = hammersley2d(i, num_samples);
        const vec3 H = importance_sample_ggx(xi, pc.roughness, N);
        const vec3 L = 2.0 * dot(V, H) * H - V;
        const float dot_nl = clamp(dot(N, L), 0.0, 1.0);
        if (dot_nl > 0.0) {
            const float dot_nh = clamp(dot(N, H), 0.0, 1.0);
            const float dot_vh = clamp(dot(V, H), 0.0, 1.0);
            const float pdf = d_ggx(dot_nh, pc.roughness) * dot_nh / (4.0 * dot_vh) + 0.0001;
            const float omega_s = 1.0 / (float(num_samples) * pdf);
            const float omega_p = 4.0 * PI / (6.0 * env_map_dim * env_map_dim);
            const float mip_level = pc.roughness == 0.0 ? 0.0 : max(0.5 * log2(omega_s / omega_p) + 1.0, 0.0);
            color += textureLod(env_cube, L, mip_level).rgb * dot_nl;
            total_weight += dot_nl;
        }
    }
    out_color = vec4(color / max(total_weight, 1e-4), 1.0);
}