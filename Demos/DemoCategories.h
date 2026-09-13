#pragma once
#include "../Start.h"

enum class DemoCategoryType {
    VULKAN_TESTS,           // Vulkan功能测试
    BASIC_RENDERING,   // 基础实现
    RASTERIZATION,         // 光栅化
    PBR_RAY_TRACING,       // PBR光线追踪
    // 预留扩展空间
    CUSTOM_CATEGORY_1,
    CUSTOM_CATEGORY_2
};

using DemoType = std::string;

inline std::unordered_map<std::string,std::vector<DemoType>> demos = {
    // Vulkan Tests
    {"Vulkan Test Examples",
        {
        "BuffersAndPictureTest",
        "ImagelessFramebufferTest",
        "DynamicRenderingTest",
        "OffScreenRenderingTest",
        "FrameGraphOffScreenTest",
            "DepthAttachmentTest",
            "DeferredRenderingTest"
        }
    },
    {
        "Basic Rendering Examples",
        {
            "Loading & Rendering glTF Model",
            "ShadowMapping",
            "InstancedScene",
            "PbrIbl"
        }
    }

};

inline std::string current_demo_name = "BuffersAndPictureTest";

