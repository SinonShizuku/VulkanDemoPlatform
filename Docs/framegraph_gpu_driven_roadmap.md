# VulkanRenderer 重构路线：FrameGraph + Synchronization + GPU-driven Rendering + RT + Benchmark

> 文档状态：设计基线、实施路线与已验证进度
> 建立日期：2026-09-11
> 最近更新：2026-09-12
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

本地化（代码页 936）的 MSVC 输出会让 CMake/Ninja 的 dyndep 解析不到头文件依赖，表现为「只改头文件不重编译」，从而验证到旧二进制甚至定位错问题。**更正（2026-09-12）**：此前记录的 `$env:VSLANG = "1033"` 方案在本机**无效**——本机只安装了 2052（简体中文）语言资源，`cl.exe /showIncludes` 无论 VSLANG 如何都输出中文；而 CMake 按系统 ANSI 代码页解码 cl 的 **UTF-8** 输出，探测出的 `msvc_deps_prefix` 成了乱码，Ninja 永远匹配不上，头文件依赖数为 0（`ninja -t deps` 实测 `#deps 0`，改动 `.h` 不触发任何重编译）。现在 `CMakeLists.txt` 在配置期自己做一次探测：编译一个最小 TU，按 UTF-8 读取 `cl` 的输出并剥掉头文件路径得到真实前缀，与 CMake 的探测结果不一致时才覆盖 `CMAKE_CL_SHOWINCLUDES_PREFIX`（英文环境两者一致，不受影响），配置时会打印 `MSVC /showIncludes prefix repaired: ...`。验证：对 `FrameGraphExecutor.cpp.obj` 记录到 178 个依赖；touch 一个被广泛包含的头文件会重编译对应 TU；紧接着的无改动构建编译 0 个文件。

