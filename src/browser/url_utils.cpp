#include "url_utils.h"

#include <cctype>
#include <string>

namespace browser {

namespace {
constexpr const char* DEFAULT_SEARCH_ENGINE = "https://duckduckgo.com/?q=";

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(begin, end - begin + 1);
}
} // namespace

bool starts_with_http(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string encode_query_component(const std::string& input) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(input.size() * 3);

    for (unsigned char ch : input) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            output.push_back(static_cast<char>(ch));
        } else if (ch == ' ') {
            output.push_back('+');
        } else {
            output.push_back('%');
            output.push_back(HEX[(ch >> 4) & 0x0F]);
            output.push_back(HEX[ch & 0x0F]);
        }
    }

    return output;
}

std::string normalize_url(const std::string& raw_url) {
    std::string url = trim(raw_url);
    if (url.empty()) return {};
    if (url.find(' ') != std::string::npos) {
        return std::string(DEFAULT_SEARCH_ENGINE) + encode_query_component(url);
    }
    if (starts_with_http(url)) return url;
    return std::string("https://") + url;
}

} // namespace browser
