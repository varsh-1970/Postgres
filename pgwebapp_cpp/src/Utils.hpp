#pragma once
/*
 * Utils.hpp — URL decoding and form-body parsing
 */

#include <string>
#include <unordered_map>
#include <cctype>

namespace Utils {

/* URL-decode a percent-encoded string (+→space) */
inline std::string url_decode(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ) {
        if (src[i] == '%' && i + 2 < src.size()
            && std::isxdigit((unsigned char)src[i+1])
            && std::isxdigit((unsigned char)src[i+2])) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            out += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 3;
        } else if (src[i] == '+') {
            out += ' ';
            ++i;
        } else {
            out += src[i++];
        }
    }
    return out;
}

/* Parse application/x-www-form-urlencoded body */
inline std::unordered_map<std::string, std::string>
parse_form(const std::string& body) {
    std::unordered_map<std::string, std::string> fields;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t amp = body.find('&', pos);
        if (amp == std::string::npos) amp = body.size();
        auto pair = body.substr(pos, amp - pos);
        auto eq   = pair.find('=');
        if (eq != std::string::npos) {
            auto key = url_decode(pair.substr(0, eq));
            auto val = url_decode(pair.substr(eq + 1));
            fields[key] = val;
        }
        pos = amp + 1;
    }
    return fields;
}

/* Get a field with a default fallback */
inline std::string field(
    const std::unordered_map<std::string, std::string>& m,
    const std::string& key,
    const std::string& def = "")
{
    auto it = m.find(key);
    return (it != m.end() && !it->second.empty()) ? it->second : def;
}

} // namespace Utils
