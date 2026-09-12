#include "Launcher/VulkanAppLauncher.h"

#include <string>

// 用法：VulkanRenderer [--demo <demo 名>] [--scene <资产名或路径>]
//   --demo  按菜单名或 demo 类型名选择启动 demo（例如 glTFLoading / ShadowMapping）
//   --scene 选择要加载的 glTF 资产（绝对路径，或 Assets/models 下的相对路径/名字）
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--frames" || argument == "--warmup" || argument == "--csv") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (argument == "--frames") benchmark_frames = std::stoi(value);
            else if (argument == "--warmup") benchmark_warmup = std::stoi(value);
            else benchmark_csv = value;
            continue;
        }
        if ((argument == "--demo" || argument == "--scene") && i + 1 < argc) {
            std::string& target = (argument == "--demo") ? command_line_demo : command_line_scene;
            target = argv[++i];
        }
    }
    VulkanAppLauncher::getSingleton(default_window_size).run();
    return 0;
}