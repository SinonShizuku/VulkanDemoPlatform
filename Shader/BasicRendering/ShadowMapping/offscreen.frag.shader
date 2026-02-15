#version 450
#pragma shader_stage(fragment)

layout (binding = 0) uniform UBO
{
    mat4 depthMVP;
    float zNear;
    float zFar;
} ubo;

layout (location = 0) out vec2 outMoments;

float linearizeDepth(float d)
{
    return (ubo.zNear * ubo.zFar) / (ubo.zFar - d * (ubo.zFar - ubo.zNear));
}

void main()
{
    // Use linear depth for moments to reduce foggy leakage in VSM/VSSM.
    float depth = linearizeDepth(gl_FragCoord.z);

    // Variance term with derivative-based bias to reduce light leaking.
    float dx = dFdx(depth);
    float dy = dFdy(depth);

    float m1 = depth;
    float m2 = depth * depth + 0.25 * (dx * dx + dy * dy);

    outMoments = vec2(m1, m2);
}
