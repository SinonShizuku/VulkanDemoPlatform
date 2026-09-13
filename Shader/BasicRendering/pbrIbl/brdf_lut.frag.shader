#version 450
#pragma shader_stage(fragment)

// split-sum 的 BRDF 积分 LUT（移植自 genbrdflut.frag，NUM_SAMPLES 从 spec constant 改为常量）
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

const uint NUM_SAMPLES = 512u;
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

float g_schlick_smith_ggx(float dot_nl, float dot_nv, float roughness) {
    const float k = (roughness * roughness) / 2.0;
    const float gl = dot_nl / (dot_nl * (1.0 - k) + k);
    const float gv = dot_nv / (dot_nv * (1.0 - k) + k);
    return gl * gv;
}

vec2 integrate_brdf(float no_v, float roughness) {
    const vec3 N = vec3(0.0, 0.0, 1.0);
    const vec3 V = vec3(sqrt(1.0 - no_v * no_v), 0.0, no_v);

    vec2 lut = vec2(0.0);
    for (uint i = 0u; i < NUM_SAMPLES; ++i) {
        const vec2 xi = hammersley2d(i, NUM_SAMPLES);
        const vec3 H = importance_sample_ggx(xi, roughness, N);
        const vec3 L = 2.0 * dot(V, H) * H - V;

        const float dot_nl = max(dot(N, L), 0.0);
        const float dot_nv = max(dot(N, V), 0.0);
        const float dot_vh = max(dot(V, H), 0.0);
        const float dot_nh = max(dot(H, N), 0.0);

        if (dot_nl > 0.0) {
            const float g = g_schlick_smith_ggx(dot_nl, dot_nv, roughness);
            const float g_vis = (g * dot_vh) / max(dot_nh * dot_nv, 1e-6);
            const float fc = pow(1.0 - dot_vh, 5.0);
            lut += vec2((1.0 - fc) * g_vis, fc * g_vis);
        }
    }
    return lut / float(NUM_SAMPLES);
}

void main() {
    out_color = vec4(integrate_brdf(in_uv.s, in_uv.t), 0.0, 1.0);
}