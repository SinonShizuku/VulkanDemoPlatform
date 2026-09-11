#pragma once
#include <queue>

#include "../../Start.h"
#include "VulkanInstance.h"
#include "../VKFormat.h"

class VulkanDevice {
public:
    // getter
    [[nodiscard]] VkDevice get_device() const {
        return device;
    }

    [[nodiscard]] std::vector<const char *> get_device_extensions() const {
        return device_extensions;
    }

    [[nodiscard]] VkPhysicalDevice get_physical_device() const {
        return physical_device;
    }

    // [[nodiscard]] VkPhysicalDeviceProperties get_physical_device_properties() const {
    //     return physical_device_properties;
    // }
    //
    // [[nodiscard]] VkPhysicalDeviceMemoryProperties &get_physical_device_memory_properties() {
    //     return physical_device_memory_properties;
    // }

    [[nodiscard]] std::vector<VkPhysicalDevice> & get_available_physical_devices() {
        return available_physical_devices;
    }

    [[nodiscard]] VkPhysicalDevice get_available_physical_device(uint32_t index) const {
        return available_physical_devices[index];
    }

    [[nodiscard]] uint32_t get_available_physical_device_count() const {
        return available_physical_devices.size();
    }

    [[nodiscard]] uint32_t get_queue_family_index_graphics() const {
        return queue_family_index_graphics;
    }

    [[nodiscard]] uint32_t get_queue_family_index_presentation() const {
        return queue_family_index_presentation;
    }

    [[nodiscard]] uint32_t get_queue_family_index_compute() const {
        return queue_family_index_compute;
    }

    [[nodiscard]] VkQueue get_queue_graphics() const {
        return queue_graphics;
    }

    [[nodiscard]] VkQueue get_queue_presentation() const {
        return queue_presentation;
    }

    [[nodiscard]] VkQueue get_queue_compute() const {
        return queue_compute;
    }

    [[nodiscard]] std::vector<std::function<void()>> & get_callbacks_create_device() {
        return callbacks_create_device;
    }

    [[nodiscard]] std::vector<std::function<void()>> & get_callbacks_destroy_device() {
        return callbacks_destroy_device;
    }

    [[nodiscard]] const VkFormatProperties& get_format_properties(VkFormat format) const {
        if constexpr (ENABLE_DEBUG_MESSENGER)
            if (uint32_t(format) >= std::size(format_infos_v1_0))
                outstream << std::format("[ VulkanFormatProperties ] ERROR\nThis function only supports definite formats provided by VK_VERSION_1_0.\n"),
                abort();
        return format_properties[format];
    }

    constexpr VulkanFormatInfo get_format_info(VkFormat format) {
        if constexpr (ENABLE_DEBUG_MESSENGER)
            if (uint32_t(format) >= std::size(format_infos_v1_0))
                outstream << std::format("[ VulkanFormatInfo ] ERROR\nThis function only supports definite formats provided by VK_VERSION_1_0.\n"),
                        abort();
        return format_infos_v1_0[uint32_t(format)];
    }

    [[nodiscard]] size_t get_format_properties_size() const {
        return std::size(format_properties);
    }

    constexpr const VkPhysicalDeviceFeatures2& get_physical_device_features() const {
        return physical_device_features;
    }

    constexpr const VkPhysicalDeviceVulkan11Features& get_physical_device_vulkan11_features() const {
        return physical_device_features_vulkan11;
    }

    constexpr const VkPhysicalDeviceVulkan12Features& get_physical_device_vulkan12_features() const {
        return physical_device_features_vulkan12;
    }

    constexpr const VkPhysicalDeviceVulkan13Features& get_physical_device_vulkan13_features() const {
        return physical_device_features_vulkan13;
    }

    VkPhysicalDeviceFeatures2& get_physical_device_features() {
        return physical_device_features;
    }

    VkPhysicalDeviceVulkan11Features& get_physical_device_vulkan11_features() {
        return physical_device_features_vulkan11;
    }

    VkPhysicalDeviceVulkan12Features& get_physical_device_vulkan12_features() {
        return physical_device_features_vulkan12;
    }

    VkPhysicalDeviceVulkan13Features& get_physical_device_vulkan13_features() {
        return physical_device_features_vulkan13;
    }

    [[nodiscard]] constexpr const VkPhysicalDeviceProperties & get_physical_device_properties() const {
        return physical_device_properties.properties;
    }

    [[nodiscard]] constexpr const VkPhysicalDeviceVulkan11Properties & get_physical_device_properties_vulkan11() const {
        return physical_device_properties_vulkan11;
    }

