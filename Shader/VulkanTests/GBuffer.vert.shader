#version 460
#pragma shader_stage(vertex)

layout(location = 0) in vec3 i_Position;         //逐顶点
layout(location = 1) in vec3 i_Normal;           //逐顶点
layout(location = 2) in vec4 i_AlbedoSpecular;   //逐顶点
layout(location = 3) in vec3 i_InstancePosition; //逐实例
layout(location = 0) out vec4 o_NormalZ;
layout(location = 1) out vec4 o_AlbedoSpecular;
layout(binding = 0) uniform descriptorConstants_pv {
    mat4 proj;
    mat4 view; //观察矩阵
};

void main() {
    vec3 position = i_Position + i_InstancePosition;
    gl_Position = proj * view * vec4(position, 1);
    o_NormalZ = vec4(i_Normal, gl_Position.w); //此处gl_Position.w等于相机空间中的z坐标
    o_AlbedoSpecular = i_AlbedoSpecular;
}