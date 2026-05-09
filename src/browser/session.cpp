#include "session.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef __vita__
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#endif

namespace browser {
namespace {
constexpr const char* HOME_URL = "https://example.com";
constexpr const char* DEFAULT_SEARCH_ENGINE = "https://duckduckgo.com/?q=";

bool starts_with_http(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(begin, end - begin + 1);
}

void append_limited(std::vector<std::string>& out, const std::string& value, std::size_t limit) {
    if (value.empty()) return;
    out.erase(std::remove(out.begin(), out.end(), value), out.end());
    out.push_back(value);
    while (out.size() > limit) {
        out.erase(out.begin());
    }
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
} // namespace

Session::Session() = default;

void Session::init() {
    load_storage();
    if (m_history.empty()) {
        m_history.push_back(HOME_URL);
        m_history_index = 0;
    } else {
        m_history_index = m_history.size() - 1;
        m_current_url = m_history[m_history_index];
    }
    fetch_into_current(m_current_url);
}

void Session::shutdown() {
    save_storage();
}

bool Session::navigate(const std::string& url) {
    return push_history_and_load(normalize_url(url));
}

bool Session::reload() {
    return fetch_into_current(m_current_url);
}

bool Session::go_back() {
    if (!can_go_back()) return false;
    --m_history_index;
    m_current_url = m_history[m_history_index];
    const bool ok = fetch_into_current(m_current_url);
    save_storage();
    return ok;
}

bool Session::go_forward() {
    if (!can_go_forward()) return false;
    ++m_history_index;
    m_current_url = m_history[m_history_index];
    const bool ok = fetch_into_current(m_current_url);
    save_storage();
    return ok;
}

void Session::go_home() {
    push_history_and_load(HOME_URL);
}

void Session::add_bookmark() {
    append_limited(m_bookmarks, m_current_url, MAX_BOOKMARKS);
    m_status = "Bookmarked: " + m_current_url;
    save_storage();
}

const std::string& Session::current_url() const { return m_current_url; }
const std::string& Session::display_title() const { return m_title; }
const std::string& Session::status_message() const { return m_status; }
LoadState Session::load_state() const { return m_state; }
const std::vector<std::string>& Session::page_lines() const { return m_page_lines; }
const std::vector<std::string>& Session::bookmarks() const { return m_bookmarks; }

bool Session::can_go_back() const {
    return !m_history.empty() && m_history_index > 0;
}

bool Session::can_go_forward() const {
    return !m_history.empty() && (m_history_index + 1) < m_history.size();
}

bool Session::fetch_into_current(const std::string& url) {
    m_state = LoadState::Loading;
    m_status = "Loading " + url;

    HttpResponse resp = m_http.get(url, 8000, MAX_PAGE_BYTES);
    if (!resp.ok) {
        m_state = LoadState::Error;
        m_title = resp.cert_error ? "Certificate error" : "Load error";
        m_page_lines = {
            "Unable to load page.",
            "",
            resp.error.empty() ? "Unknown network error" : resp.error,
            "",
            resp.cert_error ? "Check system date/cert trust and try another site."
                            : "Press Circle for back or Select to edit URL.",
        };
        m_status = "Error: " + (resp.error.empty() ? std::string("network failure") : resp.error);
        return false;
    }

    m_state = LoadState::Ready;
    m_title = extract_title(resp.body);
    m_page_lines = parse_lines(resp.body);
    m_status = "Loaded: HTTP " + std::to_string(resp.status_code);
    return true;
}

bool Session::push_history_and_load(const std::string& url) {
    if (url.empty()) {
        m_state = LoadState::Error;
        m_status = "Invalid URL";
        return false;
    }

    if (!m_history.empty() && m_history_index + 1 < m_history.size()) {
        m_history.erase(
            m_history.begin() +
                static_cast<std::vector<std::string>::difference_type>(m_history_index + 1),
            m_history.end());
    }

    append_limited(m_history, url, MAX_HISTORY);
    m_history_index = m_history.empty() ? 0 : (m_history.size() - 1);
    m_current_url = url;

    const bool ok = fetch_into_current(url);
    save_storage();
    return ok;
}

void Session::load_storage() {
    m_history.clear();
    m_bookmarks.clear();

    std::ifstream history_in(history_file());
    std::string line;
    while (std::getline(history_in, line)) {
        line = trim(line);
        if (!line.empty()) append_limited(m_history, line, MAX_HISTORY);
    }

    std::ifstream bookmarks_in(bookmarks_file());
    while (std::getline(bookmarks_in, line)) {
        line = trim(line);
        if (!line.empty()) append_limited(m_bookmarks, line, MAX_BOOKMARKS);
    }
}

void Session::save_storage() const {
#ifdef __vita__
    sceIoMkdir(storage_dir().c_str(), 0777);
#else
    std::error_code ec;
    const std::string dir = storage_dir();
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "failed to create storage dir '%s': %s\n",
                     dir.c_str(), ec.message().c_str());
    }
#endif

