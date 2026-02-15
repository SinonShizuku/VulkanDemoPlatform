#version 450
#pragma shader_stage(compute)

layout (local_size_x = 256, local_size_y = 1) in;

layout (binding = 0, rg32f) uniform readonly image2D rowPartial;
layout (binding = 1, rg32f) uniform readonly image2D blockPrefix;
layout (binding = 2, rg32f) uniform writeonly image2D rowSAT;

void main()
{
    ivec2 size = imageSize(rowPartial);
    uint row = gl_WorkGroupID.y;
    uint block = gl_WorkGroupID.x;
    uint tid = gl_LocalInvocationID.x;

    if (row >= uint(size.y)) {
        return;
    }

    uint x = block * 256u + tid;
    if (x >= uint(size.x)) {
        return;
    }

    vec2 offset = vec2(0.0);
    if (block > 0u) {
        offset = imageLoad(blockPrefix, ivec2(block - 1u, row)).rg;
    }

    vec2 value = imageLoad(rowPartial, ivec2(x, row)).rg;
    imageStore(rowSAT, ivec2(x, row), vec4(value + offset, 0.0, 0.0));
}
