// =============================================================================
//  tests/test_framework.hpp  --  a ~70-line single-header test runner
//
//  Deliberately dependency-free so `cmake --build . && ctest` works on a bare
//  machine with no network. Register a test with TEST(name){...}; assert with
//  CHECK(cond) / CHECK_EQ(a,b). main() lives in test_main.cpp.
// =============================================================================
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tf {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failure_count() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline void report_failure(const char* file, int line, const std::string& msg) {
    std::printf("    FAIL %s:%d  %s\n", file, line, msg.c_str());
    ++failure_count();
}

inline int run_all() {
    int failed_cases = 0;
    for (auto& c : registry()) {
        const int before = failure_count();
        std::printf("[ RUN  ] %s\n", c.name.c_str());
        c.fn();
        if (failure_count() == before) {
            std::printf("[  OK  ] %s\n", c.name.c_str());
        } else {
            std::printf("[ FAIL ] %s\n", c.name.c_str());
            ++failed_cases;
        }
    }
    std::printf("\n%d test(s) failed, %zu total.\n", failed_cases, registry().size());
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace tf

#define TF_CONCAT_(a, b) a##b
#define TF_CONCAT(a, b) TF_CONCAT_(a, b)

#define TEST(name)                                                            \
    static void name();                                                       \
    static ::tf::Registrar TF_CONCAT(tf_reg_, __LINE__){#name, name};         \
    static void name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) ::tf::report_failure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        auto _va = (a);                                                       \
        auto _vb = (b);                                                       \
        if (!(_va == _vb))                                                    \
            ::tf::report_failure(__FILE__, __LINE__,                          \
                                 "CHECK_EQ(" #a ", " #b ")");                 \
    } while (0)
