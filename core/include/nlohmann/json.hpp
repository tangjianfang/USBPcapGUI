#pragma once

#include <string>
#include <map>
#include <vector>
#include <variant>
#include <stdexcept>
#include <sstream>

namespace nlohmann {

    class json {
    public:
        using value_type = std::variant<
            std::nullptr_t,
            bool,
            int,
            double,
            std::string,
            std::vector<json>,
            std::map<std::string, json>
        >;

        json() : value_(nullptr) {}
        json(std::nullptr_t) : value_(nullptr) {}
        json(bool v) : value_(v) {}
        json(int v) : value_(v) {}
        json(unsigned int v) : value_(static_cast<int>(v)) {}
        json(long long v) : value_(static_cast<int>(v)) {}
        json(unsigned long long v) : value_(static_cast<double>(v)) {}
        json(double v) : value_(v) {}
        json(const char* v) : value_(std::string(v)) {}
        json(const std::string& v) : value_(v) {}
        json(const std::vector<json>& v) : value_(v) {}
        json(const std::map<std::string, json>& v) : value_(v) {}
        
        json(std::initializer_list<json> init) {
            value_ = std::vector<json>(init.begin(), init.end());
        }

        template<typename T>
        json(std::initializer_list<T> init) {
            value_ = std::vector<json>();
            for (const auto& item : init) {
                std::get<std::vector<json>>(value_).push_back(json(item));
            }
        }

        template<typename K, typename V>
        json(std::initializer_list<std::pair<K, V>> init) {
            value_ = std::map<std::string, json>();
            for (const auto& item : init) {
                std::ostringstream oss;
                oss << item.first;
                std::get<std::map<std::string, json>>(value_)[oss.str()] = json(item.second);
            }
        }

        bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
        bool is_boolean() const { return std::holds_alternative<bool>(value_); }
        bool is_number() const { return std::holds_alternative<int>(value_) || std::holds_alternative<double>(value_); }
        bool is_string() const { return std::holds_alternative<std::string>(value_); }
        bool is_array() const { return std::holds_alternative<std::vector<json>>(value_); }
        bool is_object() const { return std::holds_alternative<std::map<std::string, json>>(value_); }

        bool is_string_integer() const {
            if (!is_string()) return false;
            try {
                std::stoi(std::get<std::string>(value_));
                return true;
            } catch (...) {
                return false;
            }
        }

        template<typename T>
        T get() const {
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return std::get<std::nullptr_t>(value_);
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::get<bool>(value_);
            } else if constexpr (std::is_same_v<T, int>) {
                if (std::holds_alternative<int>(value_)) {
                    return std::get<int>(value_);
                } else if (std::holds_alternative<double>(value_)) {
                    return static_cast<int>(std::get<double>(value_));
                }
                throw std::runtime_error("not an integer");
            } else if constexpr (std::is_same_v<T, double>) {
                if (std::holds_alternative<double>(value_)) {
                    return std::get<double>(value_);
                } else if (std::holds_alternative<int>(value_)) {
                    return static_cast<double>(std::get<int>(value_));
                }
                throw std::runtime_error("not a double");
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::get<std::string>(value_);
            } else if constexpr (std::is_same_v<T, std::vector<json>>) {
                return std::get<std::vector<json>>(value_);
            } else if constexpr (std::is_same_v<T, std::map<std::string, json>>) {
                return std::get<std::map<std::string, json>>(value_);
            }
            throw std::runtime_error("unsupported type");
        }

        json& operator[](const std::string& key) {
            if (!is_object()) {
                value_ = std::map<std::string, json>();
            }
            return std::get<std::map<std::string, json>>(value_)[key];
        }

        const json& operator[](const std::string& key) const {
            return std::get<std::map<std::string, json>>(value_).at(key);
        }

        json& operator[](size_t index) {
            return std::get<std::vector<json>>(value_)[index];
        }

        const json& operator[](size_t index) const {
            return std::get<std::vector<json>>(value_)[index];
        }

        size_t size() const {
            if (is_array()) {
                return std::get<std::vector<json>>(value_).size();
            } else if (is_object()) {
                return std::get<std::map<std::string, json>>(value_).size();
            }
            return 0;
        }

        bool contains(const std::string& key) const {
            if (!is_object()) return false;
            return std::get<std::map<std::string, json>>(value_).count(key) > 0;
        }