另一个相关坑：应用 target 此前没有指定源码编码，MSVC 按本地代码页（936）解析 UTF-8 源码，个别多字节序列会“吞掉”后续代码（表现为莫名其妙的语法错误）并产生大量 `C4819`。现在 `VulkanRenderer` 也编译为 `/utf-8`（`FrameGraphCore` / 测试同样如此），构建 0 warning、中文注释不再影响语法。

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
10. 已有 Validation 错误：**默认路径已清零（2026-09-12，见 §5.8）**。ImGui render pass 的 `LOAD` + `UNDEFINED` initialLayout（VUID-VkAttachmentDescription-format-06699）已改为 `PRESENT_SRC_KHR`；退出时的 `vkDestroyShaderModule: Invalid device` / `0xC0000409`、`vkFreeDescriptorSets` 缺 `FREE_DESCRIPTOR_SET_BIT`、57 个 leaked objects、以及 demo 的悬垂 swapchain 回调均已修复。默认路径连跑 5 次：exit code 0、stdout 无 VUID、stderr 为空。**图路径仍未清零**：`glTFLoading` 作为启动 demo 时稳定复现 presentable image acquire 相关错误（见 §5.8）。
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
- **acquire 记账修复**：`vkAcquireNextImageKHR` 返回 `VK_SUBOPTIMAL_KHR` 时图像**已经 acquire 成功**，但原实现直接 `recreate_swapchain()` 并返回，随后拿一张“从未 acquire”的新 swapchain 图像渲染——validation 的 `performs a layout transition on presentable VkImage ... but the image has not been acquired from VkSwapchainKHR` 正是指这个。现在 SUBOPTIMAL 视为“本帧可用”，重建推迟到下一帧 acquire 之前；只有 `VK_ERROR_OUT_OF_DATE_KHR` 才立即重建并重新 acquire。
- **待查（会挡住图路径的可靠验证）**：应用初始化在本机偶发失败——同一个二进制有时整帧运行、有时报 `Failed to switch to default demo!`（并伴随 validation 层自身在 `VkLayer_khronos_validation.dll` 里崩溃）。疑似未初始化状态（例如 `DemoBase::window_size` / `window`）或初始化顺序问题，建议在推进 ShadowMapping/Deferred 迁移前优先排查。
- **swapchain 的所有权方案（已实现）**：给图加了「外部同步资源」语义——`import_texture(..., externally_synchronized=true)` 时图仍记录使用与依赖，但不为该资源生成 barrier；同时 `RenderTargetAttachment` 支持显式 `initial_layout`/`final_layout`。这样 swapchain image 的转换交回 render pass（`UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR`，与 legacy 屏幕 pass 一致），图只负责自己拥有的资源（例如深度）。
- **glTF 迁移（已落地）**：颜色 = 导入的 swapchain image（外部同步），深度 = 图拥有的 transient 纹理（图规划并录制其布局转换），pass 主体用 `acquire_render_target()` 拿到的兼容 render pass/framebuffer 记录绘制。实跑 60 FPS、单元测试 18/123 全过。
- **仍未过 validation 的部分**：`vkQueueSubmit(): performs a layout transition on presentable VkImage ... but the image has not been acquired`，与既有的 render-finished semaphore 跨 swapchain image 复用错误（§2.6 第 10 条）同时出现、高度相关。下一步应先修 swapchain 同步（每个 swapchain image 一个 render-finished semaphore，或引入 `VK_KHR_swapchain_maintenance1` + fence），再复验。
- **ShadowMapping 迁移（本轮尝试的结论）**：已把**屏幕 pass** 接到图（图拥有 depth、颜色用外部同步的 swapchain、由 render pass 完成 `UNDEFINED -> COLOR -> PRESENT`），demo 能稳定运行；但 validation 报 `vkQueueSubmit(): performs a layout transition on presentable VkImage ... but the image has not been acquired from VkSwapchainKHR`——该错误在旧路径（RHI render pass + manager 自带 framebuffer）不出现，只在这条图路径出现，说明 acquire/present 的窗口记账与图路径还没对齐，因此**迁移已回退**（master 保持验证干净）。下一步：先梳理 `VulkanSwapchainManager::swap_image()` 与 `VulkanCommand::present_image()` 的 acquire 记账，或把 acquire/present 也做成图上的显式 pass；然后再迁 shadow depth / SAT compute（原计划：mage 的用例，并复用上面的外部同步方案处理 swapchain）。
- BasicRendering 迁移进行中，且前置阻塞已定位：executor 的 render target 能力已就位；`glTFLoading` 的迁移尝试在真实运行中初始化失败，随后用**未修改的** `glTFLoading` 复测同样失败（进程退出码 `0xC0000409`，后台隐藏窗口运行偶尔能进入帧循环），说明这是**既有问题、与 FrameGraph 迁移无关**。迁移该 demo 之前需要先定位这个既有崩溃，因此本轮已回退 demo 改动、master 保持可用。
- 下一步顺序：① 定位 `glTFLoading` 既有初始化失败；② 用 executor 迁移 glTF（图持有 depth、render target 由 executor 提供）；③ 迁移 `ShadowMapping`（阴影 pass + 手写 barrier + SAT compute 系列 + 屏幕 pass），需要先在 executor 上补 compute/storage image 支持。

### 5.8 退出路径与验证可信化（2026-09-12）

本轮不加功能，目标是把「验证结果可信」补齐：先修构建依赖，再修退出路径，最后让默认路径的 validation 归零。本地分支 `codex/validation-cleanup`。

已修复（均实跑验证）：

