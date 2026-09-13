cmake_minimum_required(VERSION 3.28)

project(VulkanRenderer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ---------------------------------------------------------------------------
# MSVC /showIncludes 前缀修复（Ninja 的头文件依赖依赖它）
#
# Ninja 用 rules.ninja 里的 msvc_deps_prefix 从 cl.exe /showIncludes 输出中剥掉本地化前缀。
# CMake 在配置期探测该前缀时按系统 ANSI 代码页解码 cl 的输出，而当前 MSVC 直接输出 UTF-8，
# 于是在非 ASCII locale（例如中文 Windows）下探测结果会变成乱码，与 cl.exe 的真实输出不匹配：
# Ninja 解析不到任何头文件依赖，改动 .h 不会触发重编译——“改了头文件却没重编”会让构建产物
# 与源码不一致，验证结果不可信。
#
# 这里在配置期自己做一次探测：编译一个只 include 一个头文件的最小 TU，按 UTF-8 直接读取
# cl.exe 的输出并取出真实前缀；只有与 CMake 的探测结果不一致时才覆盖（英文环境下两者相同，
# 不受影响）。探测失败时保持 CMake 的原有行为。
# ---------------------------------------------------------------------------
if(MSVC AND CMAKE_CXX_COMPILER)
    set(_vkr_showincludes_dir "${CMAKE_BINARY_DIR}/CMakeFiles/showincludes-prefix-probe")
    file(MAKE_DIRECTORY "${_vkr_showincludes_dir}")
    file(WRITE "${_vkr_showincludes_dir}/probe.h" "#pragma once\n")
    file(WRITE "${_vkr_showincludes_dir}/probe.cpp" "#include \"probe.h\"\n")
    file(TO_NATIVE_PATH "${_vkr_showincludes_dir}/probe.cpp" _vkr_showincludes_source)
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /c /showIncludes "${_vkr_showincludes_source}"
        WORKING_DIRECTORY "${_vkr_showincludes_dir}"
        OUTPUT_FILE "${_vkr_showincludes_dir}/probe.out"
        ERROR_QUIET
        RESULT_VARIABLE _vkr_showincludes_result)

    set(_vkr_showincludes_prefix "")
    if(_vkr_showincludes_result EQUAL 0)
        file(READ "${_vkr_showincludes_dir}/probe.out" _vkr_showincludes_output)
        string(REGEX MATCH "[^\r\n]*probe\\.h" _vkr_showincludes_line "${_vkr_showincludes_output}")
        file(TO_NATIVE_PATH "${_vkr_showincludes_dir}/probe.h" _vkr_showincludes_header)
        string(LENGTH "${_vkr_showincludes_line}" _vkr_line_length)
        string(LENGTH "${_vkr_showincludes_header}" _vkr_header_length)
        # 该行形如 <前缀><probe.h 的绝对路径>，把路径剥掉剩下的就是前缀本身。
        string(FIND "${_vkr_showincludes_line}" "${_vkr_showincludes_header}" _vkr_header_pos)
        math(EXPR _vkr_prefix_length "${_vkr_line_length} - ${_vkr_header_length}")
        if(_vkr_header_pos GREATER 0 AND _vkr_header_pos EQUAL ${_vkr_prefix_length})
            string(SUBSTRING "${_vkr_showincludes_line}" 0 ${_vkr_header_pos} _vkr_showincludes_prefix)
        endif()
    endif()

    if(_vkr_showincludes_prefix AND
       NOT _vkr_showincludes_prefix STREQUAL CMAKE_CL_SHOWINCLUDES_PREFIX)
        message(STATUS "MSVC /showIncludes prefix repaired: \"${_vkr_showincludes_prefix}\" (CMake detected: \"${CMAKE_CL_SHOWINCLUDES_PREFIX}\")")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_vkr_showincludes_prefix}")
        set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${_vkr_showincludes_prefix}")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_vkr_showincludes_prefix}")
    endif()
endif()

set(VULKAN_RENDERER_EXTERNAL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/External"
    CACHE PATH "Directory containing the pinned VulkanRenderer dependencies")

set(_required_dependency_files
    "glfw/include/GLFW/glfw3.h"
    "glm/glm.hpp"
    "imgui/imgui.h"
    "imgui/backends/imgui_impl_glfw.cpp"
    "imgui/backends/imgui_impl_vulkan.cpp"
    "tinygltf/tiny_gltf.h"
    "tinygltf/json.hpp"
    "stb/stb_image.h"
    "bcdec/bcdec.h"
    "ktx/include/ktx.h"
    "ktx/include/ktxvulkan.h"
    "ktx/lib/texture.c"
    "ktx/lib/vkloader.c"
)

set(_missing_dependency_files)
foreach(_relative_path IN LISTS _required_dependency_files)
    if(NOT EXISTS "${VULKAN_RENDERER_EXTERNAL_DIR}/${_relative_path}")
        list(APPEND _missing_dependency_files "${_relative_path}")
    endif()
endforeach()

