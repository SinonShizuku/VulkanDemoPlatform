// FrameGraph v1 单元测试：只覆盖没有 device 的部分——资源/pass 声明、依赖分析、
// 生命周期、barrier 规划、图 dump 与执行顺序。设备层（validation layer 零错误、
// 真实资源分配与 vkCmdPipelineBarrier2 录制）属于后续 executor 的测试范围。

#include "FrameGraph.h"
#include "TestHarness.h"

#include <string>

namespace {

framegraph::TextureDesc color_texture_desc(
    const char* name,
    framegraph::ImageUsage usage = (framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Sampled)) {
    framegraph::TextureDesc desc;
    desc.name = name;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.extent = VkExtent3D{ 1920u, 1080u, 1u };
    desc.usage = usage;
    return desc;
}

framegraph::TextureDesc depth_texture_desc(const char* name) {
    framegraph::TextureDesc desc;
    desc.name = name;
    desc.format = VK_FORMAT_D32_SFLOAT;
    desc.extent = VkExtent3D{ 1920u, 1080u, 1u };
    desc.usage = framegraph::ImageUsage::DepthStencilAttachment;
    return desc;
}

framegraph::BufferDesc buffer_desc(const char* name, framegraph::BufferUsage usage) {
    framegraph::BufferDesc desc;
    desc.name = name;
    desc.size = 4096;
    desc.usage = usage;
    return desc;
}

}  // namespace

