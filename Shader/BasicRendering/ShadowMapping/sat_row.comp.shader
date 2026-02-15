#version 450
#pragma shader_stage(compute)

layout (local_size_x = 16, local_size_y = 16) in;

layout (binding = 0, rg32f) uniform readonly image2D inputImage;
layout (binding = 1, rg32f) uniform writeonly image2D outputImage;

void main()
{
    ivec2 size = imageSize(inputImage);
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= size.x || gid.y >= size.y) {
        return;
    }

    vec2 sum = vec2(0.0);
    for (int x = 0; x <= gid.x; x++) {
        sum += imageLoad(inputImage, ivec2(x, gid.y)).rg;
    }

    imageStore(outputImage, gid, vec4(sum, 0.0, 0.0));
}
