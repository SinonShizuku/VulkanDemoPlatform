#pragma once
#include "../Start.h"
#include "VulkanSwapchainManager.h"
#include "components/VulkanRenderPassWithFramebuffers.h"

inline const VkExtent2D& window_size = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageExtent;

class VulkanPipelineManager {
public:
    VulkanPipelineManager(){};
    ~VulkanPipelineManager(){};

    static VulkanPipelineManager& get_singleton() {
        static VulkanPipelineManager singleton = VulkanPipelineManager();
        return singleton;
    }

    const auto& get_rpwf_imgui() {
        return rpwf_imgui;
    }

    const auto& get_rpwf_screen() {
        return rpwf;
    }

    const auto& get_rpwf_screen_imageless_framebuffer() {
        return rpwf_imageless;
    }

    const auto& get_rpwf_offscreen() {
        return rpwf_offscreen;
    }

    const auto& get_ca_canvas() {
        return ca_canvas;
    }

    const auto& get_ca_offscreen_vsm() {
        return ca_offscreen_vsm;
    }

    const auto& get_rpwf_ds() {
        return rpwf_ds;
    }

    const auto& get_rpwf_deferred_to_screen() {
        return rpwf_deferred_to_screen;
    }

    const auto& get_rpwf_offscreen_ds() {
        return rpwf_offscreen_ds;
    }

    [[nodiscard]] const VulkanColorAttachment & get_ca_deferred_to_screen_normal_z() const {
        return ca_deferred_to_screen_normalZ;
    }

    [[nodiscard]] const VulkanColorAttachment & get_ca_deferred_to_screen_albedo_specular() const {
        return ca_deferred_to_screen_albedo_specular;
    }

    [[nodiscard]] const VulkanDepthStencilAttachment & get_dsa_deferred_to_screen() const {
        return dsa_deferred_to_screen;
    }

    [[nodiscard]] const VulkanDepthStencilAttachment & get_dsa_offscreen() const {
        return dsa_offscreen;
    }

    [[nodiscard]] VkExtent2D get_shadow_map_size() const {
        return shadow_map_size;
    }