    [[nodiscard]] constexpr const VkPhysicalDeviceVulkan12Properties & get_physical_device_properties_vulkan12() const {
        return physical_device_properties_vulkan12;
    }

    [[nodiscard]] constexpr const VkPhysicalDeviceVulkan13Properties & get_physical_device_properties_vulkan13() const {
        return physical_device_properties_vulkan13;
    }

    [[nodiscard]] uint32_t get_physical_device_api_version() const {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        return properties.apiVersion;
    }

    [[nodiscard]] constexpr const VkPhysicalDeviceMemoryProperties & get_physical_device_memory_properties() const {
        return physical_device_memory_properties.memoryProperties;
    }

    VkFormat get_supported_depth_format() {
        std::vector<VkFormat> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };

        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);

            // 检查是否支持在 TILING_OPTIMAL 下作为深度附件
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                return format;
            }
        }
        outstream << std::format("[ VulkanDevice ] ERROR\nFailed to find supported depth format!\n");
        return VK_FORMAT_UNDEFINED;
    }

    VkBool32 format_is_filterable(VkFormat format, VkImageTiling tiling) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_OPTIMAL) return props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if (tiling == VK_IMAGE_TILING_LINEAR) return props.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        return false;
    }

    void set_device(VkDevice device) {
        this->device = device;
    }

    void set_queue_family_index_graphics(uint32_t queue_family_index_graphics) {
        this->queue_family_index_graphics = queue_family_index_graphics;
    }

    void set_queue_family_index_presentation(uint32_t queue_family_index_presentation) {
        this->queue_family_index_presentation = queue_family_index_presentation;
    }

    void set_queue_family_index_compute(uint32_t queue_family_index_compute) {
        this->queue_family_index_compute = queue_family_index_compute;
    }

    void set_device_extensions(const std::vector<const char*>& extensionNames) {
        device_extensions = extensionNames;
    }

    void set_physical_device(VkPhysicalDevice physical_device) {
        this->physical_device = physical_device;
    }

    void set_physical_device_format_properties(){
        for (size_t i=0; i<std::size(format_properties);i++) {
            vkGetPhysicalDeviceFormatProperties(physical_device,VkFormat(i),&format_properties[i]);
        }
    }

    void add_device_extension(const char *extension_name);

    void add_next_structure_device_create_info(auto& next, bool allow_duplicate = false) {
        set_pnext(pnext_device_create_info, &next, allow_duplicate);
    }

    void add_next_structure_physical_device_features(auto& next, bool allow_duplicate = false) {
        set_pnext(pnext_physical_device_features, &next, allow_duplicate);
    }

    void add_next_structure_physical_device_properties(auto& next, bool allow_duplicate = false) {
        set_pnext(pnext_physical_device_properties, &next, allow_duplicate);
    }

    void add_next_structure_physical_device_memory_properties(auto& next, bool allow_duplicate = false) {
        set_pnext(pnext_physical_device_memory_properties, &next, allow_duplicate);
    }

    result_t create_device(uint32_t api_version, VkDeviceCreateFlags flags = 0) {
        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo queue_create_info[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueCount = 1,
                .pQueuePriorities = &queue_priority},
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueCount = 1,
                .pQueuePriorities = &queue_priority},
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueCount = 1,
                .pQueuePriorities = &queue_priority}
        };
        uint32_t queue_create_info_count = 0;
        if (queue_family_index_graphics!=VK_QUEUE_FAMILY_IGNORED)
            queue_create_info[queue_create_info_count++].queueFamilyIndex = queue_family_index_graphics;
        if (queue_family_index_presentation!=VK_QUEUE_FAMILY_IGNORED
            && queue_family_index_presentation != queue_family_index_graphics)
            queue_create_info[queue_create_info_count++].queueFamilyIndex = queue_family_index_presentation;
        if (queue_family_index_compute!=VK_QUEUE_FAMILY_IGNORED
            && queue_family_index_compute != queue_family_index_graphics
            && queue_family_index_compute != queue_family_index_presentation)
            queue_create_info[queue_create_info_count++].queueFamilyIndex = queue_family_index_compute;
        get_physical_device_features(api_version);

        // Query fills the structures with every supported feature. Preserve the
        // pNext chain but clear the values so only explicit requirements are enabled.
        void* features_pnext = physical_device_features.pNext;
        physical_device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        physical_device_features.pNext = features_pnext;

        if (api_version >= VK_API_VERSION_1_1) {
            void* features_11_pnext = physical_device_features_vulkan11.pNext;
            physical_device_features_vulkan11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
            physical_device_features_vulkan11.pNext = features_11_pnext;
        }
        if (api_version >= VK_API_VERSION_1_2) {
            void* features_12_pnext = physical_device_features_vulkan12.pNext;
            physical_device_features_vulkan12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            physical_device_features_vulkan12.pNext = features_12_pnext;
        }
        if (api_version >= VK_API_VERSION_1_3) {
            void* features_13_pnext = physical_device_features_vulkan13.pNext;
            physical_device_features_vulkan13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            physical_device_features_vulkan13.pNext = features_13_pnext;
        }

        for (auto& callback : callbacks_configure_device)
            callback();

        VkDeviceCreateInfo device_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .flags = flags,
            .queueCreateInfoCount = queue_create_info_count,
            .pQueueCreateInfos = queue_create_info,
            .enabledExtensionCount = uint32_t(device_extensions.size()),
            .ppEnabledExtensionNames = device_extensions.data(),
            // .pEnabledFeatures = &physical_device_features
        };
        void** ppnext = nullptr;
        if (api_version >= VK_API_VERSION_1_1)
            ppnext = set_pnext(pnext_device_create_info, &physical_device_features);
        else
            device_create_info.pEnabledFeatures = &physical_device_features.features;
        device_create_info.pNext = pnext_device_create_info;
        if (ppnext)
            *ppnext = nullptr;
        if (result_t result = vkCreateDevice(physical_device, &device_create_info, nullptr, &device);result!=VK_SUCCESS) {
            callbacks_configure_device.clear();
            outstream << std::format("[ VulkanDevice ] ERROR\nFailed to create a vulkan logical device!\nError code: {}\n", int32_t(result));
            return result;
        }
        callbacks_configure_device.clear();
        if (queue_family_index_graphics!=VK_QUEUE_FAMILY_IGNORED)
            vkGetDeviceQueue(device,queue_family_index_graphics,0,&queue_graphics);
        if (queue_family_index_presentation!=VK_QUEUE_FAMILY_IGNORED)
            vkGetDeviceQueue(device,queue_family_index_presentation,0,&queue_presentation);
        if (queue_family_index_compute!=VK_QUEUE_FAMILY_IGNORED)
            vkGetDeviceQueue(device,queue_family_index_compute,0,&queue_compute);
        get_physical_device_properties(api_version);
        // vkGetPhysicalDeviceProperties(physical_device,&physical_device_properties);
        // vkGetPhysicalDeviceMemoryProperties(physical_device,&physical_device_memory_properties);
        // 输出所用的物理设备名称
        auto renderer_name = physical_device_properties.properties.deviceName;
        outstream << std::format("Renderer: {}\n", renderer_name);
        for (auto& i : callbacks_create_device)
            i();
        return VK_SUCCESS;
    }

    //以下函数用于创建逻辑设备失败后
    void query_physical_device_features(uint32_t api_version) {
        get_physical_device_features(api_version);
    }

    [[nodiscard]] bool has_device_extension(const char* extension_name) const {
        std::vector<const char*> extensions{extension_name};
        if (check_device_extensions(extensions) != VK_SUCCESS) {
            return false;
        }
        return extensions.front() != nullptr;
    }

    result_t check_device_extensions(std::span<const char*> extensions_to_check, const char* layer_name = nullptr) const {
        uint32_t extension_count;
        std::vector<VkExtensionProperties> available_extensions;
        if (result_t result = vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &extension_count, nullptr)) {
            layer_name ?
                outstream << std::format("[ VulkanDevice ] ERROR\nFailed to get the count of device extensions!\nLayer name:{}\n", layer_name) :
                outstream << std::format("[ VulkanDevice ] ERROR\nFailed to get the count of device extensions!\n");

            return result;
        }
        if (extension_count) {
            available_extensions.resize(extension_count);
            if (result_t result = vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &extension_count, available_extensions.data())) {
                outstream << std::format("[ VulkanDevice ] ERROR\nFailed to enumerate device extension properties!\nError code: {}\n", int32_t(result));
                return result;
            }
            for (auto& i : extensions_to_check) {
                bool found = false;
                for (auto& j : available_extensions)
                    if (!strcmp(i, j.extensionName)) {
                        found = true;
                        break;
                    }
                if (!found)
                    i = nullptr;//If a required extension isn't available, set it to nullptr
            }
        }
        else
            for (auto& i : extensions_to_check)
                i = nullptr;
        return VK_SUCCESS;
    }

    void add_callback_configure_device(std::function<void()> function) {
        callbacks_configure_device.push_back(function);
    }

    void add_callback_create_device(std::function<void()> function) {
        callbacks_create_device.push_back(function);
    }

    void add_callback_destory_device(std::function<void()> function) {
        callbacks_destroy_device.push_back(function);
    }


