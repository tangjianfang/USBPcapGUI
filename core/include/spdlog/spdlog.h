#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <mutex>

namespace spdlog {
    enum class level {
        trace = 0,
        debug = 1,
        info = 2,
        warn = 3,
        err = 4,
        critical = 5,
        off = 6
    };

    class logger {
    public:
        void set_level(level lvl) { level_ = lvl; }
        
        template<typename... Args>
        void info(const char* fmt, Args&&... args) {
            if (level_ <= level::info) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::cout << "[INFO] ";
                print_args(fmt, std::forward<Args>(args)...);
                std::cout << std::endl;
            }
        }
        
        template<typename... Args>
        void warn(const char* fmt, Args&&... args) {
            if (level_ <= level::warn) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::cout << "[WARN] ";
                print_args(fmt, std::forward<Args>(args)...);
                std::cout << std::endl;
            }
        }
        
        template<typename... Args>
        void error(const char* fmt, Args&&... args) {
            if (level_ <= level::err) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::cerr << "[ERROR] ";
                print_args(fmt, std::forward<Args>(args)...);
                std::cerr << std::endl;
            }
        }
        
        template<typename... Args>
        void debug(const char* fmt, Args&&... args) {
            if (level_ <= level::debug) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::cout << "[DEBUG] ";
                print_args(fmt, std::forward<Args>(args)...);
                std::cout << std::endl;
            }
        }
        
        template<typename... Args>
        void critical(const char* fmt, Args&&... args) {
            if (level_ <= level::critical) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::cerr << "[CRITICAL] ";
                print_args(fmt, std::forward<Args>(args)...);
                std::cerr << std::endl;
            }
        }
        
    private:
        level level_ = level::info;
        std::mutex mutex_;
        
        template<typename T>
        void print_args(const T& arg) {
            std::cout << arg;
        }
        
        template<typename T, typename... Args>
        void print_args(const T& first, Args&&... rest) {
            std::cout << first;
            print_args(std::forward<Args>(rest)...);
        }
    };

    inline void set_level(level lvl) {
        static logger instance;
        instance.set_level(lvl);
    }
    
    inline logger& get() {
        static logger instance;
        return instance;
    }

    template<typename... Args>
    inline void info(const char* fmt, Args&&... args) {
        get().info(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    inline void warn(const char* fmt, Args&&... args) {
        get().warn(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    inline void error(const char* fmt, Args&&... args) {
        get().error(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    inline void debug(const char* fmt, Args&&... args) {
        get().debug(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    inline void critical(const char* fmt, Args&&... args) {
        get().critical(fmt, std::forward<Args>(args)...);
    }
}