    const auto& create_rpwf_screen() {
        VkAttachmentDescription attachment_description = {
            .format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        };
        VkAttachmentReference attachment_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass_description = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment_reference
        };
        VkSubpassDependency subpass_dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
        };
        VkRenderPassCreateInfo render_pass_create_info = {
            .attachmentCount = 1,
            .pAttachments = &attachment_description,
            .subpassCount = 1,
            .pSubpasses = &subpass_description,
            .dependencyCount = 1,
            .pDependencies = &subpass_dependency
        };
        rpwf.render_pass.create(render_pass_create_info);

        auto create_frame_buffers = [] {
            size_t image_count = VulkanSwapchainManager::get_singleton().get_swapchain_image_count();
            rpwf.framebuffers.resize(image_count);
            VkFramebufferCreateInfo framebuffer_create_info = {
                .renderPass = rpwf.render_pass,
                .attachmentCount = 1,
                .width = window_size.width,
                .height = window_size.height,
                .layers = 1
            };
            for (auto i = 0; i < image_count; i++) {
                VkImageView attachment = VulkanSwapchainManager::get_singleton().get_swapchain_image_views()[i];
                framebuffer_create_info.pAttachments = &attachment;
                rpwf.framebuffers[i].create(framebuffer_create_info);
            }
        };

        auto destroy_framebuffers = [] {
            for (auto &framebuffer : rpwf.framebuffers) {
                framebuffer.clear();
            }
            rpwf.framebuffers.clear();
        };
        create_frame_buffers();

        ExecuteOnce(rpwf);

        VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(create_frame_buffers);
        VulkanSwapchainManager::get_singleton().add_callback_destroy_swapchain(destroy_framebuffers);
        return rpwf;
    }

    void clear_rpwf_screen() {
        rpwf.render_pass.clear();
        for (auto &framebuffer : rpwf.framebuffers) {
            framebuffer.clear();
        }
        rpwf.framebuffers.clear();
    }

    const auto& create_rpwf_imgui() {
        VkAttachmentDescription attachment_description = {
            .format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        };
        VkAttachmentReference attachment_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass_description = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment_reference
        };
        VkSubpassDependency subpass_dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
        };
        VkRenderPassCreateInfo render_pass_create_info = {
            .attachmentCount = 1,
            .pAttachments = &attachment_description,
            .subpassCount = 1,
            .pSubpasses = &subpass_description,
            .dependencyCount = 1,
            .pDependencies = &subpass_dependency
        };
        rpwf_imgui.render_pass.create(render_pass_create_info);

        auto create_frame_buffers = [] {
            size_t image_count = VulkanSwapchainManager::get_singleton().get_swapchain_image_count();
            rpwf_imgui.framebuffers.resize(image_count);
            VkFramebufferCreateInfo framebuffer_create_info = {
                .renderPass = rpwf_imgui.render_pass,
                .attachmentCount = 1,
                .width = window_size.width,
                .height = window_size.height,
                .layers = 1
            };
            for (auto i = 0; i < image_count; i++) {
                VkImageView attachment = VulkanSwapchainManager::get_singleton().get_swapchain_image_views()[i];
                framebuffer_create_info.pAttachments = &attachment;
                rpwf_imgui.framebuffers[i].create(framebuffer_create_info);
            }
        };

        auto destroy_framebuffers = [] {
            for (auto &framebuffer : rpwf_imgui.framebuffers) {
                framebuffer.clear();
            }
            rpwf_imgui.framebuffers.clear();
        };
        create_frame_buffers();

        ExecuteOnce(rpwf_imgui);

        VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(create_frame_buffers);
        VulkanSwapchainManager::get_singleton().add_callback_destroy_swapchain(destroy_framebuffers);
        return rpwf_imgui;
    }

    void clear_rpwf_imgui() {
        rpwf_imgui.render_pass.clear();
        for (auto &framebuffer : rpwf_imgui.framebuffers) {
            framebuffer.clear();
        }
        rpwf_imgui.framebuffers.clear();
    }

    const auto& create_rpwf_screen_imageless_framebuffer() {
        VkAttachmentDescription attachment_description = {
            .format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
         };
        VkAttachmentReference attachment_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass_description = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment_reference
        };
        VkSubpassDependency subpass_dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
        };
        VkRenderPassCreateInfo render_pass_create_info = {
            .attachmentCount = 1,
            .pAttachments = &attachment_description,
            .subpassCount = 1,
            .pSubpasses = &subpass_description,
            .dependencyCount = 1,
            .pDependencies = &subpass_dependency
        };
        rpwf_imageless.render_pass.create(render_pass_create_info);

        // 待填充
        auto create_framebuffer = [] {
            VkFramebufferAttachmentImageInfo framebuffer_attachment_image_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
                .usage = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageUsage,
                .width = window_size.width,
                .height = window_size.height,
                .layerCount = 1,
                .viewFormatCount = 1,
                .pViewFormats = &VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat
            };
            if (VulkanSwapchainManager::get_singleton().get_swapchain_create_info().flags & VK_SWAPCHAIN_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT_KHR)
                framebuffer_attachment_image_info.flags |= VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT;
            if (VulkanSwapchainManager::get_singleton().get_swapchain_create_info().flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)
                framebuffer_attachment_image_info.flags |= VK_IMAGE_CREATE_PROTECTED_BIT;
            if (VulkanSwapchainManager::get_singleton().get_swapchain_create_info().flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR)
                framebuffer_attachment_image_info.flags |=  VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;;
            VkFramebufferAttachmentsCreateInfo framebuffer_attachments_create_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,
                .attachmentImageInfoCount = 1,
                .pAttachmentImageInfos = &framebuffer_attachment_image_info
            };
            VkFramebufferCreateInfo framebuffer_create_info = {
                .pNext = &framebuffer_attachments_create_info,
                .flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT,
                .renderPass = rpwf_imageless.render_pass,
                .attachmentCount = 1,
                .width = window_size.width,
                .height = window_size.height,
                .layers = 1
            };
            rpwf_imageless.framebuffer.create(framebuffer_create_info);
        };
        auto destroy_framebuffer = [] {
            rpwf_imageless.framebuffer.~VulkanFramebuffer();
        };
        create_framebuffer();

        ExecuteOnce(rpwf_imageless);
        VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(create_framebuffer);
        VulkanSwapchainManager::get_singleton().add_callback_destroy_swapchain(destroy_framebuffer);

        return rpwf_imageless;
    }

    void clear_rpwf_screen_imagelss_framebuffer() {
        rpwf_imageless.render_pass.clear();
        rpwf_imageless.framebuffer.clear();
    }

    const auto& create_rpwf_offscreen(VkExtent2D canvas_size) {
        ca_canvas.create(VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat, canvas_size,
            1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        VkAttachmentDescription attachment_description = {
            .format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
         };

        VkSubpassDependency subpass_dependencies[2] = {
            {
                .srcSubpass = 0,
                .dstSubpass = VK_SUBPASS_EXTERNAL,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
            },
            {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
            }
        };
        VkAttachmentReference attachment_reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass_description = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment_reference
        };
        VkRenderPassCreateInfo render_pass_create_info = {
            .attachmentCount = 1,
            .pAttachments = &attachment_description,
            .subpassCount = 1,
            .pSubpasses = &subpass_description,
            .dependencyCount = 2,
            .pDependencies = subpass_dependencies,
        };
        rpwf_offscreen.render_pass.create(render_pass_create_info);
        VkFramebufferCreateInfo framebuffer_create_info = {
            .renderPass = rpwf_offscreen.render_pass,
            .attachmentCount = 1,
            .pAttachments = ca_canvas.get_address_of_image_view(),
            .width = canvas_size.width,
            .height = canvas_size.height,
            .layers = 1
        };
        rpwf_offscreen.framebuffer.create(framebuffer_create_info);
        return rpwf_offscreen;
    }

    void clear_rpwf_offcreen() {
        rpwf_offscreen.render_pass.clear();
        rpwf_offscreen.framebuffer.clear();
    }

    const auto& create_rpwf_offscreen_ds() {
        dsa_offscreen.create(
            VK_FORMAT_D16_UNORM,
            shadow_map_size,
            1,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_USAGE_SAMPLED_BIT
        );

        ca_offscreen_vsm.create(
            VK_FORMAT_R32G32_SFLOAT,
            shadow_map_size,
            1,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
        );

        VkAttachmentDescription attachment_description[2] = {
            {
                .format = VK_FORMAT_R32G32_SFLOAT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_GENERAL
            },
            {
                .format = VK_FORMAT_D16_UNORM,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            }
        };

        VkAttachmentReference attachment_reference[2] = {
            {
                    .attachment = 0,
                    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                },
            {
                    .attachment = 1,
                    .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            }

        };

        VkSubpassDescription subpass_description = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment_reference[0],
            .pDepthStencilAttachment = &attachment_reference[1]

        };

        VkSubpassDependency subpass_dependencies[2] = {
            {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
            },
            {
                .srcSubpass = 0,
                .dstSubpass = VK_SUBPASS_EXTERNAL,
                .srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
            }
        };

        VkRenderPassCreateInfo render_pass_create_info = {
            .attachmentCount = 2,
            .pAttachments = attachment_description,
            .subpassCount = 1,
            .pSubpasses = &subpass_description,
            .dependencyCount = 2,
            .pDependencies = subpass_dependencies
        };
        rpwf_offscreen_ds.render_pass.create(render_pass_create_info);

        VkImageView attachments[2] = {
              ca_offscreen_vsm.get_image_view(),   // color (RG32F)
              dsa_offscreen.get_image_view()       // depth
          };

        VkFramebufferCreateInfo framebuffer_create_info = {
            .renderPass = rpwf_offscreen_ds.render_pass,
            .attachmentCount = 2,
            .pAttachments = attachments,
            .width = shadow_map_size.width,
            .height = shadow_map_size.height,
            .layers = 1
        };
        rpwf_offscreen_ds.framebuffer.create(framebuffer_create_info);
        return rpwf_offscreen_ds;

    }


    void clear_rpwf_offcreen_ds() {
        rpwf_offscreen_ds.render_pass.clear();
        rpwf_offscreen_ds.framebuffer.clear();
    }

    const auto& create_rpwf_ds() {
        _depth_stencil_format = VulkanCore::get_singleton().get_vulkan_device().get_supported_depth_format();

        // _depth_stencil_format = depth_stencil_format;
        VkAttachmentDescription attachment_description[2] = {
            {
                .format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            },
            {
                .format = _depth_stencil_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = _depth_stencil_format != VK_FORMAT_S8_UINT ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = _depth_stencil_format >= VK_FORMAT_S8_UINT ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            }
        };
        VkAttachmentReference attachment_reference[2] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
        };
        VkSubpassDescription subpass_description ={
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = attachment_reference,
            .pDepthStencilAttachment = attachment_reference + 1
        };

        VkSubpassDependency subpass_dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
        };
        VkRenderPassCreateInfo render_pass_create_info = {
            .attachmentCount = 2,
            .pAttachments = attachment_description,
            .subpassCount = 1,
            .pSubpasses = &subpass_description,
            .dependencyCount = 1,
            .pDependencies = &subpass_dependency,
        };



        rpwf_ds.render_pass.create(render_pass_create_info);
        auto create_framebuffers = [this] {
            dsas_screen_with_ds.resize(VulkanSwapchainManager::get_singleton().get_swapchain_image_count());
            rpwf_ds.framebuffers.resize(VulkanSwapchainManager::get_singleton().get_swapchain_image_count());
            for (auto&i : dsas_screen_with_ds)
                i.create(_depth_stencil_format,window_size,1,VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
            VkFramebufferCreateInfo framebuffer_create_info = {
                .renderPass = rpwf_ds.render_pass,
                .attachmentCount = 2,
                .width = window_size.width,
                .height = window_size.height,
                .layers = 1
            };
            for (size_t i=0; i < VulkanSwapchainManager::get_singleton().get_swapchain_image_count(); i++) {
                VkImageView attachment[2] = {
                    VulkanSwapchainManager::get_singleton().get_swapchain_image_view(i),
                    dsas_screen_with_ds[i].get_image_view()
                };
                framebuffer_create_info.pAttachments = attachment;
                rpwf_ds.framebuffers[i].create(framebuffer_create_info);
            }

        };
        auto destroy_framebuffers = [this] {
            dsas_screen_with_ds.clear();
            rpwf_ds.framebuffers.clear();
        };
        create_framebuffers();

        ExecuteOnce(rpwf_ds);
        VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(create_framebuffers);
        VulkanSwapchainManager::get_singleton().add_callback_destroy_swapchain(destroy_framebuffers);
        return rpwf_ds;
    }

    void clear_rpwf_ds() {
        rpwf_ds.render_pass.clear();
        rpwf_ds.framebuffers.clear();
    }

    const auto& create_rpwf_deferred_to_screen() {
        _depth_stencil_format = VulkanCore::get_singleton().get_vulkan_device().get_supported_depth_format();
        VkAttachmentDescription attachment_description[4] = {
            {
                .format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            },
            {
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            },
            {
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            },
            {
                .format = _depth_stencil_format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = _depth_stencil_format >= VK_FORMAT_S8_UINT ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            }
        };

        VkAttachmentReference attachment_references_subpass0[3] = {
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, // normalZ
            {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, // specular & albedo
            {3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL} // depth
        };
        VkAttachmentReference attachment_references_subpass1[3] = {
            {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, // normalZ
            {2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, // specular & albedo
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } // swapchain image
        };
        VkSubpassDescription subpass_description[2] = {
            { // 第一个子通道，生成G-Buffer
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 2,
                .pColorAttachments = attachment_references_subpass0,
                .pDepthStencilAttachment = attachment_references_subpass0 + 2
            },
            { // 第二个子通道，进行composition
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .inputAttachmentCount = 2,
                .pInputAttachments = attachment_references_subpass1,
                .colorAttachmentCount = 1,
                .pColorAttachments = attachment_references_subpass1 + 2
            }
        };
        VkSubpassDependency subpass_dependency[2] = {
            {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
            },
            {
                .srcSubpass = 0,
                .dstSubpass = 1,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
            }
        };
        VkRenderPassCreateInfo render_pass_create_info = {
            .attachmentCount = 4,
            .pAttachments = attachment_description,
            .subpassCount = 2,
            .pSubpasses = subpass_description,
            .dependencyCount = 2,
            .pDependencies = subpass_dependency
        };
        rpwf_deferred_to_screen.render_pass.create(render_pass_create_info);

        auto create_framebuffers = [&]() {
            rpwf_deferred_to_screen.framebuffers.resize(VulkanSwapchainManager::get_singleton().get_swapchain_image_count());
            ca_deferred_to_screen_normalZ.create(VK_FORMAT_R16G16B16A16_SFLOAT,window_size,1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
            ca_deferred_to_screen_albedo_specular.create(VK_FORMAT_R8G8B8A8_UNORM,window_size,1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
            dsa_deferred_to_screen.create(_depth_stencil_format,window_size,1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
            VkImageView attachment[4] = {
                VK_NULL_HANDLE,
                ca_deferred_to_screen_normalZ.get_image_view(),
                ca_deferred_to_screen_albedo_specular.get_image_view(),
                dsa_deferred_to_screen.get_image_view()
            };
            VkFramebufferCreateInfo framebuffer_create_info = {
                .renderPass = rpwf_deferred_to_screen.render_pass,
                .attachmentCount = 4,
                .pAttachments = attachment,
                .width = window_size.width,
                .height = window_size.height,
                .layers = 1
            };
            for (size_t i = 0; i < VulkanSwapchainManager::get_singleton().get_swapchain_image_count(); i++) {
                attachment[0] = VulkanSwapchainManager::get_singleton().get_swapchain_image_view(i);
                rpwf_deferred_to_screen.framebuffers[i].create(framebuffer_create_info);
            }
        };
        auto destory_framebuffers = [this] {
            ca_deferred_to_screen_normalZ.~VulkanColorAttachment();
            ca_deferred_to_screen_albedo_specular.~VulkanColorAttachment();
            dsa_deferred_to_screen.~VulkanDepthStencilAttachment();
            rpwf_deferred_to_screen.framebuffers.clear();
        };
        create_framebuffers();

        ExecuteOnce(rpwf_deferred_to_screen);
        VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(create_framebuffers);
        VulkanSwapchainManager::get_singleton().add_callback_destroy_swapchain(destory_framebuffers);
        return rpwf_deferred_to_screen;
    }

    void clear_rpwf_deferred_to_screen() {
        rpwf_deferred_to_screen.render_pass.clear();
        rpwf_deferred_to_screen.framebuffers.clear();
    }

    void clear_all_rpwf() {
        clear_rpwf_screen();
        clear_rpwf_imgui();
        clear_rpwf_screen_imagelss_framebuffer();
        clear_rpwf_offcreen();
        clear_rpwf_ds();
        clear_rpwf_deferred_to_screen();
        clear_rpwf_offcreen_ds();
    }

    void cmd_clear_canvas(VkCommandBuffer command_buffer, VkClearColorValue clear_color_value) {
        VkImageSubresourceRange imageSubresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageMemoryBarrier imageMemoryBarrier = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            nullptr,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            ca_canvas.get_image(),
            imageSubresourceRange
        };
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

        vkCmdClearColorImage(command_buffer, ca_canvas.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color_value, 1, &imageSubresourceRange);

        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = 0;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
    }


private:
    inline static RenderPassWithFramebuffers rpwf;
    inline static RenderPassWithFramebuffers rpwf_imgui;
    inline static RenderPassWithFramebuffer rpwf_imageless;
    inline static RenderPassWithFramebuffer rpwf_offscreen;
    inline static RenderPassWithFramebuffer rpwf_offscreen_ds;
    inline static RenderPassWithFramebuffer rpwf_offscreen_ds_vssm;
    inline static RenderPassWithFramebuffers rpwf_deferred_to_screen;
    inline static VulkanColorAttachment ca_canvas;

    std::vector<VulkanDepthStencilAttachment>dsas_screen_with_ds;
    inline static RenderPassWithFramebuffers rpwf_ds;
    inline static VkFormat _depth_stencil_format;

    const VkExtent2D shadow_map_size = { 2048, 2048 };

    VulkanDepthStencilAttachment dsa_deferred_to_screen;
    VulkanColorAttachment ca_deferred_to_screen_normalZ;
    VulkanColorAttachment ca_deferred_to_screen_albedo_specular;

    VulkanColorAttachment ca_offscreen_vsm;
    VulkanDepthStencilAttachment dsa_offscreen;

};