    {
        std::ofstream history_out(history_file(), std::ios::trunc);
        for (const auto& url : m_history) {
            history_out << url << '\n';
        }
    }

    {
        std::ofstream bookmarks_out(bookmarks_file(), std::ios::trunc);
        for (const auto& url : m_bookmarks) {
            bookmarks_out << url << '\n';
        }
    }
}

std::string Session::storage_dir() const {
#ifdef __vita__
    return "ux0:data/chromium-vita";
#else
    return "/tmp/chromium-vita";
#endif
}

std::string Session::history_file() const {
    return storage_dir() + "/history.txt";
}

std::string Session::bookmarks_file() const {
    return storage_dir() + "/bookmarks.txt";
}

std::vector<std::string> Session::parse_lines(const std::string& html) const {
    std::vector<std::string> lines;
    std::string plain;
    plain.reserve(html.size());

    bool in_tag = false;
    for (char ch : html) {
        if (ch == '<') {
            in_tag = true;
            plain.push_back(' ');
            continue;
        }
        if (ch == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag) {
            plain.push_back((ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : ch);
        }
    }

    std::string compact;
    compact.reserve(plain.size());
    bool was_space = false;
    for (char ch : plain) {
        bool is_space = std::isspace(static_cast<unsigned char>(ch)) != 0;
        if (is_space) {
            if (!was_space) compact.push_back(' ');
        } else {
            compact.push_back(ch);
        }
        was_space = is_space;
    }

    compact = trim(compact);
    if (compact.empty()) {
        lines.push_back("(No readable content)");
        return lines;
    }

    constexpr std::size_t WRAP = 68;
    std::istringstream iss(compact);
    std::string word;
    std::string line;

    while (iss >> word) {
        if (line.empty()) {
            line = word;
            continue;
        }
        if (line.size() + 1 + word.size() > WRAP) {
            lines.push_back(line);
            line = word;
        } else {
            line.append(" ").append(word);
        }
    }
    if (!line.empty()) lines.push_back(line);

    if (lines.size() > 300) lines.resize(300);
    return lines;
}

std::string Session::extract_title(const std::string& html) const {
    auto lower = html;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    const auto title_open = lower.find("<title>");
    const auto title_close = lower.find("</title>");
    if (title_open == std::string::npos || title_close == std::string::npos || title_close <= title_open + 7) {
        return m_current_url;
    }

    std::string title = trim(html.substr(title_open + 7, title_close - (title_open + 7)));
    if (title.empty()) return m_current_url;
    if (title.size() > 80) title.resize(80);
    return title;
}

std::string Session::normalize_url(const std::string& raw_url) const {
    std::string url = trim(raw_url);
    if (url.empty()) return {};
    if (url.find(' ') != std::string::npos) {
        return std::string(DEFAULT_SEARCH_ENGINE) + encode_query_component(url);
    }
    if (starts_with_http(url)) return url;
    return std::string("https://") + url;
}

} // namespace browser