if(_missing_dependency_files)
    string(JOIN "\n  - " _missing_dependency_list ${_missing_dependency_files})
    message(FATAL_ERROR
        "Missing pinned dependencies under ${VULKAN_RENDERER_EXTERNAL_DIR}:\n"
        "  - ${_missing_dependency_list}\n\n"
        "Run:\n"
        "  pwsh -File scripts/bootstrap-dependencies.ps1\n"
        "or pass -DVULKAN_RENDERER_EXTERNAL_DIR=<path> to CMake.")
endif()

if(WIN32)
    set(_glfw_library "${VULKAN_RENDERER_EXTERNAL_DIR}/glfw/lib-vc2022/glfw3.lib")
    if(NOT EXISTS "${_glfw_library}")
        message(FATAL_ERROR
            "Missing Windows GLFW import library: ${_glfw_library}\n"
            "Run scripts/bootstrap-dependencies.ps1 to restore the pinned GLFW package.")
    endif()

    add_library(glfw::glfw STATIC IMPORTED GLOBAL)
    set_target_properties(glfw::glfw PROPERTIES
        IMPORTED_LOCATION "${_glfw_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_RENDERER_EXTERNAL_DIR}/glfw/include")
else()
    find_package(glfw3 REQUIRED)
    add_library(glfw::glfw ALIAS glfw)
endif()

# glslc 用于在构建时把 Shader/**/*.shader 编译成运行期加载的 SPIR-V。
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)

# ShaderLoader.h selects the debug or release shaderc library with a pragma.
# Keep the matching SDK lib directory on the linker search path instead of
# depending on a manually copied External/lib directory.
get_filename_component(_vulkan_library_dir "${Vulkan_LIBRARY}" DIRECTORY)

set(IMGUI_DIR "${VULKAN_RENDERER_EXTERNAL_DIR}/imgui")
file(GLOB IMGUI_CORE_SOURCES "${IMGUI_DIR}/*.cpp")
set(IMGUI_BACKEND_SOURCES
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

# assimp：固定版本源码，只构建需要的 importer（FBX/OBJ/PLY），静态链接进可执行文件，避免部署额外 DLL。
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_IMPORTER ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${VULKAN_RENDERER_EXTERNAL_DIR}/assimp" "${CMAKE_BINARY_DIR}/assimp" EXCLUDE_FROM_ALL)

set(KTX_DIR "${VULKAN_RENDERER_EXTERNAL_DIR}/ktx")
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/hashtable.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/errstr.c
    ${KTX_DIR}/lib/vkloader.c
    ${KTX_DIR}/lib/vk_funcs.c
)

add_library(ktx STATIC ${KTX_SOURCES})
target_include_directories(ktx
    PUBLIC
        ${KTX_DIR}/include
        ${KTX_DIR}/other_include
    PRIVATE
        ${KTX_DIR}/lib
)
target_link_libraries(ktx PUBLIC Vulkan::Vulkan)

# FrameGraph v1 核心：只依赖 Vulkan 头文件，不创建 VkDevice，因此可以独立编译和单元测试。
add_library(FrameGraphCore STATIC
    VulkanBase/FrameGraph/FrameGraph.cpp
    VulkanBase/FrameGraph/FrameGraphBarrier.cpp
    VulkanBase/FrameGraph/FrameGraphCompiler.cpp
    VulkanBase/FrameGraph/FrameGraph.h
    VulkanBase/FrameGraph/FrameGraphBarrier.h
    VulkanBase/FrameGraph/FrameGraphCompiler.h
    VulkanBase/FrameGraph/FrameGraphTypes.h
)

target_include_directories(FrameGraphCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/VulkanBase/FrameGraph
)
target_link_libraries(FrameGraphCore PUBLIC Vulkan::Vulkan)

if(MSVC)
    target_compile_options(FrameGraphCore PRIVATE /utf-8)
endif()

add_executable(VulkanRenderer
    main.cpp
    Start.h
    Launcher/VulkanAppLauncher.cpp
    Launcher/VulkanAppLauncher.h
    VulkanBase/VulkanCore.h
    VulkanBase/components/VulkanInstance.h
    VulkanBase/components/VulkanDevice.h
    VulkanBase/components/VulkanSync.h
    VulkanBase/VulkanSwapchainManager.h
    VulkanBase/VulkanExecutionManager.h
    VulkanBase/components/VulkanCommand.h
    VulkanBase/VulkanPipelineManager.h
    VulkanBase/components/VulkanRenderPassWithFramebuffers.h
    VulkanBase/components/VulkanPipepline.h
    Demos/SharedResourceManager.h
    VulkanBase/components/VulkanShaderModule.h
    VulkanBase/components/VulkanMemory.h
    Geometry/Vertex.h
    VulkanBase/components/VulkanDescriptor.h
    Interaction/Texture.h
    Interaction/DdsImage.h
    Interaction/HdrImage.h
    bcdec_implementation.cpp
    VulkanBase/VulkanContext.h
    VulkanBase/components/VulkanOperation.h
    stb_image_implementation.cpp
    VulkanBase/components/VulkanSampler.h
    VulkanBase/components/VulkanTexture.h
    VulkanBase/components/VulkanTextureCube.h
    Demos/DemoManager.h
    UI/ImGuiManager.h
    Demos/DemoCategories.h
    Demos/DemoBase.h
    Demos/VulkanTests/BuffersAndPictureTest.h
    Demos/VulkanTests/ImagelessFramebufferTest.h
    Demos/VulkanTests/DynamicRenderingTest.h
    Demos/VulkanTests/OffScreenRenderingTest.h
    Demos/VulkanTests/FrameGraphOffScreenTest.h
    VulkanBase/FrameGraph/FrameGraphExecutor.cpp
    VulkanBase/FrameGraph/FrameGraphExecutor.h
    Demos/VulkanTests/DepthAttachmentTest.h
    Demos/VulkanTests/DeferredRenderingTest.h
    Shader/ShaderLoader.h
    Shader/ShaderLoader.cpp
    Geometry/Model.h
    Geometry/AssimpModelLoader.h
    Demos/BasicRendering/glTFLoading.h
    Interaction/Camera.h
    Demos/DemoBase3D.h
    Demos/BasicRendering/ShadowMapping.h
    Demos/BasicRendering/PbrIbl.h
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
)