// 线性依赖：Pass A -> Pass B，并完成 UNDEFINED -> ColorAttachment -> Sampled 的转换
TEST_CASE(linear_dependency_orders_passes_and_inserts_barriers) {
    framegraph::FrameGraph graph;
    const auto color = graph.create_texture(color_texture_desc("Color"));

    graph.add_graphics_pass("GBuffer").write(color, framegraph::usage::color_attachment_write());
    graph.add_graphics_pass("Lighting").read(color, framegraph::usage::sampled_read());

    CHECK(graph.compile());
    CHECK(graph.get_passes().size() == 2);
    CHECK(graph.get_passes()[0].name == "GBuffer");
    CHECK(graph.get_passes()[1].name == "Lighting");

    const auto& gbuffer_barriers = graph.get_passes()[0].image_barriers;
    CHECK(gbuffer_barriers.size() == 1);
    CHECK(gbuffer_barriers[0].old_layout == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(gbuffer_barriers[0].new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(gbuffer_barriers[0].reason == "first use");
    CHECK(gbuffer_barriers[0].layout_transition);

    const auto& lighting_barriers = graph.get_passes()[1].image_barriers;
    CHECK(lighting_barriers.size() == 1);
    CHECK(lighting_barriers[0].old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(lighting_barriers[0].new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(lighting_barriers[0].reason == "read-after-write");
    CHECK(lighting_barriers[0].src_stages == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(lighting_barriers[0].dst_stages == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    CHECK(lighting_barriers[0].src_access == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    CHECK(lighting_barriers[0].dst_access == VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

// 分支依赖：A -> B、A -> C，两个消费者之间没有依赖，保持声明顺序且只插入一次读后读 barrier
TEST_CASE(independent_branches_keep_declaration_order) {
    framegraph::FrameGraph graph;
    const auto color = graph.create_texture(color_texture_desc("Color"));

    graph.add_graphics_pass("Producer").write(color, framegraph::usage::color_attachment_write());
    graph.add_graphics_pass("ConsumerA").read(color, framegraph::usage::sampled_read());
    graph.add_graphics_pass("ConsumerB").read(color, framegraph::usage::sampled_read());

    CHECK(graph.compile());
    CHECK(graph.get_passes().size() == 3);
    CHECK(graph.get_passes()[0].name == "Producer");
    CHECK(graph.get_passes()[1].name == "ConsumerA");
    CHECK(graph.get_passes()[2].name == "ConsumerB");
    CHECK(graph.get_passes()[1].image_barriers.size() == 1);
    CHECK(graph.get_passes()[2].image_barriers.empty());
}

// 双写冲突：两个 pass 写同一张 storage image，后写需要 WAW barrier（layout 不变）
TEST_CASE(write_after_write_serializes_storage_image_access) {
    framegraph::FrameGraph graph;
    framegraph::TextureDesc desc = color_texture_desc("Accumulator", framegraph::ImageUsage::Storage);
    desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    const auto accumulator = graph.create_texture(desc);

    graph.add_compute_pass("Clear").write(accumulator, framegraph::usage::storage_write());
    graph.add_compute_pass("Blur").write(accumulator, framegraph::usage::storage_write());

    CHECK(graph.compile());
    CHECK(graph.get_passes().size() == 2);

    const auto& barriers = graph.get_passes()[1].image_barriers;
    CHECK(barriers.size() == 1);
    CHECK(barriers[0].reason == "write-after-write");
    CHECK(barriers[0].old_layout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(barriers[0].new_layout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(!barriers[0].layout_transition);
    CHECK(barriers[0].src_access == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    CHECK(barriers[0].dst_access == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

// 写后读：WAR 只需要执行依赖，srcAccess 应为 0，但 layout 转换仍然要做
TEST_CASE(write_after_read_uses_execution_dependency_only) {
    framegraph::FrameGraph graph;
    const auto color = graph.import_texture(color_texture_desc("ImportedColor"),
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    graph.add_compute_pass("ReadColor")
        .read(color, framegraph::usage::sampled_read().with_stages(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT));
    graph.add_graphics_pass("RewriteColor").write(color, framegraph::usage::color_attachment_write());

    CHECK(graph.compile());
    CHECK(graph.get_passes()[0].name == "ReadColor");
    CHECK(graph.get_passes()[0].image_barriers.empty());

    const auto& barriers = graph.get_passes()[1].image_barriers;
    CHECK(barriers.size() == 1);
    CHECK(barriers[0].reason == "write-after-read");
    CHECK(barriers[0].src_access == VK_ACCESS_2_NONE);
    CHECK(barriers[0].src_stages == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(barriers[0].dst_stages == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(barriers[0].old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(barriers[0].new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(barriers[0].dst_access == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

// 导入资源：使用调用方声明的初始状态，并在 pass 之间生成 RAW 与 present 转换
TEST_CASE(imported_resource_uses_declared_initial_state) {
    framegraph::FrameGraph graph;
    framegraph::TextureDesc desc = color_texture_desc(
        "SwapchainImage", framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Present);
    desc.format = VK_FORMAT_B8G8R8A8_UNORM;
    const auto swapchain = graph.import_texture(desc,
                                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    graph.add_graphics_pass("Scene").write(swapchain, framegraph::usage::color_attachment_write());
    graph.add_transfer_pass("Present").write(swapchain, framegraph::usage::present());

    CHECK(graph.compile());
    const auto& scene_barriers = graph.get_passes()[0].image_barriers;
    CHECK(scene_barriers.size() == 1);
    CHECK(scene_barriers[0].reason == "imported initial state");
    CHECK(scene_barriers[0].old_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    CHECK(scene_barriers[0].new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(scene_barriers[0].src_stages == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    CHECK(scene_barriers[0].src_access == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    const auto& present_barriers = graph.get_passes()[1].image_barriers;
    CHECK(present_barriers.size() == 1);
    CHECK(present_barriers[0].old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CHECK(present_barriers[0].new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    CHECK(present_barriers[0].src_access == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    CHECK(present_barriers[0].dst_stages == VK_PIPELINE_STAGE_2_NONE);
    CHECK(present_barriers[0].dst_access == VK_ACCESS_2_NONE);
}

// 读后读且 layout 不变：不插入 barrier，但依然计入使用次数
TEST_CASE(read_after_read_is_elided) {
    framegraph::FrameGraph graph;
    const auto albedo = graph.import_texture(color_texture_desc("Albedo", framegraph::ImageUsage::Sampled),
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    graph.add_graphics_pass("Forward").read(albedo, framegraph::usage::sampled_read());
    graph.add_graphics_pass("Post").read(albedo, framegraph::usage::sampled_read());

    CHECK(graph.compile());
    CHECK(graph.get_passes()[0].image_barriers.empty());
    CHECK(graph.get_passes()[1].image_barriers.empty());
    CHECK(graph.get_stats().image_barriers == 0);
    CHECK(graph.get_stats().elided_barriers == 2);
    CHECK(graph.get_resources()[albedo.index].use_count == 2);
}

// 同一 pass 内对同一资源的多次访问合并成一次 barrier
TEST_CASE(usages_within_one_pass_are_merged_into_one_barrier) {
    framegraph::FrameGraph graph;
    framegraph::TextureDesc desc = color_texture_desc("Histogram", framegraph::ImageUsage::Storage);
    desc.format = VK_FORMAT_R32_UINT;
    const auto histogram = graph.create_texture(desc);

    graph.add_compute_pass("Reduce")
        .write(histogram, framegraph::usage::storage_write())
        .read(histogram, framegraph::usage::storage_read());

    CHECK(graph.compile());
    CHECK(graph.get_passes()[0].image_barriers.size() == 1);
    CHECK(graph.get_passes()[0].image_barriers[0].reason == "first use");
    CHECK(graph.get_passes()[0].image_barriers[0].dst_access ==
          (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
    CHECK(graph.get_stats().image_barriers == 1);
    CHECK(graph.get_stats().elided_barriers == 1);
}

// buffer：RAW 生成 buffer barrier，buffer 没有 layout 因此首次使用不需要 barrier
TEST_CASE(buffer_read_after_write_inserts_buffer_barrier) {
    framegraph::FrameGraph graph;
    const auto indirect = graph.create_buffer(
        buffer_desc("IndirectDrawBuffer", framegraph::BufferUsage::Storage | framegraph::BufferUsage::Indirect));

    graph.add_compute_pass("Cull").write(indirect, framegraph::usage::storage_buffer_write());
    graph.add_graphics_pass("Draw").read(indirect, framegraph::usage::indirect_read());

    CHECK(graph.compile());
    CHECK(graph.get_passes()[0].buffer_barriers.empty());

    const auto& barriers = graph.get_passes()[1].buffer_barriers;
    CHECK(barriers.size() == 1);
    CHECK(barriers[0].reason == "read-after-write");
    CHECK(barriers[0].src_stages == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    CHECK(barriers[0].dst_stages == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    CHECK(barriers[0].src_access == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    CHECK(barriers[0].dst_access == VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    CHECK(graph.get_stats().buffer_barriers == 1);
}

// 生命周期：first/last use 按编译后的执行顺序统计
TEST_CASE(resource_lifetimes_track_first_and_last_use) {
    framegraph::FrameGraph graph;
    const auto color = graph.create_texture(color_texture_desc("Color"));
    const auto depth = graph.create_texture(depth_texture_desc("Depth"));

    graph.add_graphics_pass("GBuffer").write(color, framegraph::usage::color_attachment_write());
    graph.add_graphics_pass("Shadow").write(depth, framegraph::usage::depth_stencil_write());
    graph.add_graphics_pass("Resolve").read(color, framegraph::usage::sampled_read());

    CHECK(graph.compile());
    CHECK(graph.get_execution_order().size() == 3);

    const auto& resources = graph.get_resources();
    CHECK(resources[color.index].first_use == 0);
    CHECK(resources[color.index].last_use == 2);
    CHECK(resources[color.index].use_count == 2);
    CHECK(resources[depth.index].first_use == 1);
    CHECK(resources[depth.index].last_use == 1);
    CHECK(resources[depth.index].use_count == 1);

    // 未被任何 pass 使用的资源不参与生命周期统计
    const auto unused = graph.create_texture(depth_texture_desc("UnusedDepth"));
    CHECK(graph.compile());
    CHECK(graph.get_resources()[unused.index].first_use == framegraph::invalid_index);
    CHECK(graph.get_resources()[unused.index].last_use == framegraph::invalid_index);
}

// 深度附件：barrier 的 aspect 必须来自格式，而不是默认 color
TEST_CASE(depth_attachment_barrier_uses_depth_aspect) {
    framegraph::FrameGraph graph;
    const auto depth = graph.create_texture(depth_texture_desc("Depth"));

    graph.add_graphics_pass("Shadow").write(depth, framegraph::usage::depth_stencil_write());

    CHECK(graph.compile());
    const auto& barriers = graph.get_passes()[0].image_barriers;
    CHECK(barriers.size() == 1);
    CHECK(barriers[0].range.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(barriers[0].range.levelCount == 1);
    CHECK(barriers[0].range.layerCount == 1);
    CHECK(barriers[0].new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

// 非法依赖环：compile() 必须报错，且错误信息里包含涉及的 pass
TEST_CASE(dependency_cycle_is_rejected) {
    framegraph::FrameGraph graph;
    const auto color = graph.create_texture(color_texture_desc("Color"));

    auto producer = graph.add_graphics_pass("Producer").write(color, framegraph::usage::color_attachment_write());
    auto consumer = graph.add_graphics_pass("Consumer").read(color, framegraph::usage::sampled_read());
    producer.depends_on(consumer);

    CHECK(!graph.compile());
    CHECK(graph.get_error().find("Producer") != std::string::npos);
    CHECK(graph.get_error().find("Consumer") != std::string::npos);
    CHECK(graph.get_passes().empty());
}

// 资源声明缺少 pass 用到的用途时必须报错
TEST_CASE(usage_missing_in_texture_description_is_rejected) {
    framegraph::FrameGraph graph;
    const auto color = graph.create_texture(color_texture_desc("Color", framegraph::ImageUsage::ColorAttachment));

    graph.add_graphics_pass("GBuffer").write(color, framegraph::usage::color_attachment_write());
    graph.add_graphics_pass("Lighting").read(color, framegraph::usage::sampled_read());

    CHECK(!graph.compile());
    CHECK(graph.get_error().find("Sampled") != std::string::npos);
    CHECK(graph.get_error().find("Color") != std::string::npos);
}

// transient 资源在写入之前被读取时必须报错
TEST_CASE(reading_transient_texture_before_write_is_rejected) {
    framegraph::FrameGraph graph;
    const auto transient_texture =
        graph.create_texture(color_texture_desc("Transient", framegraph::ImageUsage::Sampled));

    graph.add_compute_pass("Sampler").read(transient_texture, framegraph::usage::sampled_read());

    CHECK(!graph.compile());
    CHECK(graph.get_error().find("Transient") != std::string::npos);
}

// read()/write() 与 usage 语义不匹配时必须报错
TEST_CASE(write_with_read_only_usage_is_rejected) {
    framegraph::FrameGraph graph;
    const auto color = graph.create_texture(color_texture_desc("Color"));

    graph.add_graphics_pass("Bad").write(color, framegraph::usage::sampled_read());

    CHECK(!graph.compile());
    CHECK(graph.get_error().find("write") != std::string::npos);
}

// 句柄校验：没有创建过资源就使用句柄必须报错
TEST_CASE(invalid_resource_handle_is_rejected) {
    framegraph::FrameGraph graph;
    const framegraph::ResourceHandle bogus{ 0u, framegraph::ResourceKind::Texture };

    graph.add_graphics_pass("Bad").read(bogus, framegraph::usage::sampled_read());

    CHECK(!graph.compile());
    CHECK(graph.get_error().find("'Bad'") != std::string::npos);
}

// 执行顺序：begin 钩子先于 pass 回调，且回调能拿到本 pass 的 barrier 计划
TEST_CASE(execute_calls_begin_hook_then_pass_callbacks_in_order) {
    framegraph::FrameGraph graph;
    const auto color = graph.create_texture(color_texture_desc("Color"));
    std::string trace;

    graph.add_graphics_pass("GBuffer")
        .write(color, framegraph::usage::color_attachment_write())
        .execute([&trace](framegraph::PassContext& context) {
            trace += "GBuffer(" + std::to_string(context.image_barriers.size()) + ");";
        });
    graph.add_graphics_pass("Lighting")
        .read(color, framegraph::usage::sampled_read())
        .execute([&trace](framegraph::PassContext& context) {
            trace += "Lighting(" + std::to_string(context.image_barriers.size()) + ");";
        });
    graph.set_begin_pass_callback([&trace](framegraph::PassContext& context) {
        trace += "begin:" + context.pass->name + ";";
    });

    CHECK(graph.compile());
    graph.execute();
    CHECK(trace == "begin:GBuffer;GBuffer(1);begin:Lighting;Lighting(1);");
}

// 图 dump：包含 pass、资源生命周期与 barrier 来源，便于人工核对 barrier 的成因
TEST_CASE(dump_reports_passes_barriers_and_lifetimes) {
    framegraph::FrameGraph graph;
    graph.set_name("Dump");
    const auto color = graph.create_texture(color_texture_desc("Color"));

    graph.add_graphics_pass("GBuffer").write(color, framegraph::usage::color_attachment_write());
    graph.add_graphics_pass("Lighting").read(color, framegraph::usage::sampled_read());

    CHECK(graph.compile());
    const std::string dump = graph.dump();
    CHECK(dump.find("FrameGraph 'Dump'") != std::string::npos);
    CHECK(dump.find("image_barriers=2") != std::string::npos);
    CHECK(dump.find("read-after-write") != std::string::npos);
    CHECK(dump.find("lifetime=[0..1]") != std::string::npos);
}

// reset()：清空资源、pass 与编译结果，便于每帧重建
TEST_CASE(reset_clears_graph) {
    framegraph::FrameGraph graph;
    graph.set_name("Frame");
    const auto color = graph.create_texture(color_texture_desc("Color"));
    graph.add_graphics_pass("GBuffer").write(color, framegraph::usage::color_attachment_write());

    CHECK(graph.compile());
    CHECK(graph.is_compiled());

    graph.reset();
    CHECK(!graph.is_compiled());
    CHECK(graph.get_passes().empty());
    CHECK(graph.get_resources().empty());
    CHECK(graph.get_execution_order().empty());
    CHECK(graph.get_error().empty());
    CHECK(graph.get_name().empty());
}

int main() {
    return testh::Runner::instance().run();
}