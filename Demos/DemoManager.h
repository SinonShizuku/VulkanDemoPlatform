#pragma once
#include <unordered_map>
#include <functional>
#include <memory>

#include "../UI/ImGuiManager.h"
#include "DemoCategories.h"
#include "SharedResourceManager.h"
#include "DemoBase.h"

// demos
#include "VulkanTests/BuffersAndPictureTest.h"
#include "VulkanTests/ImagelessFramebufferTest.h"
#include "VulkanTests/DynamicRenderingTest.h"
#include "VulkanTests/OffScreenRenderingTest.h"
#include "VulkanTests/FrameGraphOffScreenTest.h"
#include "VulkanTests/DepthAttachmentTest.h"
#include "VulkanTests/DeferredRenderingTest.h"
#include "BasicRendering/glTFLoading.h"
#include "BasicRendering/ShadowMapping.h"

class DemoManager {
public:
    static DemoManager& get_singleton() {
        static DemoManager singleton = DemoManager();
        return singleton;
    }

    void initialize_demos() {
        implemented_demos["BuffersAndPictureTest"] = []() {
            return std::make_unique<BuffersAndPictureTest>();;
        };

        implemented_demos["ImagelessFramebufferTest"] = []() {
            return std::make_unique<ImagelessFramebufferTest>();
        };

        implemented_demos["DynamicRenderingTest"] = []() {
            return std::make_unique<DynamicRenderingTest>();;
        };

        // implemented_demos["OffScreenRenderingTest"] = [this]() {
        //     auto demo = std::make_unique<OffScreenRenderingTest>();
        //     demo->set_window(window);
        //     return demo;
        // };

        implemented_demos["FrameGraphOffScreenTest"] = [this]() {
            auto demo = std::make_unique<FrameGraphOffScreenTest>();
            demo->set_window(window);
            return demo;
        };

        implemented_demos["DepthAttachmentTest"] = []() {
            return std::make_unique<DepthAttachmentTest>();;
        };

        implemented_demos["DeferredRenderingTest"] = []() {
            return std::make_unique<DeferredRenderingTest>();;
        };

        implemented_demos["Loading & Rendering glTF Model"] = [this]() {
            return std::make_unique<glTFLoading>(window);
        };

        implemented_demos["ShadowMapping"] = [this]() {
            return std::make_unique<ShadowMapping>(window);
        };

    }

    bool initialize(GLFWwindow* window) {
        this->window = window;
        if (!SharedResourceManager::get_singleton().initialize(window)) {
            return false;
        }
        initialize_demos();
        imgui_initialized = initialize_imgui();
        return imgui_initialized;
    }

    // 退出前的显式释放，必须在销毁 VkDevice 之前调用：
    // 1. 当前 demo：它的 pipeline / shader module / descriptor set / command buffer 都在析构里调用 vkDestroy*；
    // 2. ImGui：pipeline 由 ImGui_ImplVulkan_Shutdown 释放，且它引用了共享资源的 descriptor pool
    //    与 rpwf_imgui 的 render pass。
    void shutdown() {
        if (current_demo) {
            current_demo->cleanup_scene_resources();
            current_demo.reset();
        }
        if (imgui_initialized) {
            shutdown_imgui();
            imgui_initialized = false;
        }
    }


    void show_shared_ui_components(bool &show_demo_window) {
        ImGui::ShowDemoWindow(&show_demo_window);
        if (ImGui::BeginMainMenuBar()) {
            // 文件菜单
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    glfwSetWindowShouldClose(window, true);
                }
                ImGui::EndMenu();
            }

