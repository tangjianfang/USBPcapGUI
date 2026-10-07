#pragma once

#include "core.h"

namespace fmt {

    template<typename T>
    std::string format(const char* fmt_str, const T& arg) {
        std::ostringstream oss;
        const char* p = fmt_str;
        
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                formatter<std::decay_t<T>>::format(oss, arg);
                p += 2;
            } else {
                oss << *p++;
            }
        }
        
        return oss.str();
    }

    template<typename T1, typename T2>
    std::string format(const char* fmt_str, const T1& arg1, const T2& arg2) {
        std::ostringstream oss;
        const char* p = fmt_str;
        
        size_t arg_index = 0;
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                if (arg_index == 0) {
                    formatter<std::decay_t<T1>>::format(oss, arg1);
                } else {
                    formatter<std::decay_t<T2>>::format(oss, arg2);
                }
                arg_index++;
                p += 2;
            } else {
                oss << *p++;
            }
        }
        
        return oss.str();
    }

    template<typename T1, typename T2, typename T3>
    std::string format(const char* fmt_str, const T1& arg1, const T2& arg2, const T3& arg3) {
        std::ostringstream oss;
        const char* p = fmt_str;
        
        size_t arg_index = 0;
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                if (arg_index == 0) {
                    formatter<std::decay_t<T1>>::format(oss, arg1);
                } else if (arg_index == 1) {
                    formatter<std::decay_t<T2>>::format(oss, arg2);
                } else {
                    formatter<std::decay_t<T3>>::format(oss, arg3);
                }
                arg_index++;
                p += 2;
            } else {
                oss << *p++;
            }
        }
        
        return oss.str();
    }

    template<typename T1, typename T2, typename T3, typename T4>
    std::string format(const char* fmt_str, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4) {
        std::ostringstream oss;
        const char* p = fmt_str;
        
        size_t arg_index = 0;
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                if (arg_index == 0) {
                    formatter<std::decay_t<T1>>::format(oss, arg1);
                } else if (arg_index == 1) {
                    formatter<std::decay_t<T2>>::format(oss, arg2);
                } else if (arg_index == 2) {
                    formatter<std::decay_t<T3>>::format(oss, arg3);
                } else {
                    formatter<std::decay_t<T4>>::format(oss, arg4);
                }
                arg_index++;
                p += 2;
            } else {
                oss << *p++;
            }
        }
        
        return oss.str();
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5>
    std::string format(const char* fmt_str, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5) {
        std::ostringstream oss;
        const char* p = fmt_str;
        
        size_t arg_index = 0;
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                if (arg_index == 0) {
                    formatter<std::decay_t<T1>>::format(oss, arg1);
                } else if (arg_index == 1) {
                    formatter<std::decay_t<T2>>::format(oss, arg2);
                } else if (arg_index == 2) {
                    formatter<std::decay_t<T3>>::format(oss, arg3);
                } else if (arg_index == 3) {
                    formatter<std::decay_t<T4>>::format(oss, arg4);
                } else {
                    formatter<std::decay_t<T5>>::format(oss, arg5);
                }
                arg_index++;
                p += 2;
            } else {
                oss << *p++;
            }
        }
        
        return oss.str();
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
    std::string format(const char* fmt_str, const T1& arg1, const T2& arg2, const T3& arg3, const T4& arg4, const T5& arg5, const T6& arg6) {
        std::ostringstream oss;
        const char* p = fmt_str;
        
        size_t arg_index = 0;
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                if (arg_index == 0) {
                    formatter<std::decay_t<T1>>::format(oss, arg1);
                } else if (arg_index == 1) {
                    formatter<std::decay_t<T2>>::format(oss, arg2);
                } else if (arg_index == 2) {
                    formatter<std::decay_t<T3>>::format(oss, arg3);
                } else if (arg_index == 3) {
                    formatter<std::decay_t<T4>>::format(oss, arg4);
                } else if (arg_index == 4) {
                    formatter<std::decay_t<T5>>::format(oss, arg5);
                } else {
                    formatter<std::decay_t<T6>>::format(oss, arg6);
                }
                arg_index++;
                p += 2;
            } else {
                oss << *p++;
            }
        }
        
        return oss.str();
    }

}