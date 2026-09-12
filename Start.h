#pragma once

// includes
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <stack>
#include <map>
#include <unordered_map>
#include <span>
#include <memory>
#include <functional>
#include <concepts>
#include <format>
#include <chrono>
#include <numeric>
#include <numbers>
#include <cstring>
#include <functional>
#include <thread>
#include <mutex>
#include <filesystem>

const std::filesystem::path G_PROJECT_ROOT = PROJECT_ROOT_PATH;

// GLM
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
//如果你惯用左手坐标系，在此定义GLM_FORCE_LEFT_HANDED
#ifdef WIN32
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#else
#include<glm1_0/glm.hpp>
#include<glm1_0/gtc/matrix_transform.hpp>
#endif

// GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#pragma comment(lib, "glfw3.lib") //链接编译所需的静态库

// stb_image.h
// #include <stb/stb_image.h>

// Vulkan
#ifdef _WIN32                        //考虑平台是Windows的情况（请自行解决其他平台上的差异）
#define VK_USE_PLATFORM_WIN32_KHR    //在包含vulkan.h前定义该宏，会一并包含vulkan_win32.h和windows.h
#define NOMINMAX                     //定义该宏可避免windows.h中的min和max两个宏与标准库中的函数名冲突
#pragma comment(lib, "vulkan-1.lib") //链接编译所需的静态存根库
#endif
#include <vulkan/vulkan.h>

// imgui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>


// global defines
#ifndef NDEBUG
#define ENABLE_DEBUG_MESSENGER true
#else
#define ENABLE_DEBUG_MESSENGER false
#endif

// 封装Vulkan相关类的一些函数
// 设备销毁后，其子对象已由 vkDestroyDevice 隐式释放：此时再调用 vkDestroy* 不只是多余，
// 还会因为设备句柄已被置空而触发 "vkDestroyXxx: Invalid device"（validation 层会直接崩，
// 进程以 0xC0000409 退出）。进程级 static 对象（例如 demo 里的 shader module）与单例的
// 析构都可能晚于设备销毁，所以设备句柄为空时只清句柄，不再调用 Vulkan。
#define DestroyHandleBy(device,Func) if (handle) { auto destroy_device_handle = (device); if (destroy_device_handle) Func(destroy_device_handle, handle, nullptr); handle = VK_NULL_HANDLE; }
#define MoveHandle handle = other.handle; other.handle = VK_NULL_HANDLE;
#define DefineMoveAssignmentOperator(type) type& operator=(type&& other) { this->~type(); MoveHandle; return *this; }
#define DefineHandleTypeOperator operator decltype(handle)() const { return handle; }
#define DefineAddressFunction const decltype(handle)* Address() const { return &handle; }

// 分割（函数块内）部分执行
#define ExecuteOnce(...) { static bool executed = false; if (executed) return __VA_ARGS__; executed = true; }



// global configs
constexpr VkExtent2D default_window_size = { 1920, 1080 };





// 一些封装内容
// 返回值封装
//情况1：根据函数返回值确定是否抛异常
#ifdef VK_RESULT_THROW
class result_t {
    VkResult result;
public:
    static void(*callback_throw)(VkResult);
    result_t(VkResult result) :result(result) {}
    result_t(result_t&& other) noexcept :result(other.result) { other.result = VK_SUCCESS; }
    ~result_t() noexcept(false) {
        if (uint32_t(result) < VK_RESULT_MAX_ENUM)
            return;
        if (callback_throw)
            callback_throw(result);
        throw result;
    }
    operator VkResult() {
        VkResult result = this->result;
        this->result = VK_SUCCESS;
        return result;
    }
};
inline void(*result_t::callback_throw)(VkResult);

//情况2：若抛弃函数返回值，让编译器发出警告
#elif defined VK_RESULT_NODISCARD
struct [[nodiscard]] result_t {
    VkResult result;
    result_t(VkResult result) :result(result) {}
    operator VkResult() const { return result; }
};
//在本文件中关闭弃值提醒（因为我懒得做处理）
#pragma warning(disable:4834)
#pragma warning(disable:6031)

