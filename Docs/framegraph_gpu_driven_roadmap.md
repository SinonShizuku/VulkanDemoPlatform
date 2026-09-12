# VulkanRenderer 重构路线：FrameGraph + Synchronization + GPU-driven Rendering + RT + Benchmark

> 文档状态：设计基线、实施路线与已验证进度
> 建立日期：2026-09-11
> 最近更新：2026-09-11
> 项目路径：`D:/VulkanDemoPlatform`（当前工作副本；早期记录的 `D:/myself/GraphicLearning/Graphic_api/VulkanRenderer` 已不是本机工作目录）
> 上游基础：https://github.com/SaschaWillems/Vulkan
> Canonical repository：https://github.com/SinonShizuku/VulkanDemoPlatform
> 本地 Git remote：`https://github.com/SinonShizuku/VulkanDemoPlatform`（2026-09-11 用 `git remote -v` 核实；`SinonShizuku/VulkanRenderer` 会重定向到该仓库）
> P0 代码基线提交：`961065f`、`d2be67e`、`d04593f`
> FrameGraph v1 第一切片：见 §5.6（已合并到 `master`，提交 `e1ab97a`）
> 目标岗位：游戏引擎开发 / 图形渲染 / GPU 渲染工程 / 三维视觉工程

---

## 1. 结论摘要

本项目当前更接近“基于 Vulkan 的教学型 Demo 集合”，而不是一个具有明确技术主张的现代渲染器。

后续收敛目标应为：

> **Vulkan 1.3 FrameGraph + Synchronization + GPU-driven Rendering Lab**

硬件光追作为同一渲染路径中的可选高级特性接入，而不是单独复制一个 Ray Tracing 示例。

竞争力优先级：

```text
FrameGraph + 正确 Synchronization
        >
GPU-driven Rendering + 可量化数据
        >
完整 Benchmark / 正确性验证
        >
硬件光追
```

判断标准：

- 如果 FrameGraph、Synchronization、GPU-driven 都以同一套场景和 Pass 体系真实落地，并有可复现 benchmark，项目足以支撑游戏引擎/图形渲染方向的校招竞争力。
- 如果仍然做成四个互不相干的 Demo，即使包含光追，也只是“功能数量更多”，不能形成深度证明。
- 如果目标为 NVIDIA / AMD / Intel 图形驱动或高端渲染研发，还需要补充跨厂商验证、性能分析和更复杂的 GPU-driven 架构。
- 光追是加分项，不是核心门槛。建议先做 Ray Query Shadow，再考虑 Reflection 和 Denoise，不要一开始做多跳 Path Tracing。

---

## 2. 已核实的仓库现状

### 2.1 构建环境

当前推荐构建目录：

```text
out/build/windows-ninja-debug
out/build/windows-ninja-release
```

验证配置：

- Generator：Ninja
- 编译器：MSVC 14.50
- C++ 标准：C++23
- Vulkan SDK：`1.4.313.1`
- GPU：`AMD Radeon(TM) Graphics`，设备 API `1.3.217`

推荐命令：

```powershell
.\scripts\build.ps1 -Bootstrap -Configuration Debug
.\scripts\build.ps1 -Configuration Release
```

已落地的构建改动（提交 `961065f`）：

- 新增 `scripts/build.ps1`，自动加载 Visual Studio 环境并执行 CMake Presets；
- 新增 `CMakePresets.json`，提供 Debug / Release 配置；
- 使用 `find_package(Vulkan 1.3 REQUIRED)`，不再硬编码本机 `VULKAN_SDK_PATH`；
- 新增 `scripts/bootstrap-dependencies.ps1`，固定 GLFW 3.4、GLM 1.0.1、Dear ImGui 1.92.3、tinygltf 2.9.6、KTX 3.0.1 和 stb；
- shaderc 改为使用 Vulkan SDK 自带库目录，不再依赖手工复制的 `External/lib`；
- 干净工作区已完成 Debug / Release 构建，集成后的当前工作区完成 Debug 构建和 5 秒启动验证。

### 2.1.1 构建系统的一个坑（已修）

本地化（代码页 936）的 MSVC 输出会让 CMake/Ninja 的 dyndep 解析不到头文件依赖，表现为「只改头文件不重编译」，从而验证到旧二进制甚至定位错问题。`scripts/build.ps1` 现在显式设置 `$env:VSLANG = "1033"`；用本地化前缀配置过的构建目录需要删除后重新配置才会生效。

### 2.2 WIP 与工作区状态

P0 构建和设备初始化改动已经提交到 `master`，当前工作区干净。上一轮遗留的“构建期编译着色器”未提交改动，先按“先隔离、再重构”的原则归档到独立分支，随后 fast-forward 合并回 `master`，没有混入 FrameGraph 提交：

```text
554efb5  build: compile shaders at build time with glslc
  变更文件：CMakeLists.txt、scripts/build.ps1、Readme.md
  原分支：codex/build-shader-pipeline（已合并，可删除或保留作记录）
```

处理原则：

- 不在 FrameGraph 重构中直接覆盖既有 WIP；
- 新重构使用独立分支与独立文件范围，不把构建/着色器改动混入 FrameGraph 提交；
- `master` 现在具备构建期 `glslc` 规则：`Shader/**/*.shader` 在构建时增量编译为同名 `.spv`，Debug 还会把 `shaderc_sharedd.dll` 自动部署到可执行文件旁；
- 早期文档列出的部分 PBR/IBL WIP 文件（例如 `Demos/PBR/IBL.h`、`Interaction/Material.h`、`Interaction/Texture.cpp`）在当前工作区中已不存在，这里保留为历史记录。