            for (auto category: demos) {
                auto category_name = category.first;
                if (ImGui::BeginMenu(category_name.c_str())) {
                    for (auto type : category.second) {
                        if (ImGui::MenuItem(type.c_str())) {
                            auto it = implemented_demos.find(type);
                            if (it != implemented_demos.end()) {
                                std::unique_ptr<DemoBase> demo = it->second();
                                if (request_demo_switch(std::move(demo))) current_demo_name = type;
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            }

            // 显示当前demo信息
            if (current_demo) {
                std::string demo_info = "Current Demo: " + current_demo->get_type();
                ImVec2 textSize = ImGui::CalcTextSize(demo_info.c_str());
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textSize.x);
                ImGui::Text("%s", demo_info.c_str());
            }

            ImGui::EndMainMenuBar();
        }
    }

    bool request_demo_switch(std::unique_ptr<DemoBase> new_demo) {
        bool show_popup = true;
        if (new_demo->get_type()=="DynamicRenderingTest" && VulkanCore::get_singleton().get_vulkan_instance().get_api_version() < VK_API_VERSION_1_2) {
            if (show_popup) {
                ImGui::OpenPopup("Warning");
            }
            if (ImGui::BeginPopupModal("Warning", &show_popup)) {
                ImGui::Text("Vulkan api version < 1.3, cannot activate dynamic rendering!");
                if (ImGui::Button("Back")) {
                    show_popup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            return false;
        }
        // VulkanPipelineManager::get_singleton().clear_all_rpwf();
        // SharedResourceManager::get_singleton().initialize_rpwf();
        pending_demo_switch = true;
        new_demo_request = std::move(new_demo);
        return true;
    }

    // 供 --demo 使用：先按菜单名找，再按 demo 的 type 找（两者不同，例如 glTFLoading）。
    std::unique_ptr<DemoBase> create_demo_by_name(const std::string& name) {
        if (name.empty())
            return nullptr;
        if (auto it = implemented_demos.find(name); it != implemented_demos.end())
            return it->second();
        for (auto& entry : implemented_demos) {
            auto candidate = entry.second();
            if (candidate && candidate->get_type() == name)
                return candidate;
        }
        outstream << "[ DemoManager ] 未找到 demo " << name << "（--demo 支持菜单名或 demo 类型名）\n";
        return nullptr;
    }

    bool switch_to_demo(std::unique_ptr<DemoBase> new_demo) {
        if (!new_demo)
            return false;
        // 默认 demo 与部分工厂创建的 demo 没有传入窗口，这里统一补上（DemoBase::window 默认 nullptr）。
        new_demo->set_window(window);
        // 等待GPU完成当前操作
        if (current_demo) {
            // SharedResourceManager::get_singleton().get_shared_fence().wait_and_reset();
            current_demo->cleanup_scene_resources();
        }

        if (current_demo = std::move(new_demo)) {
            // 各 demo 的 pipeline 回调用 current_demo_name 判断「现在是不是当前 demo」。之前只有走菜单
            // 切换才会更新它（默认 demo 与工厂路径都不更新），于是以名字做闸门的 demo（如 glTFLoading）
            // 在非菜单路径下初始化失败。这里统一按 demo 类型同步。
            current_demo_name = current_demo->get_type();
            return current_demo->initialize_scene_resources();
        }

        return true;
    }

    void run_main_loop() {
        if (!current_demo) {
            outstream << std::format("[ DemoManager ] ERROR\nNo demo selected!\n");
            return;
        }

        auto& shared_resources = SharedResourceManager::get_singleton();
        bool show_demo_window = true;

        // benchmark：去掉 vsync（否则测到的是刷新率上限），并准备 timestamp recorder。
        BenchmarkRecorder benchmark;
        const bool benchmarking = benchmark_frames > 0;
        if (benchmarking) {
            glfwSwapInterval(0);
            benchmark.initialize(VulkanCore::get_singleton().get_vulkan_device().get_physical_device());
        }
        uint32_t benchmark_frame_index = 0;

        double last_frame_time = glfwGetTime();
        while (!glfwWindowShouldClose(window)) {
            while (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
                glfwWaitEvents();

            double current_time = glfwGetTime();
            auto frame_timer = (float)(current_time - last_frame_time);
            last_frame_time = current_time;
                
            // 显示共享UI组件（菜单栏等）
            show_demo_window = current_demo->get_show_demo_window();
            ImGuiManager::get_singleton().imgui_new_frame(show_demo_window);
            show_shared_ui_components(show_demo_window);
            // 显示当前demo的UI组件
            current_demo->show_demo_basic_info();
            current_demo->draw_custom_ui();

            // 检查是否有demo切换请求
            if (pending_demo_switch) {
                switch_to_demo(std::move(new_demo_request));
                pending_demo_switch = false;
            }

            current_demo->update(frame_timer);

            VulkanSwapchainManager::get_singleton().swap_image(
                shared_resources.get_semaphore_image_is_available()
            );

            active_frame_benchmark = benchmarking ? &benchmark : nullptr;
            current_demo->render_frame();
            active_frame_benchmark = nullptr;

            // render-finished semaphore 必须按 swapchain image 区分，否则会与 presentation
            // engine 仍在使用的信号量冲突（VUID-vkQueueSubmit-pSignalSemaphores-00067）。
            const uint32_t acquired_image = VulkanSwapchainManager::get_singleton().get_current_image_index();
            const VkSemaphore render_finished =
                VulkanSwapchainManager::get_singleton().get_render_finished_semaphore(acquired_image);

            VulkanCommand::get_singleton().submit_command_buffer_graphics(
                current_demo->get_command_buffer(),
                shared_resources.get_semaphore_image_is_available(),
                render_finished,
                shared_resources.get_shared_fence()
            );

            VulkanCommand::get_singleton().present_image(render_finished);

            glfwPollEvents();
            update_fps_title(frame_timer);

            shared_resources.get_shared_fence().wait_and_reset();

            if (benchmarking) {
                const double gpu_ms = benchmark.resolve_and_record();
                if (benchmark_frame_index >= static_cast<uint32_t>(benchmark_warmup))
                    benchmark.add_sample(frame_timer * 1000.0, gpu_ms);
                ++benchmark_frame_index;
                if (benchmark_frame_index >= static_cast<uint32_t>(benchmark_warmup + benchmark_frames)) {
                    VkPhysicalDeviceProperties properties{};
                    vkGetPhysicalDeviceProperties(VulkanCore::get_singleton().get_vulkan_device().get_physical_device(), &properties);
                    const std::string base = benchmark_csv.empty() ? std::string("out/benchmark/latest") : benchmark_csv;
                    if (!std::filesystem::path(base).parent_path().empty())
                        std::filesystem::create_directories(std::filesystem::path(base).parent_path());
                    const auto& extent = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageExtent;
                    std::vector<std::string> metadata = {
                        std::string("demo,") + current_demo->get_type(),
                        std::string("scene,") + (command_line_scene.empty() ? std::string("(default)") : command_line_scene),
                        std::string("resolution,") + std::to_string(extent.width) + "x" + std::to_string(extent.height),
                        std::string("gpu,") + properties.deviceName,
                        std::string("driver,") + std::format("{}.{}.{}", VK_VERSION_MAJOR(properties.driverVersion), VK_VERSION_MINOR(properties.driverVersion), VK_VERSION_PATCH(properties.driverVersion)),
                        std::string("vulkan_device,") + std::format("{}.{}.{}", VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion)),
                        [] {
                            std::string state = "unlocked";
                            std::ifstream clock_state("out/benchmark/gpu-clock-state.txt");
                            if (clock_state)
                                std::getline(clock_state, state);
                            return std::string("gpu_clock,") + state;
                        }(),
                        std::string("vsync,0"),
                        std::string("warmup,") + std::to_string(benchmark_warmup),
                    };
                    benchmark.write_reports(base, metadata);
                    benchmark.shutdown();
                    glfwSetWindowShouldClose(window, true);
                }
            }
        }
    }

private:
    GLFWwindow* window = nullptr;
    std::unique_ptr<DemoBase> current_demo;
    bool imgui_initialized = false;
    std::unordered_map<DemoType, std::function<std::unique_ptr<DemoBase>()>> implemented_demos;

    bool pending_demo_switch = false;
    std::unique_ptr<DemoBase> new_demo_request;


    // 辅助函数：检查demo是否已实现
    bool is_demo_implemented(DemoType demo_type) {
        return implemented_demos.find(demo_type) != implemented_demos.end();
    }

    bool initialize_imgui() {
        // 初始化ImGui
        ImGuiManager::get_singleton().init_basic_config();

        ImGui_ImplGlfw_InitForVulkan(window, true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = VulkanCore::get_singleton().get_vulkan_instance().get_instance();
        init_info.PhysicalDevice = VulkanCore::get_singleton().get_vulkan_device().get_physical_device();
        init_info.Device = VulkanCore::get_singleton().get_vulkan_device().get_device();
        init_info.QueueFamily = VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_graphics();
        init_info.Queue = VulkanCore::get_singleton().get_vulkan_device().get_queue_graphics();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = SharedResourceManager::get_singleton().get_imgui_descriptor_pool();
        init_info.RenderPass = SharedResourceManager::get_singleton().get_render_pass_imgui();
        init_info.Subpass = 0;
        init_info.MinImageCount = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().minImageCount;
        init_info.ImageCount = VulkanSwapchainManager::get_singleton().get_swapchain_image_count();
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;

        return ImGui_ImplVulkan_Init(&init_info);
    }

    void shutdown_imgui() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void update_fps_title(float frame_timer) {
        // 这些 static 变量现在只用于 FPS 计数
        static double time_accumulator = 0.0;
        static int frame_count = 0;
        static std::stringstream info;

        time_accumulator += frame_timer;
        frame_count++;

        if (time_accumulator >= 1.0) { // 每秒更新一次
            info.precision(1);
            info << "Vulkan Renderer - " << (current_demo ? current_demo->get_type() : "未选择场景")
                 << "    " << std::fixed << (double)frame_count / time_accumulator << " FPS";
            glfwSetWindowTitle(window, info.str().c_str());

            info.str("");
            time_accumulator = 0.0; // 重置
            frame_count = 0;
        }
    }
};