//情况3：啥都不干
#else
using result_t = VkResult;
#endif

// array_ref封装
template<typename T>
class array_ref {
    T* const pArray = nullptr;
    size_t count = 0;
public:
    //从空参数构造，count为0
    array_ref() = default;
    //从单个对象构造，count为1
    array_ref(T& data) :pArray(&data), count(1) {}
    //从顶级数组构造
    template<size_t elementCount>
    array_ref(T(&data)[elementCount]) : pArray(data), count(elementCount) {}
    //从指针和元素个数构造
    array_ref(T* pData, size_t elementCount) :pArray(pData), count(elementCount) {}
    //复制构造，若T带const修饰，兼容从对应的无const修饰版本的arrayRef构造
    //24.01.07 修正因复制粘贴产生的typo：从pArray(&other)改为pArray(other.Pointer())
    array_ref(const array_ref<std::remove_const_t<T>>& other) :pArray(other.Pointer()), count(other.Count()) {}
    //Getter
    T* Pointer() const { return pArray; }
    size_t Count() const { return count; }
    //Const Function
    T& operator[](size_t index) const { return pArray[index]; }
    T* begin() const { return pArray; }
    T* end() const { return pArray + count; }
    //Non-const Function
    //禁止复制/移动赋值
    array_ref& operator=(const array_ref&) = delete;
};

// 输出方式封装
inline auto& outstream = std::cout;//不是constexpr，因为std::cout具有外部链接

// 命令行参数（main.cpp 解析）：--demo <demo 名>、--scene <资产名或路径>
inline std::string command_line_demo;
inline std::string command_line_scene;

// --scene 的解析规则：绝对路径直接用；否则先按 Assets/models/<name> 找，再按仓库根目录相对路径找，
// 必要时补 .gltf 后缀；找不到时返回按 Assets/models 解析的结果，由调用方给出错误信息。
inline std::filesystem::path resolve_scene_asset(const std::string& name) {
    if (name.empty())
        return {};
    const std::filesystem::path candidate(name);
    if (candidate.is_absolute() && std::filesystem::exists(candidate))
        return candidate;
    const std::filesystem::path under_assets = G_PROJECT_ROOT / "Assets/models" / candidate;
    if (std::filesystem::exists(under_assets))
        return under_assets;
    const std::filesystem::path under_root = G_PROJECT_ROOT / candidate;
    if (std::filesystem::exists(under_root))
        return under_root;
    std::filesystem::path with_extension = under_assets;
    with_extension += ".gltf";
    if (std::filesystem::exists(with_extension))
        return with_extension;
    return under_assets;
}

// 封装闭区间“在内”
template<std::signed_integral T>
constexpr bool between_closed(T min, T num, T max) {
    return ((num - min) | (max - num)) >= 0;
}

// pnext设置封装
static void** set_pnext(void*& pbegin, void* pnext, bool allow_duplicate = false) {
    struct vk_structure_head {
        VkStructureType stype;
        void* pnext;
    };
    if (!pnext)
        return nullptr;

    std::function<void**(void*&, void*, bool)> set_p_next_internal;
    set_p_next_internal = [&set_p_next_internal](void*& p_current_node, void* p_next, bool allow_duplicate) -> void** {
        if (p_current_node == p_next)
            return nullptr;

        if (p_current_node) {
            if (!allow_duplicate &&
                reinterpret_cast<vk_structure_head*>(p_current_node)->stype == reinterpret_cast<vk_structure_head*>(p_next)->stype) {
                return nullptr;
                }
            // 递归调用
            return set_p_next_internal(reinterpret_cast<vk_structure_head*>(p_current_node)->pnext, p_next, allow_duplicate);
        } else {
            // 找到末尾，插入 pNext
            p_current_node = p_next;
            return &p_current_node;
        }
    };

    return set_p_next_internal(pbegin, pnext, allow_duplicate);
}

inline glm::mat4 flip_vertical(const glm::mat4& projection) {
    glm::mat4 _projection = projection;
    _projection[1][1] *= -1;
    return _projection;
}
