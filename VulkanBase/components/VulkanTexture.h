#pragma once
#include "VulkanMemory.h"
#include "VulkanOperation.h"
#include "../../Start.h"
#include "../VulkanCore.h"
#include "../../Interaction/Texture.h"

class VulkanTexture : public Texture {
protected:
    VulkanImageView image_view;
    VulkanImageMemory image_memory;
    VulkanTexture() = default;

    void create_image_memory(VkImageType image_type, VkFormat format, VkExtent3D extent, uint32_t mip_level_count, uint32_t array_layer_count, VkImageCreateFlags flags = 0) {
        VkImageCreateInfo image_create_info = {
            .flags = flags,
            .imageType = image_type,
            .format = format,
            .extent = extent,
            .mipLevels = mip_level_count,
            .arrayLayers = array_layer_count,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        };
        image_memory.create(image_create_info,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    void create_image_view(VkImageViewType image_view_type, VkFormat format, uint32_t mip_level_count, uint32_t array_layer_count, VkImageCreateFlags flags = 0) {
        image_view.create(image_memory.Image(), image_view_type, format, {VK_IMAGE_ASPECT_COLOR_BIT,0,mip_level_count,0,array_layer_count}, flags);
    }

public:
    // getter
    VkImageView get_image_view() const {return image_view;}
    VkImage get_image() const {return image_memory.Image();}
    const VkImageView* get_address_of_image_view() const {return image_view.Address();}
    const VkImage* get_address_of_image() const {return image_memory.get_address_of_image();}

    // const function
    VkDescriptorImageInfo get_descriptor_image_info(VkSampler sampler) const {
        return {
            .sampler = sampler,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    static uint32_t calculate_mip_level_count(VkExtent2D extent) {
        return uint32_t(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
    }
    static void CopyBlitAndGenerateMipmap2d(VkBuffer buffer_copy_from, VkImage image_copy_to, VkImage image_blit_to, VkExtent2D image_extent,
        uint32_t mip_level_count = 1, uint32_t layer_count = 1, VkFilter min_filter = VK_FILTER_LINEAR) {
        bool generate_mipmap = mip_level_count > 1;
        bool blit_mip_level0 = image_copy_to != image_blit_to;
        static constexpr image_operation::image_memory_barrier_parameter_pack imbs[2] = {
            { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL }
        };
        auto& command_buffer = VulkanCommand::get_singleton().get_command_buffer_transfer();
        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            VkBufferImageCopy region = {
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layer_count },
                .imageExtent = { image_extent.width, image_extent.height, 1 }
            };
            image_operation::cmd_copy_buffer_to_image(command_buffer, buffer_copy_from, image_copy_to, region,
            { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED },
            imbs[generate_mipmap||blit_mip_level0]);

            if (blit_mip_level0) {
                VkImageBlit region = {
                    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layer_count },
                    { {}, { int32_t(image_extent.width), int32_t(image_extent.height), 1 } },
                    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layer_count },
                    { {}, { int32_t(image_extent.width), int32_t(image_extent.height), 1 } }
                };
                image_operation::cmd_blit_image(command_buffer, image_copy_to, image_blit_to, region,
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED },
                imbs[generate_mipmap], min_filter);
            }
            if (generate_mipmap)
                image_operation::cmd_generate_mipmap2d(command_buffer, image_blit_to, image_extent, mip_level_count, layer_count,
                    { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, min_filter);

        }
        command_buffer.end();
        VulkanCommand::get_singleton().execute_command_buffer_graphics(command_buffer);

    }
    static void blit_and_generate_mipmap2d(VkImage image_preinitialized, VkImage image_final, VkExtent2D image_extent,
        uint32_t mip_level_count = 1, uint32_t layer_count = 1, VkFilter min_filter = VK_FILTER_LINEAR) {
        bool generate_mipmap = mip_level_count > 1;
        bool blit_mip_level0 = image_preinitialized != image_final;
        static constexpr image_operation::image_memory_barrier_parameter_pack imbs[2] = {
            { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL }
        };
        if (blit_mip_level0 || generate_mipmap) {
            auto& command_buffer = VulkanCommand::get_singleton().get_command_buffer_transfer();
            command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            {
                if (blit_mip_level0) {
                    VkImageMemoryBarrier image_memory_barrier = {
                        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        nullptr,
                        0,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_IMAGE_LAYOUT_PREINITIALIZED,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED,
                        VK_QUEUE_FAMILY_IGNORED,
                        image_preinitialized,
                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layer_count}
                    };
                    vkCmdPipelineBarrier(command_buffer,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,0, nullptr, 0, nullptr, 1, &image_memory_barrier);
                    VkImageBlit region = {
                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layer_count },
                        { {}, { int32_t(image_extent.width), int32_t(image_extent.height), 1 } },
                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layer_count },
                        { {}, { int32_t(image_extent.width), int32_t(image_extent.height), 1 } }
                    };
                    image_operation::cmd_blit_image(command_buffer, image_preinitialized, image_final, region,
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED },
                imbs[generate_mipmap], min_filter);
                }

                if (generate_mipmap)
                    image_operation::cmd_generate_mipmap2d(command_buffer, image_final, image_extent, mip_level_count, layer_count,
                        { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, min_filter);
            }
            command_buffer.end();
            VulkanCommand::get_singleton().execute_command_buffer_graphics(command_buffer);
        }
    }

    static VkSamplerCreateInfo get_sampler_create_info() {
        return {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.f,
            .anisotropyEnable = VulkanCore::get_singleton().get_vulkan_device().get_physical_device_features().features.samplerAnisotropy,
            .maxAnisotropy = VulkanCore::get_singleton().get_vulkan_device().get_physical_device_properties().limits.maxSamplerAnisotropy,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.f,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = {},
            .unnormalizedCoordinates = VK_FALSE
        };
    }
};

class VulkanTexture2D : public VulkanTexture {
protected:
    VkExtent2D extent = {};
    void create_internal(VkFormat format_initial, VkFormat format_final, bool generate_mipmap) {
        uint32_t mip_level_count = generate_mipmap ? calculate_mip_level_count(extent) : 1;
        create_image_memory(VK_IMAGE_TYPE_2D, format_final, {extent.width,extent.height,1}, mip_level_count, 1);
        create_image_view(VK_IMAGE_VIEW_TYPE_2D, format_final, mip_level_count, 1);
        if (format_initial == format_final)
            CopyBlitAndGenerateMipmap2d(VulkanStagingBuffer::get_buffer_main_thread(), image_memory.Image(), image_memory.Image(), extent, mip_level_count, 1);
        else {
            if (VkImage image_conversion = VulkanStagingBuffer::aliased_image2d_main_thread(format_initial, extent))
                blit_and_generate_mipmap2d(image_conversion, image_memory.Image(), extent, mip_level_count, 1);
            else {
                VkImageCreateInfo create_info = {
                    .imageType = VK_IMAGE_TYPE_2D,
                    .format = format_initial,
                    .extent = {extent.width, extent.height, 1},
                    .mipLevels = 1,
                    .arrayLayers = 1,
                    .samples = VK_SAMPLE_COUNT_1_BIT,
                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                };
                VulkanImageMemory image_memory_conversion(create_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                CopyBlitAndGenerateMipmap2d(VulkanStagingBuffer::get_buffer_main_thread(), image_memory_conversion.Image(), image_memory.Image(), extent, mip_level_count, 1);
            }
        }

    }
public:
    VulkanTexture2D() = default;
    VulkanTexture2D(const char* file_path, VkFormat format_initial, VkFormat format_final, bool generate_mipmap = true) {
        create(file_path, format_initial, format_final, generate_mipmap);
    }
    VulkanTexture2D(const uint8_t* p_image_data, VkExtent2D extent, VkFormat format_initial, VkFormat format_final, bool generate_mipmap = true) {
        create(p_image_data, extent, format_initial, format_final, generate_mipmap);
    }

    // getter
    VkExtent2D get_extent() const {return extent;}
    uint32_t get_width() const {return extent.width;}
    uint32_t get_height() const {return extent.height;}

    // non-const function
    void create(const char* file_path, VkFormat format_initial, VkFormat format_final, bool generate_mipmap = true) {
        VkExtent2D extent;
        VulkanFormatInfo format_info = VulkanCore::get_singleton().get_vulkan_device().get_format_info(format_initial);
        std::unique_ptr<uint8_t[]> image_data = Texture::load_file(file_path, extent, format_info);
        if (image_data)
            create(image_data.get(), extent, format_initial, format_final, generate_mipmap);
    }
    void create(const uint8_t* p_image_data, VkExtent2D extent, VkFormat format_initial, VkFormat format_final, bool generate_mipmap = true) {
        this->extent = extent;
        size_t image_data_size = size_t(VulkanCore::get_singleton().get_vulkan_device().get_format_info(format_initial).sizePerPixel) * extent.width * extent.height;
        VulkanStagingBuffer::buffer_data_main_thread(p_image_data, image_data_size);
        create_internal(format_initial, format_final, generate_mipmap);
    }
};