### 2.3 本机 GPU 与 Vulkan 能力

当前本机 GPU：

```text
AMD Radeon(TM) Graphics
Vulkan API: 1.3.217
Device Type: Integrated GPU
```

本机已检测到：

```text
dynamicRendering      = true
synchronization2      = true
descriptorIndexing    = true
bufferDeviceAddress   = true
VK_EXT_descriptor_indexing
VK_KHR_buffer_device_address
VK_KHR_synchronization2
```

因此可以在本机开发和验证：

- FrameGraph；
- Synchronization 2；
- Bindless Descriptor；
- GPU-driven Culling；
- Indirect Draw；
- Benchmark 基础设施；
- Validation Layer 检查。

本机未检测到：

```text
VK_KHR_acceleration_structure
VK_KHR_ray_query
VK_KHR_ray_tracing_pipeline
```

因此：

- 光追代码可以编写，但必须运行时检测能力；
- 本机不能作为真实光追结果验证环境；
- RT Shadow / Reflection 必须在 RTX 机器上运行、截图并记录 benchmark；
- 缺少光追扩展时应允许程序关闭 RT 路径，而不是崩溃或返回错误。

### 2.4 Canonical repository（已确认）

本地 remote：

```text
git@github.com:SinonShizuku/VulkanRenderer.git
```

公开地址：

```text
https://github.com/SinonShizuku/VulkanDemoPlatform
```

已通过 GitHub API 和 `git ls-remote` 核实：`VulkanRenderer` 会重定向到 `VulkanDemoPlatform`，两者当时的 `master` 指向同一提交；公开地址可以作为项目 canonical repository。简历、Benchmark 报告和 Release 链接统一使用 `VulkanDemoPlatform`。

### 2.5 当前模块成熟度

| 模块 | 当前状态 | 说明 |
| --- | --- | --- |
| Vulkan RHI 封装 | 可用 | 已有 Device、Swapchain、Command、Memory、Descriptor、Pipeline 等封装 |
| glTF 加载 | 可用 | 已实现模型、节点、材质和 primitive 加载 |
| Shadow Mapping | 可用 | 已有 PCF、PCSS、VSSM 与 SAT compute passes |
| Deferred Rendering | 可用 | 已有 G-Buffer 与 composition 流程 |
| 构建可复现性 | 可用 | 固定依赖版本、CMake Presets、一键恢复和构建；提交 `961065f` |
| 设备 feature 初始化 | 已验证 | imageless framebuffer、dynamic rendering 在设备创建前显式启用；提交 `d2be67e` |
| PBR / IBL | WIP | 本地 IBL 代码尚未形成可运行 demo，且未注册到 DemoManager |
| FrameGraph | 核心 + executor 已落地 | 图编译、依赖、生命周期与 barrier 规划（§5.6，18 个单元测试）+ 设备端 executor（§5.7）；已接入 2 个 demo：`FrameGraphOffScreenTest`（图拥有离屏画布）、`glTFLoading`（图拥有 depth）；ShadowMapping / Deferred 迁移未开始 |
| Synchronization 2 | 部分实现 | 图路径按 stage2/access2 规划 barrier，executor 支持 `vkCmdPipelineBarrier2`；本机设备未启用该 feature，实跑走旧路径翻译分支；主循环仍是单 fence + 每帧等待 |
| Frames in Flight | 未实现 | 主循环每帧提交后立即等待 fence |
| GPU-driven Rendering | 未实现 | 无 bindless、compute culling、indirect draw 主路径 |
| Hardware RT | 未实现 | 本地无光追扩展，不能宣称已完成 |
| Benchmark | 不可用 | 目前只有窗口标题 FPS，无 CSV、P50/P95、GPU timestamp 或报告 |

### 2.6 当前核心架构问题

P0 已修复：

- 移除硬编码 Vulkan SDK 路径，改为 `find_package(Vulkan)`；
- 修复设备扩展和 feature 在 `vkCreateDevice()` 之后才配置的顺序错误；
- 修复 compute command pool 被创建到 graphics queue family 的错误；
- 不再默认启用设备支持的全部可选 feature，避免与 validation layer 的 `robustBufferAccessUpdateAfterBind` 规则冲突；
- 修复 shaderc 对手工复制 `External/lib` 的隐式依赖。

仍需处理：

1. 单个共享 fence，单个 frame in flight；
2. 提交后立即等待，CPU/GPU 无重叠；
3. 每个 Demo 自己创建和录制 command buffer；
4. 每个 Demo 自己手写 Barrier —— **部分解决**：`FrameGraphOffScreenTest`、`glTFLoading` 的 barrier 已由图规划并录制；ShadowMapping、Deferred 与 Vulkan Tests 系列仍在手写或依赖 render pass 隐式转换；
5. 资源状态没有统一追踪 —— **部分解决**：executor 已按图声明创建/复用真实 image、buffer，并跟踪 transient 纹理的跨帧 layout；swapchain image 仍归 RHI 的 render pass 管理（图的“外部同步资源”）；
6. 管线绑定传统 `VkRenderPass`；
7. 没有 transient resource 和 memory aliasing；
8. 没有统一的 GPU pass timing；
9. 没有 benchmark / correctness 基础设施；
10. 已有 Validation 错误：ImGui render pass 的 `LOAD` + `UNDEFINED` initialLayout；render-finished semaphore 跨 swapchain image 复用；图路径另触发 presentable image 的 acquire/layout 校验（需先把 swapchain 同步修好）。其中 pipeline `lineWidth=0` 已在 `602816f` 修复；
11. PBR/IBL 目前不能作为已完成能力对外陈述。

