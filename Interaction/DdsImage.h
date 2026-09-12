#pragma once

#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <bcdec.h>

// DDS（DirectDraw Surface）解码：把 mip 0 解成 RGBA8，供 VulkanTexture2D 直接上传。
//
// 覆盖范围（按本项目实测的资产优先级排）：
//   * BC1 / DXT1（含 1-bit alpha 变体）、BC3 / DXT5、BC5 / ATI2 —— Bistro v5.2 的 622 张贴图全是这三种；
//   * 顺带支持 BC2 / DXT3、BC4 / ATI1、BC7 以及 32 位未压缩（按掩码取通道）；
//   * 只取 mip 0：引擎的贴图路径本来就会自己生成 mip 链（与 glTF 路径一致）。
// BC4/BC5 是单/双通道格式：BC4 按灰度展开，BC5 保留 R/G、B 补 0（它们是法线图，不作为 base color 用）。
class DdsImage {
public:
    static bool is_dds(const uint8_t* data, size_t size) {
        return data != nullptr && size >= 4
            && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ';
    }

    // 成功时 rgba 是 width*height 的 RGBA8 像素；失败时 error 里是原因。
    static bool decode(const uint8_t* data,
                       size_t size,
                       std::vector<uint8_t>& rgba,
                       uint32_t& width,
                       uint32_t& height,
                       std::string& format_name,
                       std::string& error) {
        constexpr size_t k_header_size = 4 + 124;   // magic + DDS_HEADER
        if (!is_dds(data, size) || size < k_header_size) {
            error = "DDS 头不完整";
            return false;
        }
        if (read_u32(data, 4) != 124) {
            error = std::format("DDS 头大小异常：{}", read_u32(data, 4));
            return false;
        }

        height = read_u32(data, 12);
        width = read_u32(data, 16);
        if (width == 0 || height == 0 || width > 32768 || height > 32768) {
            error = std::format("DDS 尺寸异常：{}x{}", width, height);
            return false;
        }

        const uint32_t pixel_format_flags = read_u32(data, 80);
        const uint32_t four_cc = read_u32(data, 84);
        const uint32_t rgb_bit_count = read_u32(data, 88);
        const uint32_t channel_masks[4] = { read_u32(data, 92), read_u32(data, 96), read_u32(data, 100), read_u32(data, 104) };

        size_t pixel_data_offset = k_header_size;
        Format format = Format::unknown;
        if ((pixel_format_flags & k_ddpf_fourcc) != 0) {
            switch (four_cc) {
                case four_cc_code('D', 'X', 'T', '1'): format = Format::bc1; break;
                case four_cc_code('D', 'X', 'T', '2'):
                case four_cc_code('D', 'X', 'T', '3'): format = Format::bc2; break;
                case four_cc_code('D', 'X', 'T', '4'):
                case four_cc_code('D', 'X', 'T', '5'): format = Format::bc3; break;
                case four_cc_code('A', 'T', 'I', '1'):
                case four_cc_code('B', 'C', '4', 'U'): format = Format::bc4; break;
                case four_cc_code('A', 'T', 'I', '2'):
                case four_cc_code('B', 'C', '5', 'U'): format = Format::bc5; break;
                case k_four_cc_dx10: {
                    if (size < pixel_data_offset + 20) {
                        error = "DDS DX10 扩展头不完整";
                        return false;
                    }
                    const uint32_t dxgi_format = read_u32(data, pixel_data_offset);
                    pixel_data_offset += 20;
                    format = format_from_dxgi(dxgi_format);
                    if (format == Format::unknown)
                        error = std::format("不支持的 DXGI 格式：{}", dxgi_format);
                    break;
                }
                default:
                    error = std::format("不支持的 DDS fourCC：'{}'", four_cc_to_string(four_cc));
                    break;
            }
        }
        else if ((pixel_format_flags & k_ddpf_rgb) != 0 && rgb_bit_count == 32) {
            format = Format::raw32;
        }

        if (format == Format::unknown) {
            if (error.empty())
                error = std::format("不支持的 DDS 格式（fourCC '{}', bitCount {}）", four_cc_to_string(four_cc), rgb_bit_count);
            return false;
        }

        rgba.assign(static_cast<size_t>(width) * height * 4, 255);

        if (format == Format::raw32) {
            const size_t pixel_count = static_cast<size_t>(width) * height;
            if (pixel_data_offset + pixel_count * 4 > size) {
                error = "DDS 像素数据不足";
                return false;
            }
            for (size_t i = 0; i < pixel_count; ++i) {
                const uint32_t pixel = read_u32(data, pixel_data_offset + i * 4);
                rgba[i * 4 + 0] = extract_channel(pixel, channel_masks[0]);
                rgba[i * 4 + 1] = extract_channel(pixel, channel_masks[1]);
                rgba[i * 4 + 2] = extract_channel(pixel, channel_masks[2]);
                rgba[i * 4 + 3] = extract_channel(pixel, channel_masks[3]);
            }
            format_name = "uncompressed-32";
            return true;
        }

        const uint32_t block_bytes = block_size(format);
        const uint32_t blocks_x = (width + 3) / 4;
        const uint32_t blocks_y = (height + 3) / 4;
        if (pixel_data_offset + static_cast<size_t>(blocks_x) * blocks_y * block_bytes > size) {
            error = std::format("DDS 压缩块数据不足（需要 {} 字节）", static_cast<size_t>(blocks_x) * blocks_y * block_bytes);
            return false;
        }

        // BC1/2/3/7 解成 4x4 RGBA8（64 字节）；BC4 是 1 字节/像素、BC5 是 2 字节/像素。
        std::vector<uint8_t> block(64);
        const uint8_t* source = data + pixel_data_offset;
        const uint32_t channels = format == Format::bc4 ? 1u : (format == Format::bc5 ? 2u : 4u);
        for (uint32_t by = 0; by < blocks_y; ++by) {
            for (uint32_t bx = 0; bx < blocks_x; ++bx) {
                const uint8_t* compressed = source + (static_cast<size_t>(by) * blocks_x + bx) * block_bytes;
                decode_block(format, compressed, block.data());
                for (uint32_t py = 0; py < 4; ++py) {
                    const uint32_t y = by * 4 + py;
                    if (y >= height)
                        break;
                    for (uint32_t px = 0; px < 4; ++px) {
                        const uint32_t x = bx * 4 + px;
                        if (x >= width)
                            continue;
                        const uint8_t* texel = block.data() + (static_cast<size_t>(py) * 4 + px) * channels;
                        uint8_t* destination = rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
                        if (channels == 1) {
                            destination[0] = destination[1] = destination[2] = texel[0];
                            destination[3] = 255;
                        }
                        else if (channels == 2) {
                            destination[0] = texel[0];
                            destination[1] = texel[1];
                            destination[2] = 0;
                            destination[3] = 255;
                        }
                        else {
                            std::memcpy(destination, texel, 4);
                        }
                    }
                }
            }
        }

        format_name = format_name_of(format);
        return true;
    }

private:
    enum class Format { unknown, bc1, bc2, bc3, bc4, bc5, bc7, raw32 };

