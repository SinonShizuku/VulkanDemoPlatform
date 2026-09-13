#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtc/packing.hpp>     // glm::packHalf2x16
#include <stb_image.h>

// Radiance HDR 加载：stbi_loadf 解成 32F 线性像素，再压成 R16G16B16A16_SFLOAT 需要的半浮点数据。
// 为什么不用 Texture::load_file 的浮点路径：32F 在多数设备上不可线性过滤（采样与 mip 生成都会踩），
// 而 Texture::load_file 的调试检查只接受 4 字节浮点分量；16F 是 Vulkan 保证可过滤的格式。
class HdrImage {
public:
    static bool load(const std::filesystem::path& file,
                     std::vector<uint16_t>& half_pixels,
                     uint32_t& width,
                     uint32_t& height,
                     std::string& error) {
        int loaded_width = 0;
        int loaded_height = 0;
        int channels = 0;
        float* pixels = stbi_loadf(file.string().c_str(), &loaded_width, &loaded_height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr || loaded_width <= 0 || loaded_height <= 0) {
            error = "stb_image 解不出 HDR（文件缺失或格式不支持）";
            return false;
        }

        width = static_cast<uint32_t>(loaded_width);
        height = static_cast<uint32_t>(loaded_height);
        half_pixels.resize(static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < half_pixels.size(); i += 2) {
            const uint32_t packed = glm::packHalf2x16(glm::vec2(pixels[i], pixels[i + 1]));
            half_pixels[i] = static_cast<uint16_t>(packed & 0xFFFFu);
            half_pixels[i + 1] = static_cast<uint16_t>(packed >> 16);
        }
        stbi_image_free(pixels);
        return true;
    }

    // 直接给出可上传的字节视图（与 VulkanTexture2D::create 的入参对齐）
    static const uint8_t* as_bytes(const std::vector<uint16_t>& half_pixels) {
        return reinterpret_cast<const uint8_t*>(half_pixels.data());
    }
};