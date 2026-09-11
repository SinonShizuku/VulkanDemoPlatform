#include "VulkanAppLauncher.h"
#include "../Demos/VulkanTests/BuffersAndPictureTest.h"
#include "../Demos/DemoManager.h"

VulkanAppLauncher& VulkanAppLauncher::getSingleton(VkExtent2D size, bool fullScreen, bool isResizable, bool limitFrameRate) {
    static VulkanAppLauncher singletonInstance(size, fullScreen, isResizable, limitFrameRate);
    return singletonInstance;
}

VulkanAppLauncher::VulkanAppLauncher(VkExtent2D size, bool fullScreen, bool isResizable, bool limitFrameRate) :
    window_width(size.width),
    window_height(size.height),
    is_fullscreen(fullScreen),
    is_resizeable(isResizable),
    limit_framerate(limitFrameRate)
{}

void VulkanAppLauncher::run() {
    VulkanCommand::get_singleton();

    if (!init_window()) {
        outstream << std::format("[ InitializeWindow ] ERROR\nFailed to initialize window!\n");
        return;
    }
    if (!init_vulkan()) {
        outstream << std::format("[ InitializeWindow ] ERROR\nFailed to initialize window!\n");
        return;
    }

    // VulkanExecutionManager::get_singleton().boot_screen("../Assets/img.png", VK_FORMAT_R8G8B8A8_UNORM);
    // std::this_thread::sleep_for(std::chrono::seconds(1LL));

    main_loop();
    terminate_window();
}


