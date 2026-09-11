#pragma once

// 极简测试框架：项目里没有引入 gtest，这里只保留注册用例、断言与统计所需的最小实现，
// 保证 FrameGraph 的单元测试不依赖任何第三方库。

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace testh {

class Runner {
public:
    static Runner& instance() {
        static Runner runner;
        return runner;
    }

    void add_case(std::string name, std::function<void()> body) {
        cases_.push_back(Case{ std::move(name), std::move(body) });
    }

    void check(bool condition, const char* expression, const char* file, int line) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::printf("    [FAIL] %s (%s:%d)\n", expression, file, line);
        }
    }

    template <typename Actual, typename Expected>
    void check_equal(const Actual& actual, const Expected& expected, const char* expression, const char* file, int line) {
        ++checks_;
        if (actual == expected) {
            return;
        }
        ++failures_;
        std::printf("    [FAIL] %s (%s:%d)\n", expression, file, line);
        print_value("      actual  : ", actual);
        print_value("      expected: ", expected);
    }

    int run() {
        std::printf("running %zu test case(s)\n", cases_.size());
        for (const Case& test_case : cases_) {
            const int failures_before = failures_;
            std::printf("[ RUN  ] %s\n", test_case.name.c_str());
            test_case.body();
            if (failures_ == failures_before) {
                std::printf("[ PASS ] %s\n", test_case.name.c_str());
            } else {
                std::printf("[ FAIL ] %s\n", test_case.name.c_str());
            }
        }
        std::printf("\n%d check(s), %d failure(s)\n", checks_, failures_);
        return failures_ == 0 ? 0 : 1;
    }

private:
    struct Case {
        std::string name;
        std::function<void()> body;
    };

    template <typename T>
    static void print_value(const char* label, const T& value) {
        if constexpr (std::is_same_v<T, std::string>) {
            std::printf("%s%s\n", label, value.c_str());
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            std::printf("%s%.*s\n", label, static_cast<int>(value.size()), value.data());
        } else if constexpr (std::is_enum_v<T>) {
            std::printf("%s%lld\n", label, static_cast<long long>(static_cast<std::underlying_type_t<T>>(value)));
        } else if constexpr (std::is_integral_v<T>) {
            std::printf("%s%lld\n", label, static_cast<long long>(value));
        } else {
            std::printf("%s<unprintable>\n", label);
        }
    }

    std::vector<Case> cases_;
    int checks_ = 0;
    int failures_ = 0;
};

struct CaseRegistrar {
    CaseRegistrar(std::string name, std::function<void()> body) {
        Runner::instance().add_case(std::move(name), std::move(body));
    }
};

}  // namespace testh

#define TEST_CASE(name)                                                                     \
    static void name();                                                                     \
    [[maybe_unused]] static const testh::CaseRegistrar registrar_##name(#name, name);      \
    static void name()

#define CHECK(expression) \
    testh::Runner::instance().check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define CHECK_EQ(actual, expected) \
    testh::Runner::instance().check_equal((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)