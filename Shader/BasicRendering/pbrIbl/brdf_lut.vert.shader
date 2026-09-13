#version 450
#pragma shader_stage(vertex)

// BRDF LUT 生成用的全屏三角形（移植自 genbrdflut.vert）
layout(location = 0) out vec2 out_uv;

void main() {
    out_uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(out_uv * 2.0 - 1.0, 0.0, 1.0);
}