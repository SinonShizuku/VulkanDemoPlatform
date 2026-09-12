#pragma once
#include "../../Start.h"
#include"../VulkanCore.h"

class VulkanRenderPass {
    VkRenderPass handle = VK_NULL_HANDLE;
public:
    VulkanRenderPass() = default;
    VulkanRenderPass(VkRenderPassCreateInfo &create_info) {
        create(create_info);
    }
    VulkanRenderPass(VulkanRenderPass &&other) noexcept {MoveHandle;}
    ~VulkanRenderPass() { DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(), vkDestroyRenderPass); }
    void clear() {DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyRenderPass);}

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // const function
    void cmd_begin(VkCommandBuffer command_buffer, VkRenderPassBeginInfo &begin_info, VkSubpassContents subpass_contents = VK_SUBPASS_CONTENTS_INLINE) const {
        begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin_info.renderPass = handle;
        vkCmdBeginRenderPass(command_buffer, &begin_info, subpass_contents);
    }

    void cmd_begin(VkCommandBuffer command_buffer, VkFramebuffer framebuffer, VkRect2D render_area, array_ref<const VkClearValue>clear_values = {}, VkSubpassContents subpass_contents = VK_SUBPASS_CONTENTS_INLINE) const {
        VkRenderPassBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = handle,
            .framebuffer = framebuffer,
            .renderArea = render_area,
            .clearValueCount = uint32_t(clear_values.Count()),
            .pClearValues = clear_values.Pointer()
        };
        vkCmdBeginRenderPass(command_buffer, &begin_info, subpass_contents);
    }

    void cmd_next(VkCommandBuffer command_buffer, VkSubpassContents subpass_contents = VK_SUBPASS_CONTENTS_INLINE) const {
        vkCmdNextSubpass(command_buffer, subpass_contents);
    }

    void cmd_end(VkCommandBuffer command_buffer) const {
        vkCmdEndRenderPass(command_buffer);
    }

    result_t create(VkRenderPassCreateInfo &create_info) {
        create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        VkResult result = vkCreateRenderPass(VulkanCore::get_singleton().get_vulkan_device().get_device(), &create_info, nullptr, &handle);
        if (result) outstream<<std::format("[ VulkanRenderPass ] ERROR\nFailed to create a render pass!\nError code: {}\n", int32_t(result));
        return result;
    }
};

class VulkanFramebuffer {
    VkFramebuffer handle = VK_NULL_HANDLE;
public:
    VulkanFramebuffer() = default;
    VulkanFramebuffer(VkFramebufferCreateInfo &create_info) {
        create(create_info);
    }
    VulkanFramebuffer(VulkanFramebuffer &&other) {MoveHandle;}
    ~VulkanFramebuffer() { DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(), vkDestroyFramebuffer); }
    void clear(){ DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyFramebuffer);}

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    result_t create(VkFramebufferCreateInfo &create_info) {
        create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        VkResult result = vkCreateFramebuffer(VulkanCore::get_singleton().get_vulkan_device().get_device(),&create_info,nullptr,&handle);
        if (result != VK_SUCCESS) outstream<<std::format("[ VulkanFramebuffer ] ERROR\nFailed to create a framebuffer!\nError code: {}\n", int32_t(result));
        return result;
    }
};

struct RenderPassWithFramebuffers {
    VulkanRenderPass render_pass;
    std::vector<VulkanFramebuffer> framebuffers;
};

struct RenderPassWithFramebuffer {
    VulkanRenderPass render_pass;
    VulkanFramebuffer framebuffer;
};