- **Ninja 头文件依赖失效**：`msvc_deps_prefix` 是乱码，Ninja 记录不到任何头文件依赖（`#deps 0`），改 `.h` 不重编译；已在 `CMakeLists.txt` 配置期自行探测真实前缀并覆盖（详见 §2.1.1）。
- **退出崩溃（`0xC0000409`，崩溃点就在 validation 层内）**：根因是一条生命周期链——`VulkanAppLauncher::cleanup()` 先销毁 `VkDevice`，而 demo、ImGui、共享同步对象、`VulkanPipelineManager` 的附件都晚于设备析构，析构里调用 `vkDestroy*` 时设备句柄已被置空。修复：① `terminate_window()` 按 demo → ImGui → 共享资源 → rpwf/swapchain → device 的顺序显式释放；② `DestroyHandleBy` 在设备句柄为空时只清句柄、不再调用 Vulkan；③ `VulkanRenderPass` / `VulkanFramebuffer` 补上真正的析构（此前为空析构，`vector::clear()` 会漏掉 framebuffer）；④ demo 里进程级 `static` 的 shader module 改由 `VulkanShaderModule::release_all()` 在设备销毁前统一释放（§5.9 修正了这里早先「改成非 static」的做法）。
- **demo 的 swapchain 回调悬垂**：demo 注册的回调捕获 `this`，demo 销毁后回调仍留在 `VulkanSwapchainManager` 中，退出或重建 swapchain 时会访问已释放的 demo（实测：在 `~VulkanPipeline()` 里读到 debug 堆填充值 `0xdddddddddddddddd` 并据此调用 `vkDestroyPipeline`）。现在回调带 owner，`~DemoBase()` 统一注销。
- **ImGui render pass 非法组合**：`LOAD` + `initialLayout = UNDEFINED`（VUID-VkAttachmentDescription-format-06699）改为 `PRESENT_SRC_KHR`——ImGui 通道总是紧跟在把同一张 swapchain 图像写成 `PRESENT_SRC_KHR` 的屏幕通道之后。
- **ImGui descriptor pool 缺 flag**：`ImGui_ImplVulkan_Shutdown()` 会 `vkFreeDescriptorSets()`，池必须带 `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`（VUID-vkFreeDescriptorSets-descriptorPool-00312）。
- **glTFLoading 初始化失败（§10 P1 遗留项 ①）**：根因不是 glTF 资源，而是 demo 用全局 `current_demo_name` 当闸门，而该变量只在「菜单切换」路径被更新；默认 demo / 工厂路径下它仍停在 `BuffersAndPictureTest`，于是 `create_pipeline()` 直接 `return false`，表现为 `Failed to switch to default demo!`。修复：`switch_to_demo()` 统一按 `current_demo->get_type()` 同步该全局，并把 `glTFLoading` 的两处闸门由菜单标签改为自己的 type。

验证（2026-09-12，RTX 5090 D）：

- 默认路径 `BuffersAndPictureTest`：连跑 5 次，exit code 0、stdout 无任何 VUID、stderr 为空、60 FPS；
- `FrameGraphOffScreenTest` 作为启动 demo：exit code 0、validation 无输出、60 FPS；
- `glTFLoading` 作为启动 demo（临时改动，仅用于验证）：现在能初始化并跑满 60 FPS（修复前直接 `Failed to switch to default demo!`）；
- `FrameGraphTests.exe`：123 checks / 0 failures。

仍未解决（下一步）：

- ~~图路径的 acquire/present 记账~~：已在 §5.9 定位并修复（`FrameGraphExecutor` 的导入表只追加不覆盖，导致渲染永远写回第一帧的 swapchain image）；`glTFLoading` 图路径与 resize 现在都通过 validation。
- 除默认 demo 外，其余 demo（ShadowMapping / Deferred / 其余 VulkanTests）本轮未逐个做 validation 验收。


---

### 5.9 图路径的 acquire/present 记账与 resize 稳定性（2026-09-12）

§5.8 之后唯一挡住图路径验证的，是 `glTFLoading` 作为启动 demo 时稳定复现的两条 validation 错误：`performs a layout transition on presentable VkImage ... but the image has not been acquired`，以及 `command buffer ... expects VkImage ... PRESENT_SRC_KHR, current layout is UNDEFINED`。本轮定位并修复：

