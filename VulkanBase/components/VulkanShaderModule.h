#pragma once
#include "../../Start.h"
#include "../VulkanCore.h"
#include "../../Shader/ShaderLoader.h"

// shader module 必须早于 VkDevice 释放。本仓库多个 demo 把 module 声明成函数内 static
//（生命周期到进程结束），这些对象会晚于设备析构：析构里调用 vkDestroyShaderModule 时设备
// 句柄已经置空，既会报 "Invalid device"，也让 vkDestroyDevice 报 "leaked objects"。
// 因此每个实例都会登记到 registered_modules，由 release_all() 在销毁设备之前统一释放
//（释放后析构是空操作）。
class VulkanShaderModule {
    VkShaderModule handle = VK_NULL_HANDLE;
public:
    VulkanShaderModule() { register_module(); }
    VulkanShaderModule(VkShaderModuleCreateInfo &create_info) {
        register_module();
        create(create_info);
    }
    VulkanShaderModule(const char* filepath) {
        register_module();
        create(filepath);
    }
    VulkanShaderModule(size_t code_size, const uint32_t* pcode) {
        register_module();
        create(code_size, pcode);
    }
    VulkanShaderModule(VulkanShaderModule &&other) noexcept {
        MoveHandle;
        register_module();
    }
    ~VulkanShaderModule() {
        release();
        unregister_module();
    }

    // 释放所有仍在登记表中的 shader module（必须在 vkDestroyDevice 之前调用）。
    static void release_all() {
        for (VulkanShaderModule* module : registered_modules) {
            if (module)
                module->release();
        }
    }

    // 释放本实例持有的 shader module（句柄随后置空；重复调用安全）。
    void release() {
        DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(), vkDestroyShaderModule);
    }

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
private:
    inline static std::vector<VulkanShaderModule*> registered_modules;

    void register_module() {
        if (std::find(registered_modules.begin(), registered_modules.end(), this) == registered_modules.end())
            registered_modules.push_back(this);
    }

    void unregister_module() {
        std::erase(registered_modules, this);
    }

public:
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

