#pragma once
#include "DemoBase.h"
#include "../Interaction/Camera.h"

class DemoBase3D : public DemoBase {
public:
    DemoBase3D(DemoType type, DemoCategoryType category,  const std::string& description = "", GLFWwindow *window = nullptr)
        : DemoBase(type, category, description){
        this->window = window;
        show_demo_window = false;
    }

    void handle_mouse_button(int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS)
                input_state.left_mouse_button_down = true;
            else
                input_state.left_mouse_button_down = false;
        }
    }
    void handle_mouse_move(double x, double y) {
        if (input_state.left_mouse_button_down) {
            glm::vec2 delta = glm::vec2(x, y) - input_state.mouse_pos;
            camera.rotate(glm::vec3(-delta.y * camera.rotation_speed, delta.x * camera.rotation_speed, 0.0f));
        }
        input_state.mouse_pos = glm::vec2(x, y);
    }
    static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
        auto* demo = static_cast<DemoBase3D*>(glfwGetWindowUserPointer(window));
        if (demo) {
            if (demo->prev_mouse_button_callback) {
                demo->prev_mouse_button_callback(window, button, action, mods);
            }
            if (ImGui::GetIO().WantCaptureMouse) {
                return; // ImGui 正在使用点击，不要旋转相机
            }
            demo->handle_mouse_button(button, action, mods);
        }
    }
    static void glfw_mouse_move_callback(GLFWwindow* window, double x, double y) {
        auto* demo = static_cast<DemoBase3D*>(glfwGetWindowUserPointer(window));
        if (demo) {
            if (demo->prev_cursor_pos_callback) {
                demo->prev_cursor_pos_callback(window, x, y);
            }
            if (ImGui::GetIO().WantCaptureMouse) {
                return; // 鼠标在 ImGui 窗口上，不要旋转相机
            }
            demo->handle_mouse_move(x, y);
        }
    }

    void update(float frame_timer_from_manager) override {
        this->frame_timer = frame_timer_from_manager;
        if (!paused)
        {
            timer += timer_speed * this->frame_timer;
            if (timer > 1.0)
            {
                timer -= 1.0f;
            }
        }
    }

protected:
    Camera camera;
    GLFWmousebuttonfun prev_mouse_button_callback = nullptr;
    GLFWcursorposfun prev_cursor_pos_callback = nullptr;
    void* prev_window_user_pointer = nullptr;
    struct InputState {
        bool left_mouse_button_down = false;
        glm::vec2 mouse_pos;
    } input_state;

    float frame_timer = 0.0f; // 存储本帧的增量时间
    float timer = 0.0f;       // 0-1 循环的动画计时器
    float timer_speed = 0.25f;
    bool paused = false;

    struct UniformData {
        glm::mat4 projection = flip_vertical(glm::perspective(glm::radians(60.0f), (float)window_size.width / (float)window_size.height, 0.1f, 256.0f));
        glm::mat4 model;
        glm::vec4 light_pos = glm::vec4(5.0f, 5.0f, -5.0f, 1.0f);
        glm::vec4 view_pos;
    } uniform_data;

    void register_glfw_callback() {
        prev_window_user_pointer = glfwGetWindowUserPointer(window);
        glfwSetWindowUserPointer(window, this);
        prev_mouse_button_callback = glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
        prev_cursor_pos_callback = glfwSetCursorPosCallback(window, glfw_mouse_move_callback);
    }

    void clean_up_glfw_callback() {
        glfwSetMouseButtonCallback(window, prev_mouse_button_callback);
        glfwSetCursorPosCallback(window, prev_cursor_pos_callback);
        glfwSetWindowUserPointer(window, prev_window_user_pointer);
    }
};
