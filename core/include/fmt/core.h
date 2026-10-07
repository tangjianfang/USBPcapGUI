#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <type_traits>

namespace fmt {

    template<typename T>
    struct formatter {
        static void format(std::ostringstream& oss, const T& value) {
            oss << value;
        }
    };

    template<>
    struct formatter<std::string> {
        static void format(std::ostringstream& oss, const std::string& value) {
            oss << value;
        }
    };

    template<>
    struct formatter<const char*> {
        static void format(std::ostringstream& oss, const char* value) {
            oss << value;
        }
    };

    template<>
    struct formatter<char*> {
        static void format(std::ostringstream& oss, char* value) {
            oss << value;
        }
    };

    template<>
    struct formatter<bool> {
        static void format(std::ostringstream& oss, bool value) {
            oss << (value ? "true" : "false");
        }
    };

    template<typename T>
    struct formatter<T*> {
        static void format(std::ostringstream& oss, T* value) {
            oss << value;
        }
    };

    template<typename... Args>
    std::string format(const char* fmt_str, Args&&... args) {
        std::ostringstream oss;
        const char* p = fmt_str;
        
        auto format_one = [&](auto&& arg) {
            formatter<std::decay_t<decltype(arg)>>::format(oss, arg);
        };

        size_t arg_index = 0;
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                if (arg_index < sizeof...(Args)) {
                    format_arg<Args...>(oss, arg_index, std::forward<Args>(args)...);
                    arg_index++;
                }
                p += 2;
            } else {
                oss << *p++;
            }
        }
        
        return oss.str();
    }

    template<typename... Args>
    void print(const char* fmt_str, Args&&... args) {
        std::cout << format(fmt_str, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void println(const char* fmt_str, Args&&... args) {
        std::cout << format(fmt_str, std::forward<Args>(args)...) << std::endl;
    }

private:
    template<typename T, typename... Args>
    static void format_arg(std::ostringstream& oss, size_t index, T&& first, Args&&... rest) {
        if (index == 0) {
            formatter<std::decay_t<T>>::format(oss, first);
        } else {
            format_arg<Args...>(oss, index - 1, std::forward<Args>(rest)...);
        }
    }

    template<typename T>
    static void format_arg(std::ostringstream& oss, size_t index, T&& first) {
        if (index == 0) {
            formatter<std::decay_t<T>>::format(oss, first);
        }
    }
}