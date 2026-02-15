#pragma once
#include "../../Start.h"
#include "../VulkanCore.h"

class VulkanPipelineLayout {
    VkPipelineLayout handle = VK_NULL_HANDLE;
public:
    VulkanPipelineLayout() = default;
    VulkanPipelineLayout(VkPipelineLayoutCreateInfo &create_info) {
        create(create_info);
    }
    VulkanPipelineLayout(VulkanPipelineLayout && other) noexcept {MoveHandle;}
    ~VulkanPipelineLayout() {DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyPipelineLayout);}
    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // non-const function
    result_t create(VkPipelineLayoutCreateInfo &create_info) {
        create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkResult result = vkCreatePipelineLayout(VulkanCore::get_singleton().get_vulkan_device().get_device(), &create_info, nullptr, &handle);
        if (result) {
            outstream << std::format("[ VulkanPipelineLayout ] ERROR\nFailed to create a pipeline layout!\nError code: {}\n", int32_t(result));
        }
        return result;
    }

};


struct GraphicsPipelineCreateInfoPack {
    VkGraphicsPipelineCreateInfo create_info =
    { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    // Vertex Input
    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    std::vector<VkVertexInputBindingDescription> vertex_input_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_input_attributes;
    // Input Assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    // Tessellation
    VkPipelineTessellationStateCreateInfo tessellation_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
    //Viewport
    VkPipelineViewportStateCreateInfo viewport_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    std::vector<VkViewport> viewports;
    std::vector<VkRect2D> scissors;
    uint32_t dynamic_viewport_count = 1;//动态视口/剪裁不会用到上述的vector，因此动态视口和剪裁的个数向这俩变量手动指定
    uint32_t dynamic_scissor_count = 1;
    //Rasterization
    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    //Multisample
    VkPipelineMultisampleStateCreateInfo multisample_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    //Depth & Stencil
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    //Color Blend
    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachment_states;
    //Dynamic
    VkPipelineDynamicStateCreateInfo dynamic_state_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    std::vector<VkDynamicState> dynamic_states;
    //--------------------
    GraphicsPipelineCreateInfoPack() {
        set_create_infos();
        //若非派生管线，createInfo.basePipelineIndex不得为0，设置为-1
        create_info.basePipelineIndex = -1;
    }
    //移动构造器，所有指针都要重新赋值
    GraphicsPipelineCreateInfoPack(const GraphicsPipelineCreateInfoPack& other) noexcept {
        create_info = other.create_info;
        set_create_infos();

        vertex_input_state_create_info = other.vertex_input_state_create_info;
        input_assembly_state_create_info = other.input_assembly_state_create_info;
        tessellation_state_create_info = other.tessellation_state_create_info;
        viewport_state_create_info = other.viewport_state_create_info;
        rasterization_state_create_info = other.rasterization_state_create_info;
        multisample_state_create_info = other.multisample_state_create_info;
        depth_stencil_state_create_info = other.depth_stencil_state_create_info;
        color_blend_state_create_info = other.color_blend_state_create_info;
        dynamic_state_create_info = other.dynamic_state_create_info;

        shader_stages = other.shader_stages;
        vertex_input_bindings = other.vertex_input_bindings;
        vertex_input_attributes = other.vertex_input_attributes;
        viewports = other.viewports;
        scissors = other.scissors;
        color_blend_attachment_states = other.color_blend_attachment_states;
        dynamic_states = other.dynamic_states;
        update_all_array_addresses();
    }
    //Getter，这里我没用const修饰符
    operator VkGraphicsPipelineCreateInfo& () { return create_info; }
    //Non-const Function
    //该函数用于将各个vector中数据的地址赋值给各个创建信息中相应成员，并相应改变各个count
    void update_all_arrays() {
        create_info.stageCount = shader_stages.size();
        vertex_input_state_create_info.vertexBindingDescriptionCount = vertex_input_bindings.size();
        vertex_input_state_create_info.vertexAttributeDescriptionCount = vertex_input_attributes.size();
        viewport_state_create_info.viewportCount = viewports.size() ? uint32_t(viewports.size()) : dynamic_viewport_count;
        viewport_state_create_info.scissorCount = scissors.size() ? uint32_t(scissors.size()) : dynamic_scissor_count;
        color_blend_state_create_info.attachmentCount = color_blend_attachment_states.size();
        dynamic_state_create_info.dynamicStateCount = dynamic_states.size();
        update_all_array_addresses();
    }
private:
    //该函数用于将创建信息的地址赋值给basePipelineIndex中相应成员
    void set_create_infos() {
        create_info.pVertexInputState = &vertex_input_state_create_info;
        create_info.pInputAssemblyState = &input_assembly_state_create_info;
        create_info.pTessellationState = &tessellation_state_create_info;
        create_info.pViewportState = &viewport_state_create_info;
        create_info.pRasterizationState = &rasterization_state_create_info;
        create_info.pMultisampleState = &multisample_state_create_info;
        create_info.pDepthStencilState = &depth_stencil_state_create_info;
        create_info.pColorBlendState = &color_blend_state_create_info;
        create_info.pDynamicState = &dynamic_state_create_info;
    }
    //该函数用于将各个vector中数据的地址赋值给各个创建信息中相应成员，但不改变各个count
    void update_all_array_addresses() {
        create_info.pStages = shader_stages.data();
        vertex_input_state_create_info.pVertexBindingDescriptions = vertex_input_bindings.data();
        vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attributes.data();
        viewport_state_create_info.pViewports = viewports.data();
        viewport_state_create_info.pScissors = scissors.data();
        color_blend_state_create_info.pAttachments = color_blend_attachment_states.data();
        dynamic_state_create_info.pDynamicStates = dynamic_states.data();
    }
};

struct ComputePipelineCreateInfoPack {
    VkComputePipelineCreateInfo create_info =
    { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo compute_stage_create_info =
    { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    ComputePipelineCreateInfoPack() = default;
    ComputePipelineCreateInfoPack(const ComputePipelineCreateInfoPack& other) noexcept {
        create_info = other.create_info;
    }
    operator VkComputePipelineCreateInfo& () { return create_info; }
    void update_all() {
        create_info.stage = compute_stage_create_info;
    }
};

class VulkanPipeline {
    VkPipeline handle = VK_NULL_HANDLE;
public:
    VulkanPipeline() = default;
    VulkanPipeline(GraphicsPipelineCreateInfoPack& create_info) {
        create(create_info);
    }
    VulkanPipeline(VkComputePipelineCreateInfo& create_info) {
        create(create_info);
    }

    VulkanPipeline(VulkanPipeline &&other)  {MoveHandle;}
    ~VulkanPipeline() { DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyPipeline);}

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    result_t create(VkGraphicsPipelineCreateInfo &create_info){
        create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        VkResult result = vkCreateGraphicsPipelines(VulkanCore::get_singleton().get_vulkan_device().get_device(),VK_NULL_HANDLE,1,&create_info,nullptr,&handle);
        if (result)
            outstream<<std::format("[ VulkanPipeline ] ERROR\nFailed to create a graphics pipeline!\nError code: {}\n", int32_t(result));
        return result;
    }
    result_t create(VkComputePipelineCreateInfo &create_info){
        create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        VkResult result = vkCreateComputePipelines(VulkanCore::get_singleton().get_vulkan_device().get_device(),VK_NULL_HANDLE,1,&create_info,nullptr,&handle);
        if (result)
            outstream<<std::format("[ VulkanPipeline ] ERROR\nFailed to create a compute pipeline!\nError code: {}\n", int32_t(result));
        return result;
    }
};