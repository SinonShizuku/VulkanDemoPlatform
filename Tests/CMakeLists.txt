# FrameGraph 的 device-free 单元测试：不创建 VkDevice，只验证图编译与 barrier 规划。
add_executable(FrameGraphTests
    FrameGraphTests/main.cpp
)

target_include_directories(FrameGraphTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(FrameGraphTests PRIVATE FrameGraphCore)

if(MSVC)
    target_compile_options(FrameGraphTests PRIVATE /utf-8)
endif()

add_test(NAME FrameGraphTests COMMAND FrameGraphTests)