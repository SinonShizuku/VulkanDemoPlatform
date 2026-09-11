# Vulkan Renderer

一个以 Vulkan 1.3 为基础、用于学习和验证现代图形渲染技术的 Demo 平台。当前基线包含 RHI 封装、glTF 加载、Shadow Mapping、Deferred Rendering 等能力；FrameGraph、GPU-driven Rendering 与硬件光追属于后续重构目标，不应当作已完成能力陈述。

## 环境要求

- Windows 10/11 x64
- Visual Studio 2022 或更新版本，且安装 `使用 C++ 的桌面开发` 工作负载
- CMake 3.28+
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 1.3+
- Ninja

Vulkan SDK 安装后，确保 `VULKAN_SDK` 环境变量指向 SDK 根目录。CMake 通过 `find_package(Vulkan)` 自动发现 Vulkan，不再依赖仓库中的硬编码本机路径。

## 从干净工作区构建

在 PowerShell 中执行：

```powershell
.\scripts\build.ps1 -Bootstrap -Configuration Debug
```

`-Bootstrap` 会按照脚本中固定的版本下载 GLFW 3.4、GLM 1.0.1、Dear ImGui 1.92.3、tinygltf 2.9.6、KTX 3.0.1 和 stb，并放置到被 Git 忽略的 `External/` 目录。脚本不会覆盖已经存在但不完整的依赖目录。

仅恢复依赖：

```powershell
.\scripts\bootstrap-dependencies.ps1
```

手动配置和构建：

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

依赖版本固定在 `scripts/bootstrap-dependencies.ps1` 中，不使用浮动标签；`External/` 仍保持在 `.gitignore` 中，避免把第三方源码和二进制混入项目历史。若需要离线构建，可将同一版本依赖预先放入 `External/`，然后向 CMake 传入：

```powershell
cmake --preset windows-ninja-debug -DVULKAN_RENDERER_EXTERNAL_DIR=D:/path/to/External
```

## 技术文档

当前 FrameGraph、Synchronization、GPU-driven Rendering、Hardware RT 和 Benchmark 的实施路线保存在本地 `Docs/framegraph_gpu_driven_roadmap.md`。该文档将在对应实现经过验证后随证据一并提交，未实现内容不进入简历结论。