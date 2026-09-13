#version 450
#pragma shader_stage(vertex)

// Skybox：不需要顶点缓冲，用 gl_VertexIndex 生成一个覆盖全屏的大三角形，
// 再用 inv(view*projection) 把 NDC 反投到世界空间，得到该像素的视线方向。
layout(push_constant) uniform PushConsts {
    mat4 inv_view_projection;
    vec4 camera_position;
} pc;

layout(location = 0) out vec3 out_direction;

void main() {
    const vec2 corners[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 ndc = corners[gl_VertexIndex];
    const vec4 world = pc.inv_view_projection * vec4(ndc, 1.0, 1.0);
    out_direction = world.xyz / world.w - pc.camera_position.xyz;
    gl_Position = vec4(ndc, 1.0, 1.0);
}