- **executor 的导入表只追加、不覆盖**：`FrameGraphExecutor::import_texture()` 每帧 `emplace_back`，而 `find_imported_*()` 返回**第一个**匹配项。图每帧重建、句柄每帧重新分配，于是渲染永远写回第一帧导入的那张 swapchain image：该图像本帧没有 acquire（第一条错误），而本帧真正 acquire 到的图像从未被转换过，ImGui 通道按 `PRESENT_SRC_KHR` 去 load 它时仍是 `UNDEFINED`（第二条错误）。修复：导入按资源句柄覆盖（`Impl::set_imported`），顺带消掉导入表每帧增长两行的泄漏。
- **resize 崩溃（`0xC0000005`）**：把 demo 的 shader module 改成函数内非 static（§5.8 的做法）之后，各 demo 里既有的 `static VkPipelineShaderStageCreateInfo[]`（在 `create_pipeline()` 首次调用时捕获 module 句柄，并被 swapchain 重建回调复用）指向了已销毁的 module，重建 swapchain 时直接崩。现在的做法：**保留** demo 里 static shader module 的原有生命周期，改为在 `VulkanShaderModule` 内部登记实例，由 `VulkanShaderModule::release_all()` 在 `VulkanAppLauncher::cleanup()` 中（销毁设备之前）统一释放——既不会出现「晚于设备析构」的 invalid device，也不会在 `vkDestroyDevice` 报 leaked objects。

验证（2026-09-12，RTX 5090 D）：

- `glTFLoading` 作为启动 demo（临时改动，仅用于验证）：exit code 0、validation 无任何输出、60 FPS；
- 同一配置下用 Win32 `SetWindowPos` 连续改 4 次窗口尺寸（触发多次 swapchain / render target 重建）：exit code 0、无 VUID、无 leaked objects、stderr 为空（修复前为 `0xC0000005` 崩溃）；
- 默认路径 `BuffersAndPictureTest`：正常退出与连续 resize 同样 exit code 0 + 零 VUID；
- `FrameGraphTests.exe`：123 checks / 0 failures。

结论：§5.7 记录的「ShadowMapping 迁移回退」前置阻塞（图路径与 acquire/present 记账未对齐）已经解除，可以继续 §13 第 6 项。

小遗留：`VulkanSwapchainManager::recreate_swapchain()` 里仍有一行调试输出 `outstream << get_swapchain_image_views().size();`（resize 时会在 stdout 打出裸数字），待清理。


---

### 5.10 ShadowMapping 屏幕 pass 接入 FrameGraph（2026-09-12）

§13 第 6 项的**第一步**：把 ShadowMapping 的屏幕 pass 接进图——颜色 = 导入的 swapchain image（外部同步，layout 转换交给 render pass），深度 = 图拥有的 transient 纹理；同时删掉 `rpwf_ds` 的组合与它带来的隐式 layout 转换。scene 的 pipeline 是针对 RHI render pass 创建的，executor 生成的 render pass 复刻了相同附件格式与 subpass 依赖，因此保持兼容（§5.7 记录的那次回退正是被 §5.9 的导入表 bug 挡住的）。

验证（RTX 5090 D）：`ShadowMapping` 作为启动 demo（临时改动，仅用于验证）exit code 0、无 VUID、60 FPS；默认路径与 `FrameGraphTests`（123 checks / 0 failures）不受影响。

仍留待下一轮：阴影贴图与 SAT compute 链仍在图外（`rpwf_offscreen_ds` + 手写 barrier）。**更正（2026-09-12）**：图与 executor 侧其实已经具备 compute 能力——`FrameGraph` 提供 `add_compute_pass()`，`usage::storage_read()/storage_write()` 产生 `COMPUTE_SHADER` stage + `GENERAL` layout 的规划，executor 按图顺序执行 pass 回调、barrier 也按通用路径录制（synchronization2 或旧路径）。本轮补了一个 device-free 单测 `compute_pass_storage_write_then_fragment_sample_plans_barriers`（storage 写入 → fragment 采样，断言 `GENERAL -> SHADER_READ_ONLY_OPTIMAL`、`COMPUTE_SHADER -> FRAGMENT_SHADER`），测试总数 129 checks / 0 failures。因此 SAT 迁移**不需要新增 executor 能力**，缺的是把 shadow map / SAT 图像改为图资源，并在每帧 `prepare()` 之后、`execute()` 之前用图提供的 view 重写相关 descriptor set。另有既有的 performance warning（offscreen 管线声明了 4 个顶点属性，而 offscreen 顶点着色器只消费 location 0），属于非 error 的既有项。


