#version 450
#pragma shader_stage(fragment)

// equirectangular 2D 环境贴图 → cube 面（M1.2 的第一步，参考实现没有这步因为它们直接给 cube KTX）
layout(location = 0) in vec3 in_direction;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D equirect_map;

const float PI = 3.14159265359;

void main() {
    const vec3 d = normalize(in_direction);
    // 标准 equirect 展开：u 由 atan(z, x) 给出，v = 0 对应 +Y（图像顶部）
    const vec2 uv = vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
    out_color = vec4(texture(equirect_map, uv).rgb, 1.0);
}