        static json array() {
            return json(std::vector<json>());
        }

        static json object() {
            return json(std::map<std::string, json>());
        }

        std::vector<json>& array() {
            if (!is_array()) {
                value_ = std::vector<json>();
            }
            return std::get<std::vector<json>>(value_);
        }

        const std::vector<json>& array() const {
            return std::get<std::vector<json>>(value_);
        }

        void push_back(const json& value) {
            array().push_back(value);
        }

        std::string dump(int indent = -1) const {
            std::ostringstream oss;
            dump_internal(oss, indent, 0);
            return oss.str();
        }

        static json parse(const std::string& str) {
            json result;
            parse_internal(str, 0, result);
            return result;
        }

    private:
        value_type value_;

        void dump_internal(std::ostringstream& oss, int indent, int level) const {
            if (is_null()) {
                oss << "null";
            } else if (is_boolean()) {
                oss << (get<bool>() ? "true" : "false");
            } else if (std::holds_alternative<int>(value_)) {
                oss << get<int>();
            } else if (std::holds_alternative<double>(value_)) {
                oss << get<double>();
            } else if (is_string()) {
                oss << "\"" << get<std::string>() << "\"";
            } else if (is_array()) {
                oss << "[";
                const auto& arr = get<std::vector<json>>();
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) oss << ",";
                    if (indent >= 0) {
                        oss << "\n" << std::string(level + 1, ' ');
                    }
                    arr[i].dump_internal(oss, indent, level + 1);
                }
                if (indent >= 0 && !arr.empty()) {
                    oss << "\n" << std::string(level, ' ');
                }
                oss << "]";
            } else if (is_object()) {
                oss << "{";
                const auto& obj = get<std::map<std::string, json>>();
                bool first = true;
                for (const auto& [key, val] : obj) {
                    if (!first) oss << ",";
                    first = false;
                    if (indent >= 0) {
                        oss << "\n" << std::string(level + 1, ' ');
                    }
                    oss << "\"" << key << "\":";
                    if (indent >= 0) oss << " ";
                    val.dump_internal(oss, indent, level + 1);
                }
                if (indent >= 0 && !obj.empty()) {
                    oss << "\n" << std::string(level, ' ');
                }
                oss << "}";
            }
        }

        static size_t parse_internal(const std::string& str, size_t pos, json& result) {
            while (pos < str.size() && std::isspace(str[pos])) pos++;
            if (pos >= str.size()) return pos;

            if (str[pos] == 'n') {
                result = nullptr;
                return pos + 4;
            } else if (str[pos] == 't') {
                result = true;
                return pos + 4;
            } else if (str[pos] == 'f') {
                result = false;
                return pos + 5;
            } else if (str[pos] == '"') {
                size_t end = str.find('"', pos + 1);
                if (end == std::string::npos) end = str.size();
                result = str.substr(pos + 1, end - pos - 1);
                return end + 1;
            } else if (str[pos] == '[') {
                std::vector<json> arr;
                pos++;
                while (pos < str.size() && str[pos] != ']') {
                    json elem;
                    pos = parse_internal(str, pos, elem);
                    arr.push_back(elem);
                    while (pos < str.size() && std::isspace(str[pos])) pos++;
                    if (str[pos] == ',') pos++;
                }
                result = arr;
                return pos + 1;
            } else if (str[pos] == '{') {
                std::map<std::string, json> obj;
                pos++;
                while (pos < str.size() && str[pos] != '}') {
                    json key;
                    pos = parse_internal(str, pos, key);
                    while (pos < str.size() && std::isspace(str[pos])) pos++;
                    if (str[pos] == ':') pos++;
                    json val;
                    pos = parse_internal(str, pos, val);
                    obj[key.get<std::string>()] = val;
                    while (pos < str.size() && std::isspace(str[pos])) pos++;
                    if (str[pos] == ',') pos++;
                }
                result = obj;
                return pos + 1;
            } else {
                size_t end = pos;
                while (end < str.size() && (std::isdigit(str[end]) || str[end] == '.' || str[end] == '-')) end++;
                std::string num = str.substr(pos, end - pos);
                if (num.find('.') != std::string::npos) {
                    result = std::stod(num);
                } else {
                    result = std::stoi(num);
                }
                return end;
            }
        }
    };

}