---

### 5.11 ShadowMapping：shadow map / SAT 链迁入 FrameGraph（接口确认与执行计划，2026-09-12）

§5.10 之后剩的部分：把阴影贴图与 SAT 计算链从 RHI 手里搬进图（分支 `codex/shadow-sat`）。本轮先把接口与改动顺序固定下来，避免边改边猜：

- **图资源**：`ShadowDepth`（`D16_UNORM`，`DepthStencilAttachment | Sampled`）、`ShadowVsm`（与现有 `ca_offscreen_vsm` 同格式，`ColorAttachment | Sampled`）、SAT 中间图（行/列 block、scan、add，`Storage | Sampled`）；
- **pass**：`Shadow`（graphics，写深度 + VSM）、6 个 SAT `add_compute_pass()`（`usage::storage_read()/storage_write()`）、`Scene`（graphics，`usage::sampled_read()` 采样 SAT 最终图）；
- **删除手写同步**：`cmd_barrier_color_to_compute()`、`cmd_barrier_compute_to_fragment()` 与 SAT 链上的 barrier 全部交给图的规划；
- **descriptor 重写**：`descriptor_sets.scene` 的 binding 1/2（两个 `COMBINED_IMAGE_SAMPLER`）与 compute 的 SAT bindings 必须在每帧 `prepare()` 之后、`execute()` 之前用图提供的 view 重写；`descriptor_sets.offscreen` 只绑 UBO，不需要改绑定；`descriptor_pool`（2 sets × 2 UBO + 2 combined sampler）可复用，不必重建；
- **验收**：`ShadowMapping` 作为启动 demo（临时切换）validation 零错误 + 60 FPS + PCF/PCSS/VSSM 三种模式切换画面正常，然后还原默认 demo 并跑 `FrameGraphTests`。

**已完成（2026-09-12）**：`render_frame()` 重构为**每帧一张图**——`Shadow`(graphics) → `SatRowBlock/Scan/Add`、`SatColBlock/Scan/Add`(compute，`storage_read/storage_write`) → `Scene`(graphics，`depth_stencil_sampled_read` + `sampled_read`)；shadow map 深度、VSM 颜色与 8 张 SAT 图像全部由图拥有，`cmd_transition_undefined_to_general` / `cmd_barrier_color_to_compute` / `cmd_barrier_compute_to_compute` / `cmd_barrier_compute_to_fragment` 全部删除；6 个 compute set 与 scene 的 binding 1/2 在每帧 `prepare()` 之后用图视图重写。

实施中得到的三条硬约束（都已写进代码注释）：

1. **render pass 兼容性要求 dependencyCount 相同**：offscreen 管线创建时用的是 2 条 subpass 依赖，executor 默认只复刻 1 条 → `vkCmdDrawIndexed: dependencyCount is incompatible 1 != 2`；现由 `acquire_render_target(..., dependencies)` 显式传入 `VulkanPipelineManager::get_offscreen_subpass_dependencies()`。
2. **一个资源不能跨两张图**：SAT 与 shadow map 必须同图，否则图按帧记账的 layout 与图外手写 barrier 的 oldLayout 不一致（实测 6 条 `VUID-VkImageMemoryBarrier-oldLayout-01197` + `0xC0000005`）。
3. **render target 附件的 `final_layout` 必须等于该 pass 声明的 usage layout**，否则图的规划与真实 layout 分叉（同样报 01197）；本轮因此去掉了阴影 pass 上显式的 `final_layout` 覆盖。另外新增 `usage::depth_stencil_sampled_read()`（深度图进片元着色器采样用 `DEPTH_STENCIL_READ_ONLY_OPTIMAL` + `SHADER_SAMPLED_READ`，与既有的 attachment 读取语义区分）。