### 2.7 简历事实边界

当前可以写：

- Vulkan RHI 封装；
- glTF 加载；
- Shadow Mapping；
- PCF / PCSS / VSSM；
- Deferred Rendering；
- 可复现构建与固定依赖版本；
- Vulkan device feature 查询、扩展降级路径和显式启用；
- 上述能力的调试和 validation 分析；
- FrameGraph v1：资源/Pass/依赖建模、稳定拓扑排序、资源生命周期与 barrier 规划（device-free，含 18 个单元测试）。

当前不能写“已实现”：

- PBR / IBL；
- FrameGraph 驱动的渲染路径（当前只有 device-free 的图编译与 barrier 规划）；
- Synchronization 2；
- Frames in Flight；
- GPU-driven Rendering；
- Hardware Ray Tracing；
- 完整 Benchmark 体系；
- “已被开源项目合并”或“上游采用”。

如果代码来自 `SaschaWillems/Vulkan`，必须保留上游许可证和 attribution。当前仓库仍缺少明确的 `LICENSE` / `THIRD_PARTY_NOTICES`，这是公开发布前必须补齐的合规项。正式表述应使用：

```text
基于 SaschaWillems/Vulkan 重构并扩展了 XXX。
```

不能把上游已有实现直接表述成个人从零实现。

---

## 3. 项目目标与非目标

### 3.1 目标

- 支持 Vulkan 1.3；
- 使用 `VK_KHR_dynamic_rendering` 或 Vulkan 1.3 core dynamic rendering；
- 使用 Vulkan 1.3 `synchronization2`；
- 建立统一的 RenderGraph；
- 自动生成资源和 pass 之间的 synchronization；
- 支持 2-3 个 frames in flight；
- 支持 transient resource；
- 支持 bindless descriptor；
- 支持 GPU frustum culling；
- 支持 indirect draw；
- 可选支持 Hi-Z occlusion culling；
- 可选支持 RT Shadow / Reflection；
- 建立可复现 benchmark；
- 产生可解释的简历数据。

### 3.2 非目标

当前阶段不做：

- 通用 3D engine；
- 完整 ECS；
- 完整编辑器；
- Vulkan / D3D12 / Metal 多后端抽象；
- 全功能材质系统；
- 多跳 Path Tracing；
- ReSTIR DI/GI；
- 完整 Meshlet / Visibility Buffer；
- 全平台兼容矩阵；
- 在上游没有稳定时追求大量开源 PR。

---

## 4. 目标架构

```text
Application / Demo
        |
        v
Renderer Core
        |
        +-- FrameContextRing
        |     +-- per-frame command buffers
        |     +-- per-frame fences
        |     +-- per-frame semaphores
        |     +-- per-frame transient resources
        |
        +-- RenderGraph
        |     +-- resource declaration
        |     +-- import / transient resources
        |     +-- pass dependency graph
        |     +-- lifetime analysis
        |     +-- barrier generation
        |     +-- pass execution
        |
        +-- Vulkan RHI
        |     +-- Buffer / Image / Sampler
        |     +-- Pipeline / Descriptor
        |     +-- Command Buffer
        |     +-- Query / Synchronization
        |
        +-- Render Passes
        |     +-- Shadow / SAT
        |     +-- Deferred
        |     +-- GPU-driven
        |     +-- RT Shadow / Reflection
        |
        +-- Benchmark / Profiler
              +-- CPU timing
              +-- GPU timestamp
              +-- CSV / JSON
              +-- Markdown report
```

目录建议：

```text
VulkanBase/
  FrameGraph/
    RenderGraph.h
    RenderGraph.cpp
    RenderGraphTypes.h
    RenderGraphCompiler.cpp
    RenderGraphExecutor.cpp
    RenderGraphBarrier.cpp
  Renderer/
    FrameContext.h
    FrameContextRing.h
    FrameContextRing.cpp
  components/
    ... existing Vulkan wrappers

Benchmark/
  BenchmarkRunner.h
  BenchmarkRunner.cpp
  BenchmarkStats.h
  BenchmarkStats.cpp

Tests/
  FrameGraphTests/
  RenderGraphTests/
```

FrameGraph 核心应使用 `.h/.cpp`，不要继续全部堆在 header 中。
这样可以在没有 Vulkan device 的情况下对图编译、依赖分析和 barrier 规划做单元测试。

---

## 5. FrameGraph 设计

### 5.1 核心概念

- `RenderGraphResource`：资源句柄，不直接拥有底层 Vulkan handle；
- `ImportedResource`：swapchain image、已有图片、buffer、外部资源；
- `TransientResource`：由 graph 创建并在生命周期结束后回收；
- `Pass`：Graphics / Compute / Transfer；
- `ResourceUsage`：read / write / attachment / storage / uniform / indirect；
- `PipelineStage`：Vertex / Fragment / Compute / Transfer / ColorAttachment 等；
- `AccessFlags`：ShaderRead / ShaderWrite / ColorAttachmentWrite 等；
- `Layout`：UNDEFINED / GENERAL / COLOR_ATTACHMENT_OPTIMAL / DEPTH_STENCIL_ATTACHMENT_OPTIMAL / SHADER_READ_ONLY_OPTIMAL；
- `BarrierPlan`：由 graph compile 阶段生成的 synchronization 结果。

### 5.2 概念 API

