#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <gtc/packing.hpp>      // HDR → 16F 用 glm::packHalf2x16

#include "../Interaction/DdsImage.h"

#include "Model.h"

// §14.5 第二阶段：把 FBX/OBJ/PLY 交给 assimp，再映射到引擎现有的 VulkanglTFModel 结构，
// 复用 demo 已有的 descriptor / 绘制流程（.gltf/.glb 仍然走 tinygltf）。
//
// 设计边界（只做静态几何，与 §14.5 的决策记录一致）：
//   * 导入 flag 里用 PreTransformVertices 把节点层级烘进顶点，所以每个 aiMesh 直接对应一个
//     单位矩阵的 Node，引擎侧不需要再现 FBX 的节点树；
//   * 材质只取 base color factor（AI_MATKEY_BASE_COLOR 优先，回落到 AI_MATKEY_COLOR_DIFFUSE）；
//   * 贴图只接受 stb_image 能按 8-bit 解码的格式（png/jpg/tga/bmp），DDS/HDR 等跳过并计数；
//   * 没有可用贴图的材质回落到 images[0] 的 1x1 白色贴图，与 glTFLoading 的兜底一致。
class AssimpModelLoader {
public:
    struct Options {
        // 实例化基准场景只用几何，关掉贴图可以省掉解压与上传；关掉时不会创建任何 Vulkan 贴图。
        bool load_textures = true;
        // Bistro 有 622 个 DDS，逐条打印会刷屏；告警只保留前 N 条，计数照常。
        size_t max_warnings = 8;
    };

    struct Stats {
        uint32_t mesh_count = 0;
        uint32_t primitive_count = 0;
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        uint32_t material_count = 0;
        uint32_t texture_count = 0;
        uint32_t skipped_texture_count = 0;
        // 按来源格式细分：DDS 走 BCn 解码、HDR 走浮点路径，日志/benchmark 里能核对数量。
        uint32_t dds_texture_count = 0;
        uint32_t hdr_texture_count = 0;
        // 材质里各类贴图槽的引用数：当前 loader 只消费 base color，其余先统计（PBR 阶段要用）。
        struct {
            uint32_t base_color = 0;
            uint32_t diffuse = 0;
            uint32_t normal = 0;
            uint32_t metallic = 0;
            uint32_t roughness = 0;
            uint32_t emissive = 0;
            uint32_t ambient_occlusion = 0;
            uint32_t specular = 0;
            uint32_t glossiness = 0;
            uint32_t lightmap = 0;
            uint32_t reflection = 0;
            uint32_t unknown = 0;
        } slots;
        // 世界空间包围盒（PreTransformVertices 之后顶点就是最终位置），调用方用它推导固定机位。
        glm::vec3 bounds_min = glm::vec3(0.0f);
        glm::vec3 bounds_max = glm::vec3(0.0f);
        bool has_bounds = false;
        std::vector<std::string> warnings;
    };

    // 扩展名分派：.fbx/.obj/.ply 走 assimp，其余（.gltf/.glb）继续走 tinygltf。
    static bool supports(const std::filesystem::path& path) {
        const std::string extension = lowercase_extension(path);
        return extension == ".fbx" || extension == ".obj" || extension == ".ply";
    }

    // 成功时把模型内容追加进 model / vertex_buffer / index_buffer；失败时 error 里是 assimp 的原因。
    static bool load(const std::filesystem::path& path,
                     VulkanglTFModel& model,
                     std::vector<VulkanglTFModel::Vertex>& vertex_buffer,
                     std::vector<uint32_t>& index_buffer,
                     const Options& options,
                     Stats& stats,
                     std::string& error) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path.string(), import_flags());
        if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr) {
            error = importer.GetErrorString();
            if (error.empty())
                error = "assimp 返回了空场景";
            return false;
        }
        if (scene->mNumMeshes == 0) {
            error = "资产里没有任何 mesh";
            return false;
        }

        if (options.load_textures)
            append_white_fallback(model);       // images[0] / textures[0]
        count_texture_slots(*scene, stats);
        load_materials(*scene, model);
        if (options.load_textures)
            load_textures(*scene, path.parent_path(), model, options, stats);
        load_meshes(*scene, model, vertex_buffer, index_buffer, stats);
        return !model.nodes.empty();
    }