验证（RTX 5090 D，2026-09-12）：`ShadowMapping` 作为启动 demo（临时改动，仅用于验证）**exit code 0、零 VUID、60 FPS、无 leaked objects、stderr 为空**；默认路径 `BuffersAndPictureTest` exit 0 / 零 VUID；`FrameGraphTests` 129 checks / 0 failures。PCF/PCSS/VSSM 的画面切换属于人工确认项（Vulkan swapchain 窗口无法用 `PrintWindow` 可靠截图）。

遗留清理（不影响验收）：`rpwf_offscreen_ds`、`sat_images` 等 RHI 侧的旧资源与 `create_compute_descriptor_resources()` 里的初始 descriptor 绑定已经不再被渲染路径使用，可按帧删掉以省显存。

原始状态记录（保留）：接口确认与能力单测已完成（§5.10 的 129 checks / 0 failures）。实施过程中撞到两条结构性约束，已解决前者、并把后者写成下一轮的第一步：

- **render pass 兼容性要求 dependencyCount 相同**：offscreen 阴影管线是针对 RHI 的 render pass 创建的（2 条 subpass 依赖），而 executor 生成的 render target 默认只复刻 1 条（屏幕 pass 用的那种），于是 `vkCmdDrawIndexed` 报 `dependencyCount is incompatible ... 1 != 2`。修复：`FrameGraphExecutor::acquire_render_target()` 新增可选参数 `std::span<const VkSubpassDependency> dependencies`（为空时保持原来的单条依赖），`VulkanPipelineManager` 暴露 `get_offscreen_subpass_dependencies()` 作为唯一来源。
- **一个资源不能跨两个图**：只把「阴影 pass + 阴影贴图」搬进图（SAT 仍留在图外、继续手写 barrier）这条路走不通——SAT 的图像仍在图外被手写转换，而阴影贴图已经由图拥有，按帧复用的 layout 记账与手写 barrier 声明的 oldLayout 会不一致，validation 直接报 6 条 `VUID-VkImageMemoryBarrier-oldLayout-01197`，进程以 `0xC0000005` 退出（已回退，`master` 保持干净）。所以下一步必须**先把 `ShadowMapping::render_frame()` 重构成「每帧建一张图」**：Shadow（graphics）+ 6 个 SAT（compute）+ Scene（graphics）都在同一张图里，再统一 `compile()/prepare()/execute()`，然后才谈删手写 barrier。



---

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
- validation 零错误；——**默认路径已满足**（2026-09-12 连跑 5 次无 VUID，见 §5.8）；图路径（`glTFLoading`）仍有一条 presentable image acquire 相关错误待修
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
~~修 swapchain 同步~~（已完成）：每张 swapchain image 一个 render-finished semaphore、`SUBOPTIMAL` 视为本帧可用、ImGui render pass 非法 initialLayout、以及图路径的 acquire/present 记账（executor 导入表未覆盖，见 §5.9）均已修复；默认路径与 `glTFLoading` 图路径的 validation 都已归零，resize 连续 4 次也干净；
6. 迁移 ShadowMapping（图拥有 shadow map、SAT 走 compute pass、删掉手写 barrier）与 Deferred，并以 Validation 结果作为验收；——**屏幕 pass 已完成（§5.10）**；剩余：给 executor 补 compute/dispatch + storage image 支持，再把 shadow map 与 SAT 链搬进图；Deferred 同批处理；
7. 升级 frames in flight，解决 semaphore 跨 swapchain image 复用问题；
8. 再加入 GPU-driven；
9. 最后在 RTX 机器上加入光追；
10. 全阶段持续积累 benchmark 数据。

核心原则：

> 不要为了增加功能数量而扩展范围。
> 每一个新模块都必须进入同一条 RenderGraph 路径，并且产生可验证结果。

---

## 14. 主线：FrameGraph + Dynamic Rendering + GPU-driven（2026-09-12 定稿）

项目只保留一条技术主线，所有新功能都必须落在这条线上，不再新增互不相干的 Demo：

```text
FrameGraph（唯一同步/布局来源）
        +
Dynamic Rendering（Vulkan 1.3 core，取代 VkRenderPass/VkFramebuffer 兼容路径）
        +
GPU-driven Rendering（bindless + compute culling + indirect draw）
        +
可复现 Benchmark（同一场景、同一口径、可回指原始数据）
```