```cpp
FrameGraph graph;

auto gbuffer = graph.createTexture({
    .name = "GBuffer",
    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
    .extent = { width, height, 1 },
    .usage = ImageUsage::ColorAttachment | ImageUsage::Sampled
});

auto depth = graph.createTexture({
    .name = "Depth",
    .format = VK_FORMAT_D32_SFLOAT,
    .extent = { width, height, 1 },
    .usage = ImageUsage::DepthAttachment | ImageUsage::Sampled
});

graph.addGraphicsPass("GBufferPass")
    .write(gbuffer, ColorAttachment)
    .write(depth, DepthAttachment)
    .execute([&](RenderGraphContext& ctx) {
        // record draw calls
    });

graph.addComputePass("GPUCull")
    .read(sceneBuffer)
    .write(indirectDrawBuffer)
    .execute([&](RenderGraphContext& ctx) {
        // dispatch compute culling
    });

graph.addGraphicsPass("LightingPass")
    .read(gbuffer, SampledImage)
    .write(output, ColorAttachment)
    .execute([&](RenderGraphContext& ctx) {
        // lighting / indirect draw
    });

graph.compile();
graph.execute(commandBuffer);
```

该 API 为方向性设计，实际命名和模板细节可在实现时调整。

### 5.3 编译流程

```text
add pass
    -> build resource usage
    -> topologically sort passes
    -> compute first/last use
    -> compute lifetimes
    -> allocate physical resources
    -> generate barriers
    -> execute
```

### 5.4 Barrier 生成规则

第一版规则：

1. 跟踪每个资源的最近一次 write；
2. 在执行 Pass 前，将资源转换到该 Pass 需要的 layout；
3. 只在依赖不安全时插入 barrier；
4. 合并同一 source/dst stage 的 barrier；
5. 支持 image / buffer barrier；
6. 支持 queue family ownership transfer；
7. 支持 external resource 初始状态；
8. 输出每帧 barrier 统计。

第二版再增加：

- redundant barrier elimination；
- read-after-read 合并；
- transient memory aliasing；
- async compute overlap analysis。

### 5.5 第一版必做测试

前 9 项已有 device-free 单元测试覆盖（`Tests/FrameGraphTests`）；最后一项需要设备端 executor，属于后续切片。

- 线性依赖：Pass A -> Pass B；（已覆盖）
- 分支依赖：A -> B，A -> C；（已覆盖）
- 双写冲突：A 写资源，B 写同一资源；（已覆盖）
- 读写依赖：A 写，B 读；（已覆盖）
- 写后写依赖：A 写，B 写；（已覆盖）
- layout 转换：ColorAttachment -> Sampled；（已覆盖）
- cycle detection：非法依赖环应报错；（已覆盖，另有显式依赖 `depends_on` 的环检测）
- resource lifetime：识别 first/last use；（已覆盖）
- imported resource：正确使用外部初始状态；（已覆盖）
- validation layer：所有测试场景零错误。（未覆盖：需要 executor 在设备上录制 barrier 后运行）

### 5.6 v1 第一切片：已实现状态

提交：`e1ab97a`（已 fast-forward 合并到 `master`）。这一切片只做“不接触 Vulkan 对象”的部分，因此可以在没有 GPU、没有 device 的情况下验证。

文件：

```text
VulkanBase/FrameGraph/
  FrameGraphTypes.h          资源描述、Usage、barrier 计划与统计类型（只依赖 vulkan.h 的枚举与位掩码）
  FrameGraph.h/.cpp          资源 / pass 声明、compile()、execute()、dump()
  FrameGraphCompiler.h/.cpp  依赖推导（RAW / WAR / WAW + 显式依赖）、稳定拓扑排序、生命周期统计
  FrameGraphBarrier.h/.cpp   按执行顺序生成每个 pass 的 barrier 计划
Tests/
  TestHarness.h              不依赖第三方库的最小测试框架
  FrameGraphTests/main.cpp   18 个用例 / 123 项断言
```

API 形态（与 §5.2 的概念 API 对齐）：

```cpp
framegraph::FrameGraph graph;
auto color     = graph.create_texture(desc);                       // transient
auto swapchain = graph.import_texture(desc, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, stage, access);

graph.add_graphics_pass("Lighting")
     .read(color, framegraph::usage::sampled_read())
     .write(target, framegraph::usage::color_attachment_write())
     .execute([&](framegraph::PassContext& ctx) { /* record draw */ });

if (!graph.compile()) { log(graph.get_error()); }   // 环、非法 usage、transient 未写先读等
log(graph.dump());                                 // pass、资源生命周期、每个 barrier 的来源
graph.execute();                                   // v1：按编译顺序回调，并把 barrier 计划交给回调
```

已实现的 barrier 规则（v1）：

- 跟踪每个资源最近一次访问的 layout / stage / access 与是否为写；
- pass 之前把资源转换到本次访问需要的 layout，只在 RAW / WAR / WAW 或 layout 变化时插入 barrier；
- 读后读且 layout 不变时不插入 barrier；同一 pass 内对同一资源的多次访问合并成一次 barrier；
- WAR 只产生执行依赖（`srcAccess = 0`），RAW / WAW 才需要让前一次写的结果可见；
- 导入资源使用调用方声明的初始状态（`initial_layout` / `initial_stages` / `initial_access`），transient 资源以 `UNDEFINED` 起步；
- 每个 barrier 记录来源（`first use` / `imported initial state` / `read-after-write` / `write-after-read` / `write-after-write`），并统计 `image_barriers`、`buffer_barriers`、`layout_transitions`、`elided_barriers`。

已知限制（不要当成已完成）：