bool VulkanAppLauncher::init_vulkan() {
    
    uint32_t extension_count = 0;
    const char** extension_names = glfwGetRequiredInstanceExtensions(&extension_count);
    if (!extension_names) {
        outstream << std::format("[ InitializeVulkan ] ERROR\nFailed to get required extensions, Vulkan is not available on this machine!\n");
        glfwTerminate();
        return false;
    }
    // 获取拓展
    for (size_t i = 0; i < extension_count; i++) {
        VulkanCore::get_singleton().get_vulkan_instance().add_instance_extension(extension_names[i]);
    }
    VulkanCore::get_singleton().get_vulkan_device().add_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // 获取Vulkan版本
    auto& instance = VulkanCore::get_singleton().get_vulkan_instance();
    if (instance.use_latest_api_version()) {
        outstream << std::format("[ VulkanAppLauncher ] ERROR\nFailed to query the Vulkan loader version!\n");
        return false;
    }

    // 创建Vulkan实例
    if (instance.create_instance())
        return false;

    // 配置surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    if (result_t result = glfwCreateWindowSurface(instance.get_instance(),window,nullptr,&surface)) {
        outstream << std::format("[ InitializeWindow ] ERROR\nFailed to create a window surface!\nError code: {}\n", int32_t(result));
        glfwTerminate();
        return false;
    }
    instance.set_surface(surface);

    // 选择物理设备。扩展和 feature 必须在 vkCreateDevice 之前启用。
    auto& device = VulkanCore::get_singleton().get_vulkan_device();
    if (VulkanCore::get_singleton().acquire_physical_devices() ||
        VulkanCore::get_singleton().determine_physical_device(0,true,true))
        return false;

    const uint32_t device_api_version = std::min(instance.get_api_version(), device.get_physical_device_api_version());
    if (device_api_version < VK_API_VERSION_1_1) {
        outstream << std::format("[ VulkanAppLauncher ] ERROR\nA Vulkan 1.1 or newer device is required!\n");
        return false;
    }

    VkPhysicalDeviceImagelessFramebufferFeatures imageless_framebuffer_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES,
    };
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
    };

    if (device_api_version < VK_API_VERSION_1_2) {
        if (!device.has_device_extension(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME) ||
            !device.has_device_extension(VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME)) {
            outstream << std::format("[ VulkanAppLauncher ] ERROR\nImageless framebuffer extensions are unavailable!\n");
            return false;
        }
        device.add_device_extension(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
        device.add_device_extension(VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME);
        device.add_next_structure_physical_device_features(imageless_framebuffer_features);
    }

    if (device_api_version < VK_API_VERSION_1_3) {
        if (!device.has_device_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
            outstream << std::format("[ VulkanAppLauncher ] ERROR\nVK_KHR_dynamic_rendering is unavailable!\n");
            return false;
        }
        device.add_device_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        device.add_next_structure_physical_device_features(dynamic_rendering_features);
    }

    device.query_physical_device_features(device_api_version);

    const bool has_imageless_framebuffer = device_api_version >= VK_API_VERSION_1_2
        ? device.get_physical_device_vulkan12_features().imagelessFramebuffer
        : imageless_framebuffer_features.imagelessFramebuffer;
    const bool has_dynamic_rendering = device_api_version >= VK_API_VERSION_1_3
        ? device.get_physical_device_vulkan13_features().dynamicRendering
        : dynamic_rendering_features.dynamicRendering;

    if (!has_imageless_framebuffer || !has_dynamic_rendering) {
        outstream << std::format(
            "[ VulkanAppLauncher ] ERROR\nRequired device features are unavailable!\n"
            "imagelessFramebuffer={}, dynamicRendering={}\n",
            has_imageless_framebuffer,
            has_dynamic_rendering);
        return false;
    }

    const bool has_sampler_anisotropy = device.get_physical_device_features().features.samplerAnisotropy;
    device.add_callback_configure_device([&device, device_api_version, has_sampler_anisotropy, &imageless_framebuffer_features, &dynamic_rendering_features] {
        device.get_physical_device_features().features.samplerAnisotropy = has_sampler_anisotropy;

        if (device_api_version >= VK_API_VERSION_1_2)
            device.get_physical_device_vulkan12_features().imagelessFramebuffer = VK_TRUE;
        else
            imageless_framebuffer_features.imagelessFramebuffer = VK_TRUE;

        if (device_api_version >= VK_API_VERSION_1_3)
            device.get_physical_device_vulkan13_features().dynamicRendering = VK_TRUE;
        else
            dynamic_rendering_features.dynamicRendering = VK_TRUE;
    });

    outstream << std::format(
        "Device API Version: {}.{}.{}\n"
        "Device features: samplerAnisotropy={}, imagelessFramebuffer={}, dynamicRendering={}\n",
        VK_VERSION_MAJOR(device_api_version),
        VK_VERSION_MINOR(device_api_version),
        VK_VERSION_PATCH(device_api_version),
        has_sampler_anisotropy,
        has_imageless_framebuffer,
        has_dynamic_rendering);

    if (device.create_device(device_api_version))
        return false;

    // 创建交换链
    if (VulkanSwapchainManager::get_singleton().create_swapchain())
        return false;

    return true;
}

bool VulkanAppLauncher::init_window() {
    if (!glfwInit()) {
        outstream << std::format("[ InitializeWindow ] ERROR\nFailed to initialize GLFW!\n");
        return false;
    }
    // Vulkan格式
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, is_resizeable);

    // 配置显示器及窗口尺寸信息
    monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    window = is_fullscreen ?
        glfwCreateWindow(mode->width, mode->height,window_title,monitor,nullptr) :
        glfwCreateWindow(window_width, window_height,window_title,nullptr,nullptr);
    if (!window) {
        outstream << std::format("[ InitializeWindow ]\nFailed to create a glfw window!\n");
        glfwTerminate();
        return false;
    }

    return true;
}

void VulkanAppLauncher::main_loop() {
    // 初始化场景管理系统
    if (!DemoManager::get_singleton().initialize(window)) {
        outstream << std::format("[ MainLoop ]\nFailed to initialize demo manager!\n");
        return;
    }

    // 创建并切换到默认场景
    auto default_demo = std::make_unique<BuffersAndPictureTest>();
    if (!DemoManager::get_singleton().switch_to_demo(std::move(default_demo))) {
        outstream << std::format("[ MainLoop ]\nFailed to switch to default demo!\n");
        return;
    }

    // 运行主循环
    DemoManager::get_singleton().run_main_loop();
}