### 14.1 基准场景三预设（定稿）

| 预设 | 用途 | 规模 | 备注 |
| --- | --- | --- | --- |
| `sponza` | 正确性基线 / deferred / 阴影 / 光照对比 | 常规单场景 | Crytek Sponza（Intel "New Sponza" glTF 版本），CC-BY |
| `sponza_instanced_100k` | GPU-driven 主力压测（bindless + frustum/Hi-Z culling + indirect draw） | Sponza 布局 + 10 万实例 | 合成实例，覆盖“真实遮挡 + 海量 draw” |
| `bistro` | 重几何压力（带宽 / draw 提交 / LOD） | 外景 ~1M+ 三角形 | Amazon Lumberyard Bistro，需保留 attribution |

`FlightHelmet`（仓库已有）继续作为材质 / IBL 正确性的小场景。资产通过 `scripts/fetch-benchmark-scenes.ps1` 下载到被 Git 忽略的目录，不把大资产入库。

### 14.2 Benchmark 口径（与 §9 一致，硬约束）

- 固定相机路径、分辨率、warmup 帧与采样帧数；
- GPU timestamp 分 pass 统计，输出 CSV/JSON + P50/P95/P99；
- 每份结果记录 GPU / 驱动 / SDK / OS / 锁频状态（`nvidia-smi -lgc/-lmc`）；
- 简历里的每个数字都必须能回指到某次运行产生的原始 CSV。

### 14.3 Dynamic Rendering 迁移计划

目标：图成为唯一的同步与布局来源，`VkRenderPass` / `VkFramebuffer` 从渲染路径退场。

1. **executor 增加 dynamic rendering 路径**：`acquire_rendering_info()` 返回 `VkRenderingInfo`（含各附件的 view / layout / load-store / clear value），与现有 `acquire_render_target()` 并存；
2. **swapchain 图像回到图内**：去掉 `externally_synchronized`，由图标出 `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR`（需要一个显式的 present 过渡用法 / pass）；
3. **管线改用 `VkPipelineRenderingCreateInfo`**：`GraphicsPipelineCreateInfoPack` 增加 dynamic rendering 模式，render pass 兼容性约束（例如 dependencyCount 必须相同）随之消失；
4. **按难度迁移**：`FrameGraphOffScreenTest` → `glTFLoading` → `ShadowMapping` → 其余 VulkanTests；最后删除 RHI 的 render pass/framebuffer 与 KHR 回退分支，把 `dynamicRendering` 设为硬性要求（本机 RTX 5090 D 已满足）。

**进度（2026-09-12）**：第 1–2 步已完成并通过 `glTFLoading` 验收。

- executor 新增 `acquire_rendering_info()`（返回 `VkRenderingInfo` + 各附件 `VkRenderingAttachmentInfo`，layout 取 pass 声明的 layout、load/store/clear 由调用方给出），与 `acquire_render_target()` 并存；
- `glTFLoading` 全链路改用 dynamic rendering：管线用 `VkPipelineRenderingCreateInfo`（不再绑 render pass），pass 内 `vkCmdBeginRendering` / `vkCmdEndRendering`；swapchain 图像**不再外部同步**，由图标出 `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR`（末尾加了一个空 body 的 `Present` transfer pass，用 `usage::present()`）；
- 验证（RTX 5090 D）：`glTFLoading` 作启动 demo（临时改动，仅用于验证）**exit code 0、零 VUID、无 leaked objects、stderr 为空、60 FPS**；默认路径 `BuffersAndPictureTest` exit 0 / 零 VUID；`FrameGraphTests` 129 checks / 0 failures。

下一步：`FrameGraphOffScreenTest`（屏幕合成 pass）→ `ShadowMapping`（Shadow + Scene）→ 其余 VulkanTests；随后删除 RHI 的 render pass/framebuffer 与 `VulkanRenderPassWithFramebuffers.h`，把 `dynamicRendering` 设为硬性要求。