- ~~没有 executor~~：该限制已在第二切片解除，见 §5.7（executor 负责真实 image / buffer、barrier 录制与 pass 执行；同步2 分支已实现，本机设备未启用该 feature，实跑走旧路径翻译）；
- 未实现 queue family ownership transfer（`src/dst_queue_family` 目前恒为 `VK_QUEUE_FAMILY_IGNORED`）；
- 未实现 transient 资源内存复用（aliasing）、冗余 barrier 消除、async compute 重叠分析；
- 同一 pass 内不允许对同一资源做不同 layout 的访问（v1 直接报错，而不是拆分 barrier）；
- barrier 使用 synchronization2 的 stage/access 位；旧路径（`vkCmdPipelineBarrier`）的降级转换由后续 executor 负责。

验证记录（2026-09-11，MSVC 19.51 / Ninja / Vulkan SDK 1.4.357.0）：

- `FrameGraphCore` 与 `FrameGraphTests` 编译通过，新增文件 0 warning；
- `FrameGraphTests.exe`：18 个用例、123 项断言全部通过（exit code 0）；
- `ctest --test-dir <build> --output-on-failure`：`1/1 Test #1: FrameGraphTests ... Passed`；
- 与构建期 `glslc` 改动合并后，`.\scripts\build.ps1 -Configuration Debug` 一键配置与构建成功（含 `VulkanRendererShaders` 与 `shaderc_sharedd.dll` 部署），`ctest` 与测试可执行文件仍为 18 用例 / 123 断言全通过；
- 用一组接近迁移目标的 pass（Shadow -> GBuffer -> Lighting -> Present）跑通 `compile()` / `dump()`，输出示例：

```text
FrameGraph 'DeferredPreview': compiled=true passes=4 resources=4 image_barriers=8 layout_transitions=8
  [2] LightingPass (graphics) declared=2
      image barrier GBuffer: COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL [read-after-write]
          src stage=COLOR_ATTACHMENT_OUTPUT access=COLOR_ATTACHMENT_WRITE
          dst stage=FRAGMENT_SHADER access=SHADER_SAMPLED_READ
  [3] Present (transfer) declared=3
      image barrier SwapchainImage: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR [write-after-write]
          src stage=COLOR_ATTACHMENT_OUTPUT access=COLOR_ATTACHMENT_WRITE
          dst stage=NONE access=NONE
```

---

### 5.7 第二切片：设备端 executor 与第一份 graph 驱动 Demo（已实现）

提交：分支 `codex/framegraph-executor`（尚未合并）。

文件：

```text
VulkanBase/FrameGraph/
  FrameGraphExecutor.h/.cpp   设备侧执行器：落实资源 + 录制 barrier + 执行 pass
Demos/VulkanTests/
  FrameGraphOffScreenTest.h   OffScreenRenderingTest 的 graph 版本（第一份图驱动 Demo）
```

executor 行为：

- transient 纹理按 `TextureDesc` 创建真实 `VkImage` + 显存 + `VkImageView`，usage 用 `to_vk_image_usage` 转换；
- transient buffer 按 `BufferDesc` 创建 `VkBuffer` + 显存；
- 资源按「名字 + 描述」缓存复用，描述变化时重建，图中不再出现的自动释放；
- 导入资源通过 `import_texture` / `import_buffer` 绑定（swapchain image、外部 buffer）；
- 录制 barrier：设备启用 synchronization2 时用 `vkCmdPipelineBarrier2`，否则把 sync2 的 stage/access 翻译成兼容的旧路径 `vkCmdPipelineBarrier`（WAR 仍只做执行依赖）；
- 图每帧重建，但 transient 纹理会记住上一帧结束时的 layout：录制时把规划里的 `UNDEFINED` 起跳换成真实 layout，画布这类需要保留内容的资源因此不会被每帧丢弃；
- pass 回调通过 `PassContext::user_data` 拿到 `FrameGraphExecution`（命令缓冲 + 资源访问器）；
- render target 支持：`acquire_render_target()` 用图资源（含导入的 swapchain image）拼出与现有 `VkRenderPass` 管线兼容的 render pass + framebuffer，附件 layout 固定为图规划的 layout（pass 内没有隐式转换），按附件视图缓存复用。

Demo 行为（`FrameGraphOffScreenTest`）：

- 每帧声明两张图资源与两个 pass：`DrawCanvas`（dynamic rendering 写 transient 画布）、`Composite`（采样画布）；
- 画布创建、`UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL`、`COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL` 全部由图生成，Demo 内没有手写 barrier；
- ImGui 面板实时显示 barrier 录制路径与 barrier 统计。

验证记录（2026-09-12，RTX 5090 D / 驱动 596.36 / Vulkan SDK 1.4.357.0）：

- `build.ps1 -Configuration Debug` 构建通过，`FrameGraphTests.exe` 仍为 18 用例 / 123 断言全通过；
- 新 Demo 以 60 FPS 稳定运行，ImGui 显示 `image barriers (last frame): 2`、`layout_transitions=2`、`elided=0`，与图规划一致；
- validation layer 输出与改动前一致：只有既有的 3 条错误（render pass `LOAD`+`UNDEFINED`、`lineWidth=0`、present semaphore 跨 swapchain image 复用），没有新增与本切片相关的错误；
- 本机设备未启用 synchronization2，因此实跑走的是旧路径 barrier 录制分支（sync2 分支已实现但未在设备上跑到）。

已知问题（不要当成已完成）：