void VulkanAppLauncher::cleanup() {
    VulkanPipelineManager::get_singleton().clear_all_rpwf();
    VulkanSwapchainManager::get_singleton().destroy_singleton();
    VulkanCore::get_singleton().destroy_singleton();
}

void VulkanAppLauncher::terminate_window() {
    cleanup();
    glfwTerminate();
}

void VulkanAppLauncher::create_pipeline_layout() {
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
    pipeline_layout_triangle.create(pipeline_layout_create_info);
}

void VulkanAppLauncher::create_pipeline_layout_with_push_constant() {
    VkPushConstantRange push_constant_range = {
        VK_SHADER_STAGE_VERTEX_BIT,
        0,//offset
        24//范围大小，3个vec2是24
    };
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };
    pipeline_layout_triangle.create(pipeline_layout_create_info);
}

void VulkanAppLauncher::create_pipeline_layout_with_uniform_buffer() {
    VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
    };
    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
        .bindingCount = 1,
        .pBindings = &descriptor_set_layout_binding
    };
    descriptor_set_layout_triangle.create(descriptor_set_layout_create_info);
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .setLayoutCount = 1,
        .pSetLayouts = descriptor_set_layout_triangle.Address()
    };
    pipeline_layout_triangle.create(pipeline_layout_create_info);
}

void VulkanAppLauncher::create_pipeline_layout_with_texture() {
    VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
        .bindingCount = 1,
        .pBindings = &descriptor_set_layout_binding
    };
    descriptor_set_layout_texture.create(descriptor_set_layout_create_info);
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .setLayoutCount = 1,
        .pSetLayouts = descriptor_set_layout_texture.Address()
    };
    pipeline_layout_texture.create(pipeline_layout_create_info);
}

void VulkanAppLauncher::create_pipeline() {
    static VulkanShaderModule vert("Shader/Texture.vert.spv");
    static VulkanShaderModule frag("Shader/Texture.frag.spv");
    // static VkPipelineShaderStageCreateInfo shader_stage_create_infos_triangle[2] = {
    //     vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
    //     frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
    // };
    static VkPipelineShaderStageCreateInfo shader_stage_create_infos_texture[2] = {
        vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
        frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    auto create = [&] {
        GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
        pipeline_create_info_pack.create_info.layout = pipeline_layout_texture;
        pipeline_create_info_pack.create_info.renderPass = render_pass_and_frame_buffers().render_pass;
        // 子通道只有一个，pipeline_create_info_pack.createInfo.renderPass使用默认值0

        // vertex buffer
        //数据来自0号顶点缓冲区，输入频率是逐顶点输入
        pipeline_create_info_pack.vertex_input_bindings.emplace_back(0, sizeof(texture_vertex), VK_VERTEX_INPUT_RATE_VERTEX);
        // //location为0，数据来自0号顶点缓冲区，vec2对应VK_FORMAT_R32G32_SFLOAT，用offsetof计算position在vertex中的起始位置
        // pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position));
        // //location为1，数据来自0号顶点缓冲区，vec4对应VK_FORMAT_R32G32B32A32_SFLOAT，用offsetof计算color在vertex中的起始位置
        // pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, color));
        pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(texture_vertex, position));
        pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(texture_vertex, texCoord));

        // pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        pipeline_create_info_pack.viewports.emplace_back(0.f, 0.f, float(window_width), float(window_height), 0.f, 1.f);
        pipeline_create_info_pack.scissors.emplace_back(VkOffset2D{},window_size);
        pipeline_create_info_pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pipeline_create_info_pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
        pipeline_create_info_pack.update_all_arrays();
        pipeline_create_info_pack.create_info.stageCount = 2;
        pipeline_create_info_pack.create_info.pStages = shader_stage_create_infos_texture;

        // pipeline_triangle.create(pipeline_create_info_pack);
        pipeline_texture.create(pipeline_create_info_pack);
    };
    auto destroy = [this] {
        pipeline_triangle.~VulkanPipeline();
    };
    VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(create);
    VulkanSwapchainManager::get_singleton().add_callback_destroy_swapchain(destroy);
    create();
}
