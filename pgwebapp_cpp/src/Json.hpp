#pragma once
/*
 * Json.hpp — Lightweight JSON builder (no external deps)
 * Supports: string, number, bool, null, object, array
 */

#include <string>
#include <sstream>
#include <vector>
#include <utility>

class Json {
public:
    /* ── Primitives ─────────────────────────────────────────── */
    static std::string str(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 2);
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        out += '"';
        return out;
    }

    static std::string null_val() { return "null"; }
    static std::string boolean(bool b) { return b ? "true" : "false"; }

    template<typename T>
    static std::string number(T n) {
        std::ostringstream ss;
        ss << n;
        return ss.str();
    }

    /* ── Object builder ─────────────────────────────────────── */
    class Object {
    public:
        Object& set(const std::string& key, const std::string& val_json) {
            pairs_.emplace_back(key, val_json);
            return *this;
        }
        Object& str(const std::string& key, const std::string& val) {
            return set(key, Json::str(val));
        }
        Object& num(const std::string& key, double val) {
            return set(key, Json::number(val));
        }
        Object& boolean(const std::string& key, bool val) {
            return set(key, Json::boolean(val));
        }
        Object& null_val(const std::string& key) {
            return set(key, Json::null_val());
        }

        std::string build() const {
            std::string out = "{";
            bool first = true;
            for (auto& [k, v] : pairs_) {
                if (!first) out += ',';
                out += Json::str(k) + ':' + v;
                first = false;
            }
            out += '}';
            return out;
        }

    private:
        std::vector<std::pair<std::string, std::string>> pairs_;
    };

    /* ── Array builder ──────────────────────────────────────── */
    class Array {
    public:
        Array& push(const std::string& val_json) {
            items_.push_back(val_json);
            return *this;
        }
        Array& push_str(const std::string& val) {
            return push(Json::str(val));
        }

        std::string build() const {
            std::string out = "[";
            bool first = true;
            for (auto& item : items_) {
                if (!first) out += ',';
                out += item;
                first = false;
            }
            out += ']';
            return out;
        }

    private:
        std::vector<std::string> items_;
    };
};