    static constexpr uint32_t k_ddpf_fourcc = 0x4;
    static constexpr uint32_t k_ddpf_rgb = 0x40;
    static constexpr uint32_t k_four_cc_dx10 = 0x30315844;   // "DX10"

    static constexpr uint32_t four_cc_code(char a, char b, char c, char d) {
        return static_cast<uint32_t>(static_cast<uint8_t>(a))
             | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
             | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
    }

    static uint32_t read_u32(const uint8_t* data, size_t offset) {
        uint32_t value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    static std::string four_cc_to_string(uint32_t four_cc) {
        std::string text(4, ' ');
        for (size_t i = 0; i < 4; ++i) {
            const char c = static_cast<char>((four_cc >> (i * 8)) & 0xFF);
            text[i] = (c >= 32 && c < 127) ? c : '?';
        }
        return text;
    }

    static Format format_from_dxgi(uint32_t dxgi_format) {
        switch (dxgi_format) {
            case 70: case 71: case 72: return Format::bc1;   // BC1_TYPELESS / UNORM / UNORM_SRGB
            case 73: case 74: case 75: return Format::bc2;
            case 76: case 77: case 78: return Format::bc3;
            case 79: case 80: case 81: return Format::bc4;
            case 82: case 83: case 84: return Format::bc5;
            case 97: case 98: case 99: return Format::bc7;
            default: return Format::unknown;
        }
    }

    static uint32_t block_size(Format format) {
        switch (format) {
            case Format::bc1: return 8;
            case Format::bc2: case Format::bc3: case Format::bc5: case Format::bc7: return 16;
            case Format::bc4: return 8;
            default: return 0;
        }
    }

    static const char* format_name_of(Format format) {
        switch (format) {
            case Format::bc1: return "BC1/DXT1";
            case Format::bc2: return "BC2/DXT3";
            case Format::bc3: return "BC3/DXT5";
            case Format::bc4: return "BC4/ATI1";
            case Format::bc5: return "BC5/ATI2";
            case Format::bc7: return "BC7";
            default: return "unknown";
        }
    }

    static void decode_block(Format format, const uint8_t* compressed, uint8_t* destination) {
        switch (format) {
            case Format::bc1: bcdec_bc1(compressed, destination, 4 * 4); break;
            case Format::bc2: bcdec_bc2(compressed, destination, 4 * 4); break;
            case Format::bc3: bcdec_bc3(compressed, destination, 4 * 4); break;
            case Format::bc4: bcdec_bc4(compressed, destination, 4 * 1); break;
            case Format::bc5: bcdec_bc5(compressed, destination, 4 * 2); break;
            case Format::bc7: bcdec_bc7(compressed, destination, 4 * 4); break;
            default: break;
        }
    }

    // 把掩码描述的通道拉成 0-255（支持 5/6/8 位等任意位宽）。
    static uint8_t extract_channel(uint32_t pixel, uint32_t mask) {
        if (mask == 0)
            return 255;   // 没有 alpha 掩码的 32 位格式按不透明处理
        uint32_t shift = 0;
        while (shift < 32 && ((mask >> shift) & 1u) == 0)
            ++shift;
        uint32_t bits = 0;
        for (uint32_t m = mask >> shift; (m & 1u) != 0; m >>= 1)
            ++bits;
        if (bits == 0 || bits > 16)
            return 255;
        const uint32_t value = (pixel & mask) >> shift;
        const uint32_t max_value = (1u << bits) - 1u;
        return static_cast<uint8_t>(value * 255u / max_value);
    }
};