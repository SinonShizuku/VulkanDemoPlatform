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
        return initialize_imgui();
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

    bool switch_to_demo(std::unique_ptr<DemoBase> new_demo) {
        // 等待GPU完成当前操作
        if (current_demo) {
            // SharedResourceManager::get_singleton().get_shared_fence().wait_and_reset();
            current_demo->cleanup_scene_resources();
        }

        if (current_demo = std::move(new_demo)) {
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

            current_demo->render_frame();

            VulkanCommand::get_singleton().submit_command_buffer_graphics(
                current_demo->get_command_buffer(),
                shared_resources.get_semaphore_image_is_available(),
                shared_resources.get_semaphore_rendering_is_over(),
                shared_resources.get_shared_fence()
            );

            VulkanCommand::get_singleton().present_image(
                shared_resources.get_semaphore_rendering_is_over()
            );

            glfwPollEvents();
            update_fps_title(frame_timer);

            shared_resources.get_shared_fence().wait_and_reset();
        }
    }

private:
    GLFWwindow* window = nullptr;
    std::unique_ptr<DemoBase> current_demo;
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