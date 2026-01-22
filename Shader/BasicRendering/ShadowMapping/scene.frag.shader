#version 450
#pragma shader_stage(fragment)

layout (binding = 0) uniform UBO
{
    mat4 projection;
    mat4 view;
    mat4 model;         // (这个是来自UBO的 mat4(1.0))
    mat4 lightSpace;    // (这个是 light's VP 矩阵)
    vec4 lightPos;
    float lightSize;
    float zNear;
    float zFar;
} ubo;
layout (binding = 1) uniform sampler2D shadowMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;
layout (location = 4) in vec4 inShadowCoord;

// 0: Hard Shadow, 1: Poisson PCF, 2: PCSS
layout (constant_id = 0) const int filter_type = 0;

layout (location = 0) out vec4 outFragColor;

#define ambient 0.1

// 定义泊松分布采样点的数量
#define POISSON_SAMPLES 16

// 预计算的泊松圆盘采样点 (分布在 -1.0 到 1.0 之间)
const vec2 poissonDisk[16] = vec2[](
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ),
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
   vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ),
   vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ),
   vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ),
   vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 )
);

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

float textureProj(vec4 shadowCoord, vec2 off)
{
    float shadow = 1.0;
    if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 )
    {
        float dist = texture( shadowMap, shadowCoord.st + off ).r;
        if ( shadowCoord.w > 0.0 && dist < shadowCoord.z )
        {
            shadow = ambient;
        }
    }
    return shadow;
}

float filterPCF(vec4 sc)
{
    ivec2 texDim = textureSize(shadowMap, 0);
    float scale = 1.5;
    float dx = scale * 1.0 / float(texDim.x);
    float dy = scale * 1.0 / float(texDim.y);

    float shadowFactor = 0.0;
    int count = 0;
    int range = 1;

    for (int x = -range; x <= range; x++)
    {
        for (int y = -range; y <= range; y++)
        {
            shadowFactor += textureProj(sc, vec2(dx*x, dy*y));
            count++;
        }

    }
    return shadowFactor / count;
}

float linearizeDepth(float d) {
    return (ubo.zNear * ubo.zFar) / (ubo.zFar - d * (ubo.zFar - ubo.zNear));
}

float findBlocker(vec2 uv, float zReceiverProjected) {
    int blockers = 0;
    float avgBlockerDepthLinear = 0.0;

    // 1. 将接收点深度转为线性空间
    float zReceiverLinear = linearizeDepth(zReceiverProjected);

    // 2. 计算搜索半径 (现在 zReceiverLinear 和 ubo.zNear 在同一空间了)
    // 根据相似三角形：searchWidth(世界单位) = lightSize * (d_rec - d_near) / d_rec
    float searchWidthWorld = ubo.lightSize * (zReceiverLinear - ubo.zNear) / zReceiverLinear;

    // [关键] 将世界单位的 searchWidth 映射到 UV 空间 (0..1)
    // 这里需要一个缩放系数，通常取决于你视锥体在 zNear 处的宽度
    float searchWidthUV = searchWidthWorld / (zReceiverLinear * tan(radians(45.0 / 2.0)) * 2.0);

    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    float noise = rand(gl_FragCoord.xy);
    float s = sin(noise * 6.28);
    float c = cos(noise * 6.28);
    mat2 rot = mat2(c, -s, s, c);

    for(int i = 0; i < POISSON_SAMPLES; i++) {
        vec2 offset = rot * poissonDisk[i] * searchWidthUV;
        float shadowMapSample = texture(shadowMap, uv + offset).r;
        float sampleDepthLinear = linearizeDepth(shadowMapSample);

        // 在线性空间进行深度比较
        if(sampleDepthLinear < zReceiverLinear - 0.01) {
            avgBlockerDepthLinear += sampleDepthLinear;
            blockers++;
        }
    }

    if(blockers > 0) return avgBlockerDepthLinear / float(blockers);
    return -1.0;
}

float filterPoisson(vec4 sc, float radiusUV) // 改为 radiusUV
{
    float shadowFactor = 0.0;
    float noise = rand(gl_FragCoord.xy);
    float s = sin(noise * 6.28);
    float c = cos(noise * 6.28);
    mat2 rot = mat2(c, -s, s, c);

    for (int i = 0; i < POISSON_SAMPLES; i++)
    {
        // 直接使用 radiusUV，不再乘 texelSize
        vec2 offset = rot * poissonDisk[i] * radiusUV;
        shadowFactor += textureProj(sc, offset);
    }
    return shadowFactor / float(POISSON_SAMPLES);
}

float PCSS(vec4 sc) {
    vec2 uv = sc.xy;
    float zReceiverProjected = sc.z;
    float zReceiverLinear = linearizeDepth(zReceiverProjected);

    // 1. 寻找平均遮挡深度 (返回的是线性深度)
    float avgBlockerDepthLinear = findBlocker(uv, zReceiverProjected);

    if(avgBlockerDepthLinear == -1.0) return 1.0;

    // 2. 半影估算 (全部使用线性深度)
    // w_penumbra = (d_receiver - d_blocker) * w_light / d_blocker
    float penumbraWorld = (zReceiverLinear - avgBlockerDepthLinear) * ubo.lightSize / avgBlockerDepthLinear;

    // 3. 将世界空间的半影宽度转为 UV 空间半径
    // 这里除以 zReceiverLinear 是为了补偿透视投影带来的“近大远小”
    float penumbraRadiusUV = penumbraWorld / (zReceiverLinear * 0.5); // 0.5 是简化系数

    return filterPoisson(sc, penumbraRadiusUV); // 50.0 用于放大效果
}

void main()
{
    float shadow = 1.0;
    vec4 sc = inShadowCoord / inShadowCoord.w;

    if (sc.z > -1.0 && sc.z < 1.0)
    {
        if(filter_type == 0) shadow = textureProj(sc, vec2(0.0));
        else if(filter_type == 1) shadow = filterPoisson(sc, 2.0 / float(textureSize(shadowMap, 0)).x); // 固定半径 PCF
        else if(filter_type == 2) shadow = PCSS(sc); // 完整 PCSS
    }

    vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 V = normalize(inViewVec);
    vec3 R = normalize(-reflect(L, N));
    vec3 diffuse = max(dot(N, L), ambient) * inColor;

    outFragColor = vec4(diffuse * shadow, 1.0);

}