- 复用 legacy `CanvasToScreen` 合成通道后画面为空白，尚未定位是 legacy 屏幕路径本身还是本 Demo 参数问题；下一步把 Composite 也改成 dynamic rendering（图直接管理 swapchain image 与 present 转换）后再验证；
- swapchain image 与 ImGui pass 尚未纳入图，图目前只负责离屏画布；
- queue family ownership transfer、transient 内存复用、冗余 barrier 消除仍未实现；
- **更正（2026-09-12）**：先前记录的「`glTFLoading` 既有崩溃」结论有误。真实根因是 `GraphicsPipelineCreateInfoPack` 的 `lineWidth` 默认 0，本机 validation layer 把这条 VUID 升级为 `vkCreateGraphicsPipelines` 失败（别的机器只告警），导致 `create_pipeline()` 失败、demo 初始化中止。已在共享 helper 里把默认值改成 1.0，并验证未修改的 glTF demo 可正常跑帧、默认 demo 不受影响。
- **执行器修复**：`acquire_render_target()` 生成的 render pass 现在复刻 RHI render pass 的外部 subpass 依赖，否则管线与 render pass 不兼容（`dependencyCount 0 != 1`）。
- **swapchain 的所有权方案（已实现）**：给图加了「外部同步资源」语义——`import_texture(..., externally_synchronized=true)` 时图仍记录使用与依赖，但不为该资源生成 barrier；同时 `RenderTargetAttachment` 支持显式 `initial_layout`/`final_layout`。这样 swapchain image 的转换交回 render pass（`UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR`，与 legacy 屏幕 pass 一致），图只负责自己拥有的资源（例如深度）。
- **glTF 迁移（已落地）**：颜色 = 导入的 swapchain image（外部同步），深度 = 图拥有的 transient 纹理（图规划并录制其布局转换），pass 主体用 `acquire_render_target()` 拿到的兼容 render pass/framebuffer 记录绘制。实跑 60 FPS、单元测试 18/123 全过。
- **仍未过 validation 的部分**：`vkQueueSubmit(): performs a layout transition on presentable VkImage ... but the image has not been acquired`，与既有的 render-finished semaphore 跨 swapchain image 复用错误（§2.6 第 10 条）同时出现、高度相关。下一步应先修 swapchain 同步（每个 swapchain image 一个 render-finished semaphore，或引入 `VK_KHR_swapchain_maintenance1` + fence），再复验。
- **ShadowMapping 迁移：尚未开始**（结构：shadow depth pass + SAT compute 系列 + 屏幕 pass；需要先让 executor 覆盖 compute/storage image 的用例，并复用上面的外部同步方案处理 swapchain）。
- BasicRendering 迁移进行中，且前置阻塞已定位：executor 的 render target 能力已就位；`glTFLoading` 的迁移尝试在真实运行中初始化失败，随后用**未修改的** `glTFLoading` 复测同样失败（进程退出码 `0xC0000409`，后台隐藏窗口运行偶尔能进入帧循环），说明这是**既有问题、与 FrameGraph 迁移无关**。迁移该 demo 之前需要先定位这个既有崩溃，因此本轮已回退 demo 改动、master 保持可用。
- 下一步顺序：① 定位 `glTFLoading` 既有初始化失败；② 用 executor 迁移 glTF（图持有 depth、render target 由 executor 提供）；③ 迁移 `ShadowMapping`（阴影 pass + 手写 barrier + SAT compute 系列 + 屏幕 pass），需要先在 executor 上补 compute/storage image 支持。

## 6. Synchronization 与 FrameContext 设计

### 6.1 当前模式

```text
acquire
record
submit
present
wait fence
```

### 6.2 目标模式

```text
Frame N     acquire / record / submit / present
Frame N+1   acquire / record / submit / present
Frame N+2   acquire / record / submit / present
```

建议先使用 2 个 frame slot，稳定后再尝试 3 个。

每个 frame slot 独立持有：

- primary command buffer；
- fence；
- image available semaphore；
- render finished semaphore；
- transient resources；
- graph execution context。

### 6.3 必须处理

- swapchain 重建；
- `VK_ERROR_OUT_OF_DATE_KHR`；
- `VK_SUBOPTIMAL_KHR`；
- fence reset 的正确时机；
- semaphore 复用安全；
- 资源在 GPU 使用期间不能释放；
- queue family ownership；
- graphics / compute / present 队列差异；
- Debug Utils label；
- validation layer 优先级检查。

### 6.4 验收标准

- CPU 不再每帧等待 fence；
- 2-3 帧并行时 Validation Layer 零错误；
- resize、最小化、切换 Demo 不崩溃；
- 能对比旧同步路径和新同步路径的 CPU frame time；
- 能输出每帧 acquire / record / submit / wait 的时间。

---

## 7. GPU-driven Rendering 设计

### 7.1 第一阶段：Bindless

要求：

- `VK_EXT_descriptor_indexing`；
- `PARTIALLY_BOUND_BIT`；
- `UPDATE_AFTER_BIND_BIT`；
- runtime descriptor array；
- 降低 descriptor set bind 次数；
- benchmark 记录 descriptor update 次数。

### 7.2 第二阶段：Frustum Culling

流程：

```text
Scene / Instance Buffer
        -> Compute Frustum Culling
        -> Indirect Draw Commands
        -> vkCmdDrawIndexedIndirect / IndirectCount
```

统计：

- total instance；
- visible instance；
- culled instance；
- indirect draw count；
- CPU submission time；
- GPU culling time。

### 7.3 第三阶段：Hi-Z Occlusion Culling

可选，但在有额外时间时推荐：

- 生成上一帧 depth mip chain；
- compute 根据 Hi-Z 进行 occlusion test；
- 和仅 frustum culling 做对比；
- 记录 culling ratio 和 GPU time。

### 7.4 第四阶段：Meshlet / Visibility Buffer

