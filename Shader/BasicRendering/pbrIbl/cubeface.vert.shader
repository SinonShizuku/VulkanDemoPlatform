#version 450
#pragma shader_stage(vertex)

// cube 面渲染用的顶点着色器：不需要顶点缓冲，用全屏三角形生成面内 NDC，
// 再把 (face, ndc) 映射成该面的世界方向（与 Vulkan 标准 cubemap 面选择/UV 约定一致）。
layout(push_constant) uniform PushConsts {
    uint face;        // 0..5：+X, -X, +Y, -Y, +Z, -Z（cube 层序）
    float param0;     // irradiance: deltaPhi / prefilter: roughness
    float param1;     // irradiance: deltaTheta / prefilter: 采样数
    float param2;
} pc;

layout(location = 0) out vec3 out_direction;
layout(location = 1) out vec2 out_uv;

vec3 face_direction(uint face, vec2 uv) {
    switch (face) {
        case 0u: return vec3( 1.0, -uv.y, -uv.x);   // +X
        case 1u: return vec3(-1.0, -uv.y,  uv.x);   // -X
        case 2u: return vec3( uv.x,  1.0,  uv.y);   // +Y
        case 3u: return vec3( uv.x, -1.0, -uv.y);   // -Y
        case 4u: return vec3( uv.x, -uv.y,  1.0);   // +Z
        default: return vec3(-uv.x, -uv.y, -1.0);   // -Z
    }
}

void main() {
    const vec2 corners[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 ndc = corners[gl_VertexIndex];
    out_direction = face_direction(pc.face, ndc);
    out_uv = ndc * 0.5 + 0.5;
    gl_Position = vec4(ndc, 1.0, 1.0);
}