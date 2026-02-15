    #version 450
#pragma shader_stage(compute)

layout (local_size_x = 256, local_size_y = 1) in;

layout (binding = 0, rg32f) uniform readonly image2D colPartial;
layout (binding = 1, rg32f) uniform readonly image2D blockPrefix;
layout (binding = 2, rg32f) uniform writeonly image2D colSAT;

void main()
{
    ivec2 size = imageSize(colPartial);
    uint col = gl_WorkGroupID.x;
    uint block = gl_WorkGroupID.y;
    uint tid = gl_LocalInvocationID.x;

    if (col >= uint(size.x)) {
        return;
    }

    uint y = block * 256u + tid;
    if (y >= uint(size.y)) {
        return;
    }

    vec2 offset = vec2(0.0);
    if (block > 0u) {
        offset = imageLoad(blockPrefix, ivec2(col, block - 1u)).rg;
    }

    vec2 value = imageLoad(colPartial, ivec2(col, y)).rg;
    imageStore(colSAT, ivec2(col, y), vec4(value + offset, 0.0, 0.0));
}