暂不纳入当前目标。

### 7.5 验收标准

- 大规模实例场景下，culling on/off 结果正确；
- indirect draw 路径稳定；
- 不因 bindless 导致 descriptor 错误；
- 输出 culling ratio、draw call、GPU time；
- 本地 AMD GPU 和 RTX 机器均能运行基础路径。

---

## 8. Hardware Ray Tracing 设计

### 8.1 能力要求

必须运行时检测：

- `VK_KHR_acceleration_structure`；
- `VK_KHR_ray_query`；
- `VK_KHR_ray_tracing_pipeline`；
- `VK_KHR_deferred_host_operations`（按实现需要）；
- buffer device address；
- 相应 feature flags。

缺少光追能力时：

- 自动关闭 RT pass；
- 保留 raster fallback；
- 在 benchmark metadata 中记录 `rt_available=false`。

### 8.2 建议范围

第一阶段：

- Ray Query Shadow；
- BLAS / TLAS 构建；
- 动态场景 TLAS update；
- RT on/off 视觉对比；
- GPU pass timing。

第二阶段：

- Ray Query Reflection；
- 时间累积；
- 简单空间降噪。

第三阶段：

- Denoiser 对比；
- PSNR / SSIM / 稳定性分析；
- 和 raster shadow/reflection baseline 对比。

不建议当前做：

- 多跳 Path Tracing；
- ReSTIR DI/GI；
- 复杂材质重写；
- 全场景 GI。

### 8.3 验收标准

- 本机编译通过且运行时自动禁用 RT；
- RTX 机器上真实运行、截图、记录帧时间；
- RT pass 使用 FrameGraph 创建的 transient resource；
- benchmark 中包含 `rt_available`、GPU 型号、驱动版本；
- 不把本地 AMD 不能验证的结果写成已验证。

---

## 9. Benchmark 体系

### 9.1 CLI 设计

```text
--benchmark
--scene <name>
--frames <N>
--warmup <N>
--resolution <WxH>
--gpu <index>
--vsync off
--no-ui
--output <csv|json>
--renderer <legacy|graph>
--culling <off|frustum|hiz>
--rt <off|shadow|reflection>
```

### 9.2 CPU 指标

- frame time；
- graph compile time；
- command recording time；
- submit time；
- fence wait time；
- barrier count；
- draw call count；
- descriptor update count。

### 9.3 GPU 指标

- total GPU frame time；
- shadow pass time；
- culling pass time；
- GBuffer / lighting pass time；
- RT pass time；
- post process time；
- peak VRAM。

### 9.4 统计口径

- warmup 后开始采样；
- 默认 300-600 帧；
- 记录 mean / median / P50 / P95 / P99 / min / max / stddev；
- 明确 GPU 型号、驱动、分辨率、场景、分辨率、编译类型；
- 每次实验至少运行 3 次；
- 报告中说明测量工具和采集方式。

### 9.5 实验对照

- Legacy manual barriers vs FrameGraph auto barriers；
- RenderPass vs Dynamic Rendering；
- culling off vs frustum vs Hi-Z；
- RT off vs RT shadow vs RT reflection；
- 本机 AMD vs RTX 机器。

### 9.6 输出格式

建议：

```text
results/
  benchmark_YYYYMMDD_HHMMSS.csv
  benchmark_YYYYMMDD_HHMMSS.json
  summary.md
  charts/
```

CSV / JSON 至少包含：

```json
{
  "gpu": "AMD Radeon(TM) Graphics",
  "driver": "...",
  "api_version": "1.3.217",
  "resolution": "1920x1080",
  "scene": "default",
  "renderer": "framegraph",
  "culling": "frustum",
  "rt": "off",
  "frames": 600,
  "stats": {
    "mean": 0.0,
    "p50": 0.0,
    "p95": 0.0,
    "p99": 0.0
  }
}
```

---

## 10. 分阶段实施路线

### P0：基线冻结与构建可信化（进行中）

预计：3-5 天

已完成：

- `961065f`：新增固定的依赖恢复、CMake Presets 和一键构建脚本；移除硬编码 SDK 路径和 shaderc 隐式依赖；
- `d2be67e`：在 `vkCreateDevice()` 前显式启用 imageless framebuffer、dynamic rendering 和 sampler anisotropy，并修复 compute command pool；
- 干净依赖目录下 Debug / Release 构建成功；
- 当前集成工作区 Debug 构建成功，程序运行 5 秒未退出；
- WIP 内容在 P0 集成后逐文件校验并保留。

未完成：

- ~~决定 WIP 的最终提交策略~~：已归档到 `codex/build-shader-pipeline`（`554efb5`），分支尚未合并；
- 固化可运行的 Demo 清单；
- 记录基线 FPS / CPU frame time；
- 建立 Benchmark 元数据和报告输出。

验收状态：

- 单条命令完成配置和构建：已满足；
- Debug 构建成功：已满足；
- 当前 Demo 启动 smoke test：已满足基础检查；
- 有基线 FPS / CPU frame time：尚未满足；
- 记录 GPU 与 driver：已记录 `AMD Radeon(TM) Graphics`、Vulkan `1.3.217`、SDK `1.4.313.1`，仍需实现可重复的自动采集。

### P1：FrameGraph + Synchronization 核心

预计：2-3 周

任务：

