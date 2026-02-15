//#version 460
//#pragma shader_stage(vertex)
//
//layout(push_constant) uniform pushConstants {
//    vec2 u_Positions[3];
//};
//
//layout(location = 0) in vec2 i_Position;
//layout(location = 1) in vec4 i_Color;
//layout(location = 0) out vec4 o_Color;
//
//void main() {
//    // 位置计算暂时不重要，可以保持原样
//    gl_Position = vec4(i_Position + u_Positions[gl_InstanceIndex], 0, 1);
//
//    // 关键：根据实例ID输出纯色
//    vec4 debug_colors[3] = vec4[](
//    vec4(1.0, 0.0, 0.0, 1.0), // 实例0: 纯红
//    vec4(0.0, 1.0, 0.0, 1.0), // 实例1: 纯绿
//    vec4(0.0, 0.0, 1.0, 1.0)  // 实例2: 纯蓝
//    );
//    o_Color = debug_colors[gl_InstanceIndex];
//}

#version 460
#pragma shader_stage(vertex)

// 定义一个匹配 C++ 结构体的 uniform block
layout(push_constant) uniform PushConstants {
    vec2 offset0;
    vec2 offset1;
    vec2 offset2;
} pc;

layout(location = 0) in vec2 i_Position;
layout(location = 1) in vec4 i_Color;

layout(location = 0) out vec4 o_Color;

void main() {
    vec2 final_offset;

    // 使用 if/else 来手动选择偏移量
    if (gl_InstanceIndex == 0) {
        final_offset = pc.offset0;
    } else if (gl_InstanceIndex == 1) {
        final_offset = pc.offset1;
    } else {
        final_offset = pc.offset2;
    }

    // 或者使用 switch (需要 GLSL 4.30+)
    /*
    switch(gl_InstanceIndex) {
        case 0: final_offset = pc.offset0; break;
        case 1: final_offset = pc.offset1; break;
        case 2: final_offset = pc.offset2; break;
        default: final_offset = vec2(0.0); break;
    }
    */

    gl_Position = vec4(i_Position + final_offset, 0.0, 1.0);
    o_Color = i_Color; // 恢复正常的颜色输出
}