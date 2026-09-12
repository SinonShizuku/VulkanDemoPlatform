#pragma once
#include "../../Start.h"
#include "../VulkanCore.h"
#include "../../Shader/ShaderLoader.h"

// 生命周期约束：shader module 必须早于 VkDevice 释放。不要把它声明成函数内 static 或
// 进程级对象——那会让它在设备销毁之后析构，触发 "vkDestroyShaderModule: Invalid device"
// 并让进程以 0xC0000409 退出（析构里调用 vkDestroy* 的包装类都适用这条约束）。
class VulkanShaderModule {
    VkShaderModule handle = VK_NULL_HANDLE;
public:
    VulkanShaderModule() = default;
    VulkanShaderModule(VkShaderModuleCreateInfo &create_info) {
        create(create_info);
    }
    VulkanShaderModule(const char* filepath) {
        create(filepath);
    }
    VulkanShaderModule(size_t code_size, const uint32_t* pcode) {
        create(code_size, pcode);
    }
    VulkanShaderModule(VulkanShaderModule &&other) noexcept {MoveHandle;}
    ~VulkanShaderModule() {DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyShaderModule);}

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // const function
    VkPipelineShaderStageCreateInfo stage_create_info(VkShaderStageFlagBits stage, const char* entry = "main") const {
        return {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            stage,
            handle,
            entry,
            nullptr
        };
    }

    // non-const function
    result_t create(VkShaderModuleCreateInfo &create_info) {
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        VkResult result = vkCreateShaderModule(VulkanCore::get_singleton().get_vulkan_device().get_device(), &create_info, nullptr, &handle);
        if (result) {
            outstream << std::format("[ VulkanShaderModule ] ERROR\nFailed to create a Shader module!\nError code: {}\n", int32_t(result));
        }
        return result;
    }

    result_t create(const char* filepath /*VkShaderModuleCreateFlags flags*/) {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file) {
            outstream << std::format("[ VulkanShaderModule ] ERROR\nFailed to open the file: {}\n", filepath);
            return VK_RESULT_MAX_ENUM;//没有合适的错误代码，别用VK_ERROR_UNKNOWN
        }
        size_t file_size = size_t(file.tellg());
        std::vector<uint32_t> binaries(file_size / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(binaries.data()), file_size);
        file.close();
        return create(file_size,binaries.data());
    }

    result_t create(size_t code_size, const uint32_t* pcode) {
        VkShaderModuleCreateInfo create_info = {
            .codeSize = code_size,
            .pCode = pcode
        };
        return create(create_info);
    }
};

inline VulkanShaderModule create_shader_module_from_glsl(f_compile_glsl_to_spv &f_compile, const char* filepath, const char* entry = "main") {
    auto code = f_compile(filepath, entry);
    return VulkanShaderModule(code.size(), code.data());
}

