#version 450
#pragma shader_stage(compute)

layout (local_size_x = 256, local_size_y = 1) in;

layout (binding = 0, rg32f) uniform readonly image2D blockSums;
layout (binding = 1, rg32f) uniform writeonly image2D blockPrefix;

shared vec2 shared_data[256];

void main()
{
    ivec2 size = imageSize(blockSums);
    uint row = gl_WorkGroupID.y;
    uint tid = gl_LocalInvocationID.x;

    if (row >= uint(size.y)) {
        return;
    }

    uint x = tid;
    vec2 v = vec2(0.0);
    if (x < uint(size.x)) {
        v = imageLoad(blockSums, ivec2(x, row)).rg;
    }
    shared_data[tid] = v;

    for (uint stride = 1u; stride < 256u; stride <<= 1u) {
        barrier();
        vec2 addv = vec2(0.0);
        if (tid >= stride) {
            addv = shared_data[tid - stride];
        }
        barrier();
        shared_data[tid] += addv;
    }
    barrier();

    if (x < uint(size.x)) {
        imageStore(blockPrefix, ivec2(x, row), vec4(shared_data[tid], 0.0, 0.0));
    }
}
