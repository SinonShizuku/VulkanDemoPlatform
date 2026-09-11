# Vulkan Renderer

一个以 Vulkan 1.3 为基础、用于学习和验证现代图形渲染技术的渲染器项目。项目基于 `SaschaWillems/Vulkan` 等公开资料重构，当前包含 RHI 封装、glTF 加载、Shadow Mapping、Deferred Rendering 等能力。

## 当前状态

P0 构建与设备初始化已完成：

- 依赖版本已固定，干净工作区可以一键恢复依赖并完成配置、构建；
- Vulkan SDK 通过 `find_package(Vulkan)` 自动发现，不再依赖硬编码本机路径；
- `imagelessFramebuffer`、`dynamicRendering` 和 `samplerAnisotropy` 在 `vkCreateDevice()` 前按设备能力显式启用；
- compute command pool 已改为使用 compute queue family；
- 着色器以 GLSL 源码（`Shader/**/*.shader`）入库，构建时由 SDK 的 `glslc` 增量编译为运行期加载的 SPIR-V（同名 `.spv`）；
- Debug 构建会把 `shaderc_sharedd.dll` 自动部署到可执行文件旁，无需手工配置 `PATH`（Release 链接静态 shaderc）；
- Debug 构建和启动测试已在 `AMD Radeon(TM) Graphics` / Vulkan `1.3.217` / Vulkan SDK `1.4.313.1` 上验证；
- 构建与运行也在 `NVIDIA GeForce RTX 5090 D` / 驱动 `596.36` / Vulkan SDK `1.4.357.0` / Visual Studio `18.10` 上验证（`BuffersAndPictureTest`、`ShadowMapping` 均以 60 FPS 正常渲染）。

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
- CMake 3.28+（不在 `PATH` 时，构建脚本会自动使用 Visual Studio 自带的 CMake）
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 1.3+，且需包含 `glslc` 与 shaderc（LunarG 官方安装包的默认组件）
- Ninja（不在 `PATH` 时，构建脚本会自动使用 Visual Studio 自带的 Ninja）

Vulkan SDK 安装后，确保 `VULKAN_SDK` 环境变量指向 SDK 根目录；若该变量缺失，`scripts/build.ps1` 会自动在 `C:\VulkanSDK`、`D:\VulkanSDK` 下选择最新版本。构建期需要 SDK 的 `glslc`（编译着色器），Debug 链接需要 `shaderc_sharedd.lib`。项目保留 Vulkan 1.1/1.2 的扩展降级路径，但当前验证环境为 Vulkan 1.3。

## 从干净工作区构建

在 PowerShell 中执行：

```powershell
.\scripts\build.ps1 -Bootstrap -Configuration Debug
```

`-Bootstrap` 会按照脚本中固定的版本下载 GLFW 3.4、GLM 1.0.1、Dear ImGui 1.92.3、tinygltf 2.9.6、KTX 3.0.1 和 stb，并放置到被 Git 忽略的 `External/` 目录。已经完整恢复的依赖会跳过；目录存在但不完整时脚本会停止，不会覆盖已有内容。

构建过程会调用 SDK 的 `glslc` 把 `Shader/**/*.shader` 编译成运行期加载的同名 `.spv`，并把 Debug 需要的 `shaderc_sharedd.dll` 复制到可执行文件旁；两者都是增量执行的，无需手工介入。

仅恢复依赖：

```powershell
.\scripts\bootstrap-dependencies.ps1
```

手动配置和构建需要在已加载 Visual Studio 环境的终端中执行：

```powershell
cmake --preset windows-ninja-debug
cmake --build --preset windows-ninja-debug --parallel
```

着色器编译是构建目标 `VulkanRendererShaders` 的一部分，`cmake --build` 即可完成。构建后可直接运行，或让脚本构建完立即启动：

```powershell
.\out\build\windows-ninja-debug\VulkanRenderer.exe
.\scripts\build.ps1 -Configuration Debug -Run
```

Release 构建：

```powershell
.\scripts\build.ps1 -Configuration Release
```

构建产物位于：

```text
out/build/windows-ninja-debug/VulkanRenderer.exe
out/build/windows-ninja-debug/shaderc_sharedd.dll   # Debug 运行时依赖，构建时自动部署
out/build/windows-ninja-release/VulkanRenderer.exe
```

## 着色器

GLSL 源码以 `*.shader` 扩展名放在 `Shader/` 下（例如 `Shader/VulkanTests/Texture.vert.shader`），构建时由 CMake 调用 Vulkan SDK 的 `glslc` 增量编译为同名 `.spv`：

```powershell
# 构建会自动执行，等价的手动命令：
glslc Shader/VulkanTests/Texture.vert.shader -o Shader/VulkanTests/Texture.vert.spv
```

源文件使用 `#pragma shader_stage(...)` 声明阶段，因此无需额外传入 `-fshader-stage`。`Shader/**/*.spv` 是构建产物，已在 `.gitignore` 中忽略；运行期按 `PROJECT_ROOT_PATH/Shader/<相对路径>` 加载 SPIR-V，新增着色器后重新构建即可（`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 会自动感知新增文件）。

## 依赖策略

依赖版本固定在 `scripts/bootstrap-dependencies.ps1` 中，不使用浮动标签；`External/` 保持在 `.gitignore` 中，避免把第三方源码和二进制混入项目历史。若需要离线构建，可将同一版本依赖预先放入 `External/`，然后向 CMake 传入：

```powershell
cmake --preset windows-ninja-debug -DVULKAN_RENDERER_EXTERNAL_DIR=D:/path/to/External
```

## 仓库地址

公开仓库：

https://github.com/SinonShizuku/VulkanDemoPlatform

本地 `origin` 仍使用旧地址 `git@github.com:SinonShizuku/VulkanRenderer.git`，GitHub 会将其重定向到当前 canonical repository。