target_include_directories(VulkanRenderer
    PRIVATE
        ${VULKAN_RENDERER_EXTERNAL_DIR}/glm
        ${VULKAN_RENDERER_EXTERNAL_DIR}/tinygltf
        ${VULKAN_RENDERER_EXTERNAL_DIR}/stb
        ${VULKAN_RENDERER_EXTERNAL_DIR}/bcdec
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        # assimp 必须排在 ktx 之前：External/ktx/other_include 里带了一份旧版 assimp 头文件
        # （没有 aiTextureType_BASE_COLOR 等 PBR 枚举），而 ktx 的 PUBLIC include 会排在链接目标的前面。
        # 不显式提前，Geometry/AssimpModelLoader.h 会解析到旧头文件并报 C2065。
        ${VULKAN_RENDERER_EXTERNAL_DIR}/assimp/include
)

target_compile_definitions(VulkanRenderer PRIVATE
    PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

target_link_directories(VulkanRenderer PRIVATE "${_vulkan_library_dir}")

target_link_libraries(VulkanRenderer
    PRIVATE
        glfw::glfw
        Vulkan::Vulkan
        ktx
        assimp
        FrameGraphCore
)

# 着色器以 GLSL 源码（*.shader）入库，运行期按 PROJECT_ROOT_PATH/Shader/**.spv
# 加载 SPIR-V，因此这里把编译产物生成到源码树中与源码同名的位置。
file(GLOB_RECURSE _shader_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Shader/*.shader")

set(_glslc_extra_flags)
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(_glslc_extra_flags -O)
endif()

set(_shader_binaries)
foreach(_shader_source IN LISTS _shader_sources)
    string(REGEX REPLACE "\\.shader$" ".spv" _shader_binary "${_shader_source}")
    add_custom_command(
        OUTPUT "${_shader_binary}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" ${_glslc_extra_flags} "${_shader_source}" -o "${_shader_binary}"
        DEPENDS "${_shader_source}"
        COMMENT "Compiling shader ${_shader_source}"
        VERBATIM)
    list(APPEND _shader_binaries "${_shader_binary}")
endforeach()

add_custom_target(VulkanRendererShaders ALL DEPENDS ${_shader_binaries})
add_dependencies(VulkanRenderer VulkanRendererShaders)

# Debug 链接的是 shaderc_sharedd.lib，运行时需要同名 DLL。把它部署到可执行文件
# 旁边，这样直接双击/调试运行都无需把 Vulkan SDK 的 Bin 目录加进 PATH。
# Release 链接静态的 shaderc_combined.lib，没有额外的运行时依赖。
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    get_filename_component(_vulkan_bin_dir "${Vulkan_GLSLC_EXECUTABLE}" DIRECTORY)
    set(_shaderc_runtime_dll "${_vulkan_bin_dir}/shaderc_sharedd.dll")
    if(EXISTS "${_shaderc_runtime_dll}")
        add_custom_command(TARGET VulkanRenderer POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_shaderc_runtime_dll}"
                "$<TARGET_FILE_DIR:VulkanRenderer>"
            COMMENT "Deploying shaderc_sharedd.dll"
            VERBATIM)
    else()
        message(WARNING
            "shaderc_sharedd.dll was not found next to glslc; add <VulkanSDK>/Bin "
            "to PATH before running the Debug build.")
    endif()
endif()

# FrameGraphExecutor 会引用 Start.h / VulkanCore 等应用侧头文件，单独打开 /utf-8 避免
# 中文注释在代码页 936 下产生 C4819 警告。
if(MSVC)
    set_source_files_properties(VulkanBase/FrameGraph/FrameGraphExecutor.cpp PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()

if(MSVC)
    set_target_properties(VulkanRenderer PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # 源码是 UTF-8（含中文注释）。不指定编码时 MSVC 会按本地代码页（如 936）解析，
    # 个别多字节序列会吞掉后续代码或触发 C4819，表现为莫名其妙的语法错误。
    target_compile_options(VulkanRenderer PRIVATE /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()