- 实现 RenderGraph 资源、Pass、依赖；——图模型与依赖推导已完成（v1 第一切片，见 §5.6）
- 实现 barrier 生成；——规划层已完成（同步语义 + 统计），设备端录制未开始
- 实现 imported / transient resource；——声明与初始状态已支持，真实资源分配未开始
- 实现 executor：真实 image / buffer 分配、`vkCmdPipelineBarrier2` 录制、每帧重建图的执行上下文；**已完成（§5.7）**
- 迁移 OffScreen；**已完成**（`FrameGraphOffScreenTest`，图拥有离屏画布与两次 layout 转换）
- 迁移 ShadowMapping；**未开始（下一步）**
- 迁移 Deferred；**未开始**
- 比较 legacy 和 graph 的行为；
- 开启 validation layer。

验收：

- 视觉结果一致；——未满足（Demo 尚未迁移）
- validation 零错误；——未满足：executor 已在设备上跑起来，剩余错误集中在 ImGui render pass 的既有非法组合与 swapchain 同步（presentable image acquire / semaphore 复用）
- 输出 barrier 统计；——已满足（`BarrierStats`）
- 输出 graph dump；——已满足（`dump()`，含每个 barrier 的 reason / stage / access）
- 有 graph 单元测试；——已满足（18 个用例 / 123 项断言，device-free）
- 能解释每个 barrier 的来源；——已满足（`reason` 字段与依赖类型统计）

### P2：Frames in Flight

预计：1-2 周

任务：

- 实现 FrameContextRing；
- 2-3 帧并行；
- 独立 command buffer / fence / semaphore；
- swapchain resize；
- transient resource 延迟回收。

验收：

- CPU 不再逐帧等待；
- 无 synchronization validation 报错；
- 新旧路径 CPU frame time 对比；
- resize 稳定。

### P3：GPU-driven Rendering

预计：2-4 周

任务：

- Bindless descriptor；
- Compute frustum culling；
- Indirect draw；
- 可选 Hi-Z；
- 大规模实例场景。

验收：

- culling on/off 正确；
- draw call / visible / culled 数据完整；
- GPU 时间对比；
- 场景规模可调。

### P4：Hardware Ray Tracing

预计：2-3 周，在 RTX 机器验证

任务：

- BLAS / TLAS；
- Ray Query Shadow；
- Reflection；
- 简单降噪；
- RT on/off 比较。

验收：

- 本机自动禁用；
- RTX 机器真实运行；
- 截图、GPU 时间、质量对比；
- 运行时 feature gate 正确。

### P5：Benchmark 与报告

预计：1-2 周，持续进行

任务：

- CLI 参数；
- CSV / JSON；
- P50 / P95 / P99；
- Markdown 报告；
- 图表；
- 技术文章。

验收：

- 可复现同一实验；
- 能在新机器重建；
- 每个简历数字都能回指原始数据。

---

## 11. 风险与缓解

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| WIP 与重构混在一起 | 难回退、难定位回归 | 先处理 WIP，独立分支 |
| FrameGraph 范围失控 | 长期无法产出 | 只做 v1 barrier + lifetime |
| RT 本机不可验证 | 不能写真实结论 | feature gate + RTX 单独验证 |
| GPU-driven 一开始做 Meshlet | 周期过长 | 先 frustum，再 Hi-Z |
| benchmark 数据不可信 | 简历风险 | 固定场景、固定帧数、CSV、P50/P95 |
| 从上游复制代码 | 面试边界不清 | 保留 license，按实际改动描述 |
| remote 地址不一致 | 简历链接错 | 已确认 canonical repo 为 `SinonShizuku/VulkanDemoPlatform` |
| 为了功能数量牺牲正确性 | 项目像 Demo 集合 | 先完成一条完整主线 |

---

## 12. Definition of Done

项目达到以下条件，才认为重构完成：

- 有一个统一 RenderGraph 驱动的完整场景；
- FrameGraph 自动生成 Barrier，而不是 Demo 手写；
- 2-3 个 frames in flight；
- Validation Layer 零错误；
- GPU-driven 进入同一 Graph；
- RT 通过运行时 capability gate；
- 本机可以跑无 RT 路径；
- RTX 机器可以跑 RT 路径；
- benchmark 可一键运行；
- 输出 CSV / JSON / Markdown；
- 简历中的每个性能数字都能解释口径；
- 保留上游许可证与 attribution；
- 不再把未实现内容写成已完成。

---

## 13. 下一阶段建议

当前执行顺序：

1. canonical repository 已确认，P0 构建和设备初始化已提交到 `master`；
2. ~~决定 WIP 的归档策略~~：构建期着色器编译改动已归档到 `codex/build-shader-pipeline`（`554efb5`）；
3. ~~开始 FrameGraph v1~~：图编译、依赖、生命周期、barrier 规划与单元测试已完成（见 §5.6）；
4. ~~实现 FrameGraph executor~~：已完成（真实资源分配、barrier 录制、pass 执行、render target 生成）；已接入 `FrameGraphOffScreenTest` 与 `glTFLoading`；
5. 修 swapchain 同步（validation 归零的关键）：每张 swapchain image 一个 render-finished semaphore（或 `VK_EXT_swapchain_maintenance1` + fence），并处理 ImGui render pass 的非法 initialLayout 组合；
6. 迁移 ShadowMapping（图拥有 shadow map、SAT 走 compute pass、删掉手写 barrier）与 Deferred，并以 Validation 结果作为验收；
7. 升级 frames in flight，解决 semaphore 跨 swapchain image 复用问题；
8. 再加入 GPU-driven；
9. 最后在 RTX 机器上加入光追；
10. 全阶段持续积累 benchmark 数据。

核心原则：

> 不要为了增加功能数量而扩展范围。
> 每一个新模块都必须进入同一条 RenderGraph 路径，并且产生可验证结果。
