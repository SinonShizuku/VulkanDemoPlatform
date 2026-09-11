cmake_minimum_required(VERSION 3.28)

project(VulkanRenderer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
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

find_package(Vulkan 1.3 REQUIRED)

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
    VulkanBase/VulkanContext.h
    VulkanBase/components/VulkanOperation.h
    stb_image_implementation.cpp
    VulkanBase/components/VulkanSampler.h
    VulkanBase/components/VulkanTexture.h
    Demos/DemoManager.h
    UI/ImGuiManager.h
    Demos/DemoCategories.h
    Demos/DemoBase.h
    Demos/VulkanTests/BuffersAndPictureTest.h
    Demos/VulkanTests/ImagelessFramebufferTest.h
    Demos/VulkanTests/DynamicRenderingTest.h
    Demos/VulkanTests/OffScreenRenderingTest.h
    Demos/VulkanTests/DepthAttachmentTest.h
    Demos/VulkanTests/DeferredRenderingTest.h
    Shader/ShaderLoader.h
    Shader/ShaderLoader.cpp
    Geometry/Model.h
    Demos/BasicRendering/glTFLoading.h
    Interaction/Camera.h
    Demos/DemoBase3D.h
    Demos/BasicRendering/ShadowMapping.h
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
)

target_include_directories(VulkanRenderer
    PRIVATE
        ${VULKAN_RENDERER_EXTERNAL_DIR}/glm
        ${VULKAN_RENDERER_EXTERNAL_DIR}/tinygltf
        ${VULKAN_RENDERER_EXTERNAL_DIR}/stb
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
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
)

if(MSVC)
    set_target_properties(VulkanRenderer PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()