private:
    VkFormatProperties format_properties[std::size(format_infos_v1_0)] = {};
    VkPhysicalDevice physical_device{};
    // VkPhysicalDeviceProperties physical_device_properties{};
    // VkPhysicalDeviceMemoryProperties physical_device_memory_properties{};
    std::vector<VkPhysicalDevice> available_physical_devices;

    // 物理设备特性
    VkPhysicalDeviceFeatures2 physical_device_features;
    VkPhysicalDeviceVulkan11Features physical_device_features_vulkan11;
    VkPhysicalDeviceVulkan12Features physical_device_features_vulkan12;
    VkPhysicalDeviceVulkan13Features physical_device_features_vulkan13;

    // 物理设备属性
    VkPhysicalDeviceProperties2 physical_device_properties;
    VkPhysicalDeviceVulkan11Properties physical_device_properties_vulkan11; //Provided by VK_API_VERSION_1_2
    VkPhysicalDeviceVulkan12Properties physical_device_properties_vulkan12;
    VkPhysicalDeviceVulkan13Properties physical_device_properties_vulkan13;

    VkPhysicalDeviceMemoryProperties2 physical_device_memory_properties;

    uint32_t queue_family_index_graphics = VK_QUEUE_FAMILY_IGNORED;
    uint32_t queue_family_index_presentation = VK_QUEUE_FAMILY_IGNORED;
    uint32_t queue_family_index_compute = VK_QUEUE_FAMILY_IGNORED;
    VkQueue queue_graphics;
    VkQueue queue_presentation;
    VkQueue queue_compute;

    VkDevice device;
    std::vector<const char*> device_extensions;

    std::vector<std::function<void()>> callbacks_configure_device;
    std::vector<std::function<void()>> callbacks_create_device;
    std::vector<std::function<void()>> callbacks_destroy_device;

    void *pnext_device_create_info;
    void *pnext_physical_device_features;
    void *pnext_physical_device_properties;
    void *pnext_physical_device_memory_properties;

    void print_renderer_name() {
        // 输出所用的物理设备名称
        auto renderer_name = physical_device_properties.properties.deviceName;
        outstream << std::format("Renderer: {}\n", renderer_name);
    }

    void get_physical_device_features(uint32_t api_version) {
        if (api_version >= VK_API_VERSION_1_1) {
            physical_device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            physical_device_features_vulkan11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
            physical_device_features_vulkan12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            physical_device_features_vulkan13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            if (api_version >= VK_API_VERSION_1_2) {
                physical_device_features.pNext = &physical_device_features_vulkan11;
                physical_device_features_vulkan11.pNext = &physical_device_features_vulkan12;
                if (api_version >= VK_API_VERSION_1_3) {
                    physical_device_features_vulkan12.pNext = &physical_device_features_vulkan13;
                }
            }
            set_pnext(physical_device_features.pNext,pnext_physical_device_features);
            vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_features);
        }
        else
            vkGetPhysicalDeviceFeatures(physical_device, &physical_device_features.features);
    }

    void get_physical_device_properties(uint32_t api_version) {
        if (api_version >= VK_API_VERSION_1_1) {
            physical_device_properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
            physical_device_properties_vulkan11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES };
            physical_device_properties_vulkan12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
            physical_device_properties_vulkan13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
            if (api_version >= VK_API_VERSION_1_2) {
                physical_device_properties.pNext = &physical_device_properties_vulkan11;
                physical_device_properties_vulkan11.pNext = &physical_device_properties_vulkan12;
                if (api_version >= VK_API_VERSION_1_3) {
                    physical_device_properties_vulkan12.pNext = &physical_device_properties_vulkan13;
                }
            }
            set_pnext(physical_device_properties.pNext,pnext_physical_device_properties);
            vkGetPhysicalDeviceProperties2(physical_device, &physical_device_properties);
            physical_device_memory_properties = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
                pnext_physical_device_properties
            };
            vkGetPhysicalDeviceMemoryProperties2(physical_device, &physical_device_memory_properties);
        }
        else {
            vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties.properties);
            vkGetPhysicalDeviceMemoryProperties(physical_device, &physical_device_memory_properties.memoryProperties);
        }
    }


};
