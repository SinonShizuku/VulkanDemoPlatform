#version 450
#pragma shader_stage(vertex)

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec3 inNormal;

layout (binding = 0) uniform UBO
{
    mat4 projection;
    mat4 view;
    mat4 model;         // (这个是来自UBO的 mat4(1.0))
    mat4 lightSpace;    // (这个是 light's VP 矩阵)
    vec4 lightPos;
    float light_size;
    float zNear;
    float zFar;
} ubo;

// 1. [新增] 声明 Push Constant
// 这个 block 必须与 C++ 中的 VkPushConstantRange 匹配
layout(push_constant) uniform PushConstants {
    mat4 node_matrix;
} constants;


layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;
layout (location = 4) out vec4 outShadowCoord;

const mat4 biasMat = mat4(
0.5, 0.0, 0.0, 0.0,
0.0, 0.5, 0.0, 0.0,
0.0, 0.0, 1.0, 0.0,
0.5, 0.5, 0.0, 1.0 );

void main()
{
    // 2. [修改] 计算真正的模型矩阵和世界坐标
    // ubo.model 是你的(1.0)，constants.node_matrix 是每个节点的变换
    mat4 true_model_matrix = ubo.model * constants.node_matrix;
    vec4 world_pos = true_model_matrix * vec4(inPos, 1.0);

    outColor = inColor;

    // 3. [修改] 使用 true_model_matrix 和 world_pos 来计算所有输出
    gl_Position = ubo.projection * ubo.view * world_pos;

    outNormal = mat3(true_model_matrix) * inNormal;
    outLightVec = normalize(ubo.lightPos.xyz - world_pos.xyz); // 原代码(inPos)有误，应使用world_pos
    outViewVec = -world_pos.xyz; // (即从顶点指向(0,0,0)的向量)

    // 4. [修改] 阴影坐标也需要使用 world_pos
    // (biasMat * ubo.lightSpace) 是从世界空间到阴影贴图空间的变换
    outShadowCoord = (biasMat * ubo.lightSpace) * world_pos;
}