private:
    static constexpr VkFormat k_texture_format = VK_FORMAT_R8G8B8A8_UNORM;

    static unsigned int import_flags() {
        // PreTransformVertices 把节点层级烘进顶点（静态几何一步到位），FlipUVs 让 FBX/OBJ 的
        // 左下原点 UV 与 glTF 一致，GenSmoothNormals 只对缺法线的 mesh 生效。
        return aiProcess_Triangulate
             | aiProcess_PreTransformVertices
             | aiProcess_GenSmoothNormals
             | aiProcess_FlipUVs
             | aiProcess_JoinIdenticalVertices;
    }

    static std::string lowercase_string(std::string text) {
        for (char& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    static std::string lowercase_extension(const std::filesystem::path& path) {
        std::string extension = path.extension().string();
        for (char& c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return extension;
    }

    static void warn(Stats& stats, const Options& options, std::string message) {
        if (stats.warnings.size() < options.max_warnings)
            stats.warnings.push_back(std::move(message));
    }

    // ---------------------------------------------------------------- 贴图

    static void append_white_fallback(VulkanglTFModel& model) {
        VulkanglTFModel::Image image;
        const uint8_t white[4] = { 255, 255, 255, 255 };
        image.texture.create(white, VkExtent2D{ 1, 1 }, k_texture_format, k_texture_format, false);
        model.images.push_back(std::move(image));
        VulkanglTFModel::Texture texture;
        texture.image_index = 0;
        model.textures.push_back(texture);
    }

    static bool is_supported_texture_extension(const std::string& extension) {
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg"
            || extension == ".tga" || extension == ".bmp" || extension == ".dds" || extension == ".hdr";
    }

    static std::filesystem::path resolve_texture_file(const std::filesystem::path& model_dir,
                                                      const std::string& raw_path) {
        const std::filesystem::path candidate(raw_path);
        if (candidate.is_absolute() && std::filesystem::exists(candidate))
            return candidate;
        const std::filesystem::path relative = model_dir / candidate;
        if (std::filesystem::exists(relative))
            return relative;
        // 有些 MTL/FBX 里写的是"相对某个上层目录"的路径，退化成同名文件再试一次。
        const std::filesystem::path by_name = model_dir / candidate.filename();
        if (std::filesystem::exists(by_name))
            return by_name;
        return relative;
    }

    static std::vector<uint8_t> read_file_bytes(const std::filesystem::path& file) {
        std::ifstream stream(file, std::ios::binary | std::ios::ate);
        if (!stream)
            return {};
        const std::streamsize size = stream.tellg();
        if (size <= 0)
            return {};
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        stream.seekg(0, std::ios::beg);
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
            return {};
        return bytes;
    }

    // DDS：mip 0 解成 RGBA8，mip 链交给引擎自己生成（与 glTF/OBJ 路径一致）。
    static bool create_texture_from_dds(VulkanglTFModel::Image& image, const std::filesystem::path& file, std::string& error) {
        const std::vector<uint8_t> bytes = read_file_bytes(file);
        if (bytes.empty()) {
            error = "读不出文件";
            return false;
        }
        std::vector<uint8_t> rgba;
        uint32_t width = 0, height = 0;
        std::string format;
        if (!DdsImage::decode(bytes.data(), bytes.size(), rgba, width, height, format, error))
            return false;
        image.texture.create(rgba.data(), VkExtent2D{ width, height }, k_texture_format, k_texture_format, true);
        return true;
    }

    // HDR（Radiance）：stb 解成 32F 线性像素，再压成 16F 上传。
    // 为什么不用 Texture::load_file 的浮点路径：32F 在多数设备上不可线性过滤（采样与 mip 生成都会踩），
    // 而 Texture::load_file 的调试检查只接受 4 字节浮点；16F 是 Vulkan 保证可过滤的格式，故这里自己解、自己压。
    static bool create_texture_from_hdr(VulkanglTFModel::Image& image, const std::filesystem::path& file, std::string& error) {
        int width = 0, height = 0, channels = 0;
        float* pixels = stbi_loadf(file.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            error = "stb_image 解不出 HDR";
            return false;
        }
        std::vector<uint16_t> halves(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        for (size_t i = 0; i < halves.size(); i += 2) {
            const uint32_t packed = glm::packHalf2x16(glm::vec2(pixels[i], pixels[i + 1]));
            halves[i] = static_cast<uint16_t>(packed & 0xFFFFu);
            halves[i + 1] = static_cast<uint16_t>(packed >> 16);
        }
        stbi_image_free(pixels);
        const VkExtent2D extent{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
        image.texture.create(reinterpret_cast<const uint8_t*>(halves.data()), extent,
                             VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, true);
        return true;
    }

    static bool create_texture_from_stb_image(VulkanglTFModel::Image& image, const std::filesystem::path& file, std::string& error) {
        const VulkanFormatInfo format_info =
            VulkanCore::get_singleton().get_vulkan_device().get_format_info(k_texture_format);
        VkExtent2D extent{};
        std::unique_ptr<uint8_t[]> pixels = Texture::load_file(file.string().c_str(), extent, format_info);
        if (!pixels || extent.width == 0 || extent.height == 0) {
            error = "stb_image 解不出该文件";
            return false;
        }
        image.texture.create(pixels.get(), extent, k_texture_format, k_texture_format, true);
        return true;
    }

    // 按扩展名分派：.dds 走 BCn 解码，.hdr 走浮点路径，其余交给 stb_image。
    static bool create_texture_from_file(VulkanglTFModel::Image& image, const std::filesystem::path& file, std::string& error) {
        const std::string extension = lowercase_extension(file);
        if (extension == ".dds")
            return create_texture_from_dds(image, file, error);
        if (extension == ".hdr")
            return create_texture_from_hdr(image, file, error);
        return create_texture_from_stb_image(image, file, error);
    }

    // 嵌入式贴图的"压缩"形态：pcData 是整段文件字节（png/jpg/...），mWidth 是字节数。
    static bool create_texture_from_memory(VulkanglTFModel::Image& image, const uint8_t* data, size_t size, std::string& error) {
        // 嵌入式 DDS 也走 BCn 解码（stb_image 不认 DDS）。
        if (DdsImage::is_dds(data, size)) {
            std::vector<uint8_t> rgba;
            uint32_t width = 0, height = 0;
            std::string format;
            if (!DdsImage::decode(data, size, rgba, width, height, format, error))
                return false;
            image.texture.create(rgba.data(), VkExtent2D{ width, height }, k_texture_format, k_texture_format, true);
            return true;
        }
        const VulkanFormatInfo format_info =
            VulkanCore::get_singleton().get_vulkan_device().get_format_info(k_texture_format);
        VkExtent2D extent{};
        std::unique_ptr<uint8_t[]> pixels = Texture::load_file(data, size, extent, format_info);
        if (!pixels || extent.width == 0 || extent.height == 0) {
            error = "stb_image 解不出该嵌入式贴图";
            return false;
        }
        image.texture.create(pixels.get(), extent, k_texture_format, k_texture_format, true);
        return true;
    }

    // 嵌入式贴图的"未压缩"形态：pcData 是 RGBA 纹素数组。
    static bool create_texture_from_texels(VulkanglTFModel::Image& image, const aiTexture& texture) {
        const VkExtent2D extent{ texture.mWidth, texture.mHeight };
        if (extent.width == 0 || extent.height == 0)
            return false;
        std::vector<uint8_t> pixels(static_cast<size_t>(extent.width) * extent.height * 4);
        for (size_t i = 0; i < pixels.size() / 4; ++i) {
            const aiTexel& texel = texture.pcData[i];
            pixels[i * 4 + 0] = texel.r;
            pixels[i * 4 + 1] = texel.g;
            pixels[i * 4 + 2] = texel.b;
            pixels[i * 4 + 3] = texel.a;
        }
        image.texture.create(pixels.data(), extent, k_texture_format, k_texture_format, true);
        return true;
    }

    static bool append_embedded_texture(VulkanglTFModel& model, const aiTexture& texture, std::string& error) {
        VulkanglTFModel::Image image;
        const bool compressed = texture.mHeight == 0;   // assimp 约定：mHeight == 0 时 mWidth 是字节数
        const bool created = compressed
            ? create_texture_from_memory(image, reinterpret_cast<const uint8_t*>(texture.pcData), texture.mWidth, error)
            : create_texture_from_texels(image, texture);
        if (!created)
            return false;
        model.images.push_back(std::move(image));
        return true;
    }

    // 按文件名在嵌入贴图里找一个（FBX 把媒体嵌进文件时，材质里可能仍写着原始文件名）。
    static const aiTexture* find_embedded_texture(const aiScene& scene, const std::filesystem::path& file) {
        const std::string wanted = lowercase_string(file.filename().string());
        for (uint32_t i = 0; i < scene.mNumTextures; ++i) {
            const aiTexture* texture = scene.mTextures[i];
            if (texture == nullptr)
                continue;
            if (lowercase_string(std::filesystem::path(texture->mFilename.C_Str()).filename().string()) == wanted)
                return texture;
        }
        return nullptr;
    }

    // 嵌入贴图的格式提示（"jpg"/"png"/...）决定是否解码；DDS 这类直接跳过。
    static bool is_supported_embedded_format(const aiTexture& texture) {
        std::string extension = texture.achFormatHint;
        if (extension.empty())
            return true;    // 没有提示就交给 stb_image 试一次（压缩格式自带魔数）
        if (extension[0] != '.')
            extension.insert(extension.begin(), '.');
        return is_supported_texture_extension(lowercase_extension(extension));
    }

    static bool append_embedded_texture_by_index(const aiScene& scene, uint32_t index, VulkanglTFModel& model, std::string& error) {
        if (index >= scene.mNumTextures || scene.mTextures[index] == nullptr) {
            error = "嵌入贴图索引越界";
            return false;
        }
        return append_embedded_texture(model, *scene.mTextures[index], error);
    }

    // 每个材质最多一张 base color 贴图：成功就追加一个 Image/Texture 并指向它，
    // 失败（或本来就是 DDS 这类引擎不认的格式）保持指向 images[0] 的白色兜底。
    // 每个材质最多一张 base color 贴图：成功就追加一个 Image/Texture 并指向它，
    // 失败（或本来就是 DDS 这类引擎不认的格式）保持指向 images[0] 的白色兜底。
    static void load_textures(const aiScene& scene,
                              const std::filesystem::path& model_dir,
                              VulkanglTFModel& model,
                              const Options& options,
                              Stats& stats) {
        for (uint32_t i = 0; i < scene.mNumMaterials; ++i) {
            const aiMaterial* material = scene.mMaterials[i];
            if (material == nullptr)
                continue;

            aiString texture_path;
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texture_path) != AI_SUCCESS &&
                material->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path) != AI_SUCCESS)
                continue;

            // 1) 嵌入式贴图："*<index>" 形式
            bool created = false;
            std::string decode_error;
            std::string extension_hint;    // 统计 dds/hdr 用（外部文件看扩展名，嵌入式看格式提示）
            if (texture_path.length > 0 && texture_path.data[0] == '*') {
                const int index = std::atoi(texture_path.C_Str() + 1);
                if (index < 0 || static_cast<uint32_t>(index) >= scene.mNumTextures) {
                    ++stats.skipped_texture_count;
                    warn(stats, options, std::format("材质 {} 引用了不存在的嵌入式贴图 {}", i, texture_path.C_Str()));
                    continue;
                }
                if (!is_supported_embedded_format(*scene.mTextures[index])) {
                    ++stats.skipped_texture_count;
                    warn(stats, options, std::format("跳过 {} 嵌入式贴图（材质 {}）", scene.mTextures[index]->achFormatHint, i));
                    continue;
                }
                extension_hint = "." + lowercase_string(scene.mTextures[index]->achFormatHint);
                created = append_embedded_texture_by_index(scene, static_cast<uint32_t>(index), model, decode_error);
            }
            else {
                // 2) 外部文件：先看扩展名，再看文件在不在（不在就不要进 stb_image，避免刷错误日志）
                const std::filesystem::path file = resolve_texture_file(model_dir, texture_path.C_Str());
                const std::string extension = lowercase_extension(file);
                extension_hint = extension;
                if (!is_supported_texture_extension(extension)) {
                    ++stats.skipped_texture_count;
                    warn(stats, options, std::format("跳过 {} 贴图（材质 {}）：{}",
                                                     extension.empty() ? "(未知)" : extension, i,
                                                     file.filename().string()));
                    continue;
                }
                if (!std::filesystem::exists(file)) {
                    // FBX 把媒体嵌进文件、但材质里保留原始文件名的情况：按文件名回落到嵌入贴图
                    const aiTexture* embedded = find_embedded_texture(scene, file);
                    if (embedded != nullptr && is_supported_embedded_format(*embedded)) {
                        created = append_embedded_texture(model, *embedded, decode_error);
                    }
                    else {
                        ++stats.skipped_texture_count;
                        warn(stats, options, std::format("外部贴图不存在（材质 {}）：{}", i, file.filename().string()));
                        continue;
                    }
                }
                else {
                    VulkanglTFModel::Image image;
                    created = create_texture_from_file(image, file, decode_error);
                    if (created)
                        model.images.push_back(std::move(image));
                }
            }

            if (!created) {
                ++stats.skipped_texture_count;
                warn(stats, options, std::format("贴图解码失败（材质 {}）：{} {} {}", i, texture_path.C_Str(),
                                                 extension_hint, decode_error));
                continue;
            }

            VulkanglTFModel::Texture texture;
            texture.image_index = static_cast<uint32_t>(model.images.size() - 1);
            model.textures.push_back(texture);
            model.materials[i].base_color_texture_index = static_cast<uint32_t>(model.textures.size() - 1);
            ++stats.texture_count;
            if (extension_hint == ".dds")
                ++stats.dds_texture_count;
            else if (extension_hint == ".hdr")
                ++stats.hdr_texture_count;
        }
    }

    // -------------------------------------------------------------- 材质/几何

    // 统计每个材质引用了哪些贴图槽：既用来回答"贴图是否加载完全"，也给 PBR 阶段定范围。
    static void count_texture_slots(const aiScene& scene, Stats& stats) {
        for (uint32_t i = 0; i < scene.mNumMaterials; ++i) {
            const aiMaterial* material = scene.mMaterials[i];
            if (material == nullptr)
                continue;
            aiString path;
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS)
                ++stats.slots.base_color;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
                ++stats.slots.diffuse;
            if (material->GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS)
                ++stats.slots.normal;
            if (material->GetTexture(aiTextureType_METALNESS, 0, &path) == AI_SUCCESS)
                ++stats.slots.metallic;
            if (material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &path) == AI_SUCCESS)
                ++stats.slots.roughness;
            if (material->GetTexture(aiTextureType_EMISSIVE, 0, &path) == AI_SUCCESS)
                ++stats.slots.emissive;
            if (material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &path) == AI_SUCCESS)
                ++stats.slots.ambient_occlusion;
            if (material->GetTexture(aiTextureType_SPECULAR, 0, &path) == AI_SUCCESS)
                ++stats.slots.specular;
            if (material->GetTexture(aiTextureType_SHININESS, 0, &path) == AI_SUCCESS)
                ++stats.slots.glossiness;
            if (material->GetTexture(aiTextureType_LIGHTMAP, 0, &path) == AI_SUCCESS)
                ++stats.slots.lightmap;
            if (material->GetTexture(aiTextureType_REFLECTION, 0, &path) == AI_SUCCESS)
                ++stats.slots.reflection;
            if (material->GetTexture(aiTextureType_UNKNOWN, 0, &path) == AI_SUCCESS)
                ++stats.slots.unknown;
        }
    }
    static void load_materials(const aiScene& scene, VulkanglTFModel& model) {
        model.materials.resize(scene.mNumMaterials);
        for (uint32_t i = 0; i < scene.mNumMaterials; ++i) {
            const aiMaterial* material = scene.mMaterials[i];
            aiColor4D base_color(1.0f, 1.0f, 1.0f, 1.0f);
            if (material == nullptr ||
                (material->Get(AI_MATKEY_BASE_COLOR, base_color) != AI_SUCCESS &&
                 material->Get(AI_MATKEY_COLOR_DIFFUSE, base_color) != AI_SUCCESS))
                base_color = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
            model.materials[i].base_color_factor = glm::vec4(base_color.r, base_color.g, base_color.b, base_color.a);
            model.materials[i].base_color_texture_index = 0;
        }
        // 没有材质的资产（例如裸 PLY）也要有一个默认材质，绘制时才能安全索引。
        // 注意 Material::base_color_texture_index 没有默认值，必须显式写成白色兜底。
        if (model.materials.empty()) {
            model.materials.emplace_back();
            model.materials.front().base_color_texture_index = 0;
        }
    }

    static void load_meshes(const aiScene& scene,
                            VulkanglTFModel& model,
                            std::vector<VulkanglTFModel::Vertex>& vertex_buffer,
                            std::vector<uint32_t>& index_buffer,
                            Stats& stats) {
        for (uint32_t m = 0; m < scene.mNumMeshes; ++m) {
            const aiMesh* mesh = scene.mMeshes[m];
            if (mesh == nullptr || mesh->mNumFaces == 0)
                continue;

            const uint32_t vertex_start = static_cast<uint32_t>(vertex_buffer.size());
            const uint32_t first_index = static_cast<uint32_t>(index_buffer.size());

            for (uint32_t v = 0; v < mesh->mNumVertices; ++v) {
                VulkanglTFModel::Vertex vertex{};
                vertex.pos = glm::vec3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
                if (!stats.has_bounds) {
                    stats.bounds_min = vertex.pos;
                    stats.bounds_max = vertex.pos;
                    stats.has_bounds = true;
                }
                else {
                    stats.bounds_min = glm::min(stats.bounds_min, vertex.pos);
                    stats.bounds_max = glm::max(stats.bounds_max, vertex.pos);
                }
                vertex.normal = mesh->HasNormals()
                    ? glm::normalize(glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z))
                    : glm::vec3(0.0f);
                vertex.uv = mesh->HasTextureCoords(0)
                    ? glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
                    : glm::vec2(0.0f);
                vertex.color = mesh->HasVertexColors(0)
                    ? glm::vec3(mesh->mColors[0][v].r, mesh->mColors[0][v].g, mesh->mColors[0][v].b)
                    : glm::vec3(1.0f);
                vertex_buffer.push_back(vertex);
            }

            // Triangulate 之后每个 face 都是三角形；非三角形的 face 直接跳过（理论上不会再出现）。
            for (uint32_t f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3)
                    continue;
                for (uint32_t k = 0; k < 3; ++k)
                    index_buffer.push_back(vertex_start + face.mIndices[k]);
            }

            const uint32_t index_count = static_cast<uint32_t>(index_buffer.size()) - first_index;
            if (index_count == 0)
                continue;

            auto* node = new VulkanglTFModel::Node{};
            node->matrix = glm::mat4(1.0f);     // PreTransformVertices：几何已经在世界空间
            node->parent = nullptr;
            VulkanglTFModel::Primitive primitive{};
            primitive.first_index = first_index;
            primitive.index_count = index_count;
            primitive.material_index = mesh->mMaterialIndex < model.materials.size()
                ? static_cast<int32_t>(mesh->mMaterialIndex)
                : 0;
            node->mesh.primitives.push_back(primitive);
            model.nodes.push_back(node);

            ++stats.mesh_count;
            ++stats.primitive_count;
        }

        stats.material_count = static_cast<uint32_t>(model.materials.size());
        stats.vertex_count = static_cast<uint32_t>(vertex_buffer.size());
        stats.index_count = static_cast<uint32_t>(index_buffer.size());
    }
};
