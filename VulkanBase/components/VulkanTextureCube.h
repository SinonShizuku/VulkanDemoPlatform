#pragma once

#include "VulkanTexture.h"

// Cubemap 资源（IBL 的地基）：6 层 + VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT + 可选 mip。
//   * 采样用整张 cube view（VK_IMAGE_VIEW_TYPE_CUBE，全部 6 面 + 全部 mip）；
//   * 上传 / 渲染用"单面（可指定 mip）"的 2D view —— 这样每个面可以单独作为渲染目标或拷贝目标，
//     不需要 layered rendering，也不需要给 FrameGraph 增加 cube 目标的概念。
class VulkanTextureCube : public VulkanTexture {
public:
    VulkanTextureCube() = default;

    void create(uint32_t size, VkFormat format, uint32_t mip_level_count = 1, VkImageUsageFlags extra_usage = 0) {
        size_ = size;
        mip_level_count_ = mip_level_count;
        format_ = format;

        VkImageCreateInfo create_info = {
            .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { size, size, 1 },
            .mipLevels = mip_level_count,
            .arrayLayers = 6,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extra_usage
        };
        image_memory.create(create_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        image_view.create(image_memory.Image(), VK_IMAGE_VIEW_TYPE_CUBE, format,
                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_level_count, 0, 6 });

        face_views_.resize(6);
        for (uint32_t face = 0; face < 6; ++face) {
            face_views_[face].resize(mip_level_count);
            for (uint32_t mip = 0; mip < mip_level_count; ++mip) {
                face_views_[face][mip].create(image_memory.Image(), VK_IMAGE_VIEW_TYPE_2D, format,
                                              { VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, face, 1 });
            }
        }
    }

    // getter
    VkImageView get_cube_view() const { return image_view; }
    VkImageView get_face_view(uint32_t face, uint32_t mip = 0) const { return face_views_[face][mip]; }
    VkFormat get_format() const { return format_; }
    uint32_t get_size() const { return size_; }
    uint32_t get_mip_level_count() const { return mip_level_count_; }
    uint32_t get_face_size(uint32_t mip = 0) const { return std::max(1u, size_ >> mip); }

    VkDescriptorImageInfo get_descriptor_image_info(VkSampler sampler) const {
        return {
            .sampler = sampler,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    // 单个面（指定 mip）的 CPU 数据上传：staging → copy → 过渡到 SHADER_READ_ONLY。
    // 注意 cmd_copy_buffer_to_image 的 barrier 只覆盖本次 region 的 mip/face 范围，
    // 所以逐面上传不会破坏已经上传好的其它面。
    void upload_face(uint32_t face, uint32_t mip, const uint8_t* pixels, size_t size_in_bytes) {
        VulkanStagingBuffer::buffer_data_main_thread(pixels, size_in_bytes);
        const uint32_t level_size = get_face_size(mip);
        auto& command_buffer = VulkanCommand::get_singleton().get_command_buffer_transfer();
        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VkBufferImageCopy region = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, face, 1 },
            .imageExtent = { level_size, level_size, 1 }
        };
        image_operation::cmd_copy_buffer_to_image(command_buffer, VulkanStagingBuffer::get_buffer_main_thread(),
            image_memory.Image(), region,
            { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED },
            { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        command_buffer.end();
        VulkanCommand::get_singleton().execute_command_buffer_graphics(command_buffer);
    }


private:
    uint32_t size_ = 0;
    uint32_t mip_level_count_ = 1;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    // face_views_[face][mip]：渲染/拷贝目标用的 2D 视图
    std::vector<std::vector<VulkanImageView>> face_views_;
};
