#version 450
#pragma shader_stage(vertex)
// 实例化基准场景：模型矩阵通过 instance-rate 顶点属性传入（baseline，后面 GPU-driven 再换 storage buffer + indirect）。
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) in vec4 in_model_0;
layout(location = 5) in vec4 in_model_1;
layout(location = 6) in vec4 in_model_2;
layout(location = 7) in vec4 in_model_3;

layout(set = 0, binding = 0) uniform SceneUniform {
    mat4 view_projection;
} scene;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_color;

void main() {
    mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
    vec4 world = model * vec4(in_position, 1.0);
    out_normal = normalize(mat3(model) * in_normal);
    out_color = in_color.rgb;
    gl_Position = scene.view_projection * world;
}
