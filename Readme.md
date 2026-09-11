# Vulkan Renderer

一个以 Vulkan 1.3 为基础、用于学习和验证现代图形渲染技术的渲染器项目。项目基于 `SaschaWillems/Vulkan` 等公开资料重构，当前包含 RHI 封装、glTF 加载、Shadow Mapping、Deferred Rendering 等能力。

## 当前状态

P0 构建与设备初始化已完成：

- 依赖版本已固定，干净工作区可以一键恢复依赖并完成配置、构建；
- Vulkan SDK 通过 `find_package(Vulkan)` 自动发现，不再依赖硬编码本机路径；
- `imagelessFramebuffer`、`dynamicRendering` 和 `samplerAnisotropy` 在 `vkCreateDevice()` 前按设备能力显式启用；
- compute command pool 已改为使用 compute queue family；
- Debug 构建和启动测试已在 `AMD Radeon(TM) Graphics` / Vulkan `1.3.217` / Vulkan SDK `1.4.313.1` 上验证。

尚未实现，不能对外陈述为已完成：

- PBR / IBL 仍处于 WIP；
- FrameGraph、Synchronization 2、Frames in Flight；
- GPU-driven Rendering、Bindless、Indirect Draw；
- Hardware Ray Tracing；
- CSV / JSON Benchmark、P50 / P95 / P99 和报告体系。

详细路线、验证记录和简历事实边界见 [Docs/framegraph_gpu_driven_roadmap.md](Docs/framegraph_gpu_driven_roadmap.md)。

## 环境要求

- Windows 10/11 x64
- Visual Studio 2022 或更新版本，且安装 `使用 C++ 的桌面开发` 工作负载
- CMake 3.28+
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 1.3+
- Ninja

Vulkan SDK 安装后，确保 `VULKAN_SDK` 环境变量指向 SDK 根目录。项目保留 Vulkan 1.1/1.2 的扩展降级路径，但当前验证环境为 Vulkan 1.3。

## 从干净工作区构建

在 PowerShell 中执行：

```powershell
.\scripts\build.ps1 -Bootstrap -Configuration Debug
```

`-Bootstrap` 会按照脚本中固定的版本下载 GLFW 3.4、GLM 1.0.1、Dear ImGui 1.92.3、tinygltf 2.9.6、KTX 3.0.1 和 stb，并放置到被 Git 忽略的 `External/` 目录。已经完整恢复的依赖会跳过；目录存在但不完整时脚本会停止，不会覆盖已有内容。

仅恢复依赖：

```powershell
.\scripts\bootstrap-dependencies.ps1
```

手动配置和构建需要在已加载 Visual Studio 环境的终端中执行：

```powershell
cmake --preset windows-ninja-debug
cmake --build --preset windows-ninja-debug --parallel
```

Release 构建：

```powershell
.\scripts\build.ps1 -Configuration Release
```

构建产物位于：

```text
out/build/windows-ninja-debug/VulkanRenderer.exe
out/build/windows-ninja-release/VulkanRenderer.exe
```

## 依赖策略

依赖版本固定在 `scripts/bootstrap-dependencies.ps1` 中，不使用浮动标签；`External/` 保持在 `.gitignore` 中，避免把第三方源码和二进制混入项目历史。若需要离线构建，可将同一版本依赖预先放入 `External/`，然后向 CMake 传入：

```powershell
cmake --preset windows-ninja-debug -DVULKAN_RENDERER_EXTERNAL_DIR=D:/path/to/External
```

## 仓库地址

公开仓库：

https://github.com/SinonShizuku/VulkanDemoPlatform

本地 `origin` 仍使用旧地址 `git@github.com:SinonShizuku/VulkanRenderer.git`，GitHub 会将其重定向到当前 canonical repository。