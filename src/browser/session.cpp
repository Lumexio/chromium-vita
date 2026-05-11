#include "session.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef __vita__
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace browser {
namespace {
constexpr const char* HOME_URL = "https://example.com";
struct LoadJob {
    Session* session;
    std::string url;
};
struct SearchEngine {
    const char* name;
    const char* query_url;
};

constexpr std::array<SearchEngine, 3> kSearchEngines = {
    SearchEngine{"DuckDuckGo", "http://api.duckduckgo.com/?format=json&no_redirect=1&no_html=1&q="},
    SearchEngine{"Google", "https://www.google.com/search?gbv=1&q="},
    SearchEngine{"Bing", "https://www.bing.com/search?q="},
};

int clamp_engine_index(int idx) {
    if (idx < 0) return 0;
    if (idx >= static_cast<int>(kSearchEngines.size())) return 0;
    return idx;
}

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

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string decode_query_component(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        char ch = input[i];
        if (ch == '+' ) {
            output.push_back(' ');
            continue;
        }
        if (ch == '%' && i + 2 < input.size()) {
            const int hi = hex_value(input[i + 1]);
            const int lo = hex_value(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                output.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        output.push_back(ch);
    }
    return output;
}

std::string strip_tags(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    bool in_tag = false;
    for (char ch : input) {
        if (ch == '<') {
            in_tag = true;
            continue;
        }
        if (ch == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag) output.push_back(ch);
    }
    return output;
}

std::string html_unescape(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '&') {
            out.push_back(input[i]);
            continue;
        }

        const std::size_t semi = input.find(';', i + 1);
        if (semi == std::string::npos) {
            out.push_back(input[i]);
            continue;
        }

        const std::string ent = input.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "#39") out.push_back('\'');
        else {
            out.append("&").append(ent).append(";");
        }
        i = semi;
    }

    return out;
}

bool json_string_at(const std::string& json, std::size_t quote_pos, std::string* out, std::size_t* end_pos) {
    if (quote_pos == std::string::npos || quote_pos >= json.size() || json[quote_pos] != '"') return false;
    out->clear();
    for (std::size_t i = quote_pos + 1; i < json.size(); ++i) {
        char ch = json[i];
        if (ch == '"') {
            *end_pos = i + 1;
            return true;
        }
        if (ch == '\\' && i + 1 < json.size()) {
            const char esc = json[++i];
            switch (esc) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    if (i + 4 >= json.size()) break;
                    int value = 0;
                    for (int j = 0; j < 4; ++j) {
                        const int hex = hex_value(json[i + 1 + j]);
                        if (hex < 0) { value = -1; break; }
                        value = (value << 4) | hex;
                    }
                    if (value >= 0) {
                        if (value <= 0x7F) out->push_back(static_cast<char>(value));
                        else out->push_back('?');
                    }
                    i += 4;
                    break;
                }
                default:
                    out->push_back(esc);
                    break;
            }
            continue;
        }
        out->push_back(ch);
    }
    return false;
}

bool json_find_string_value(const std::string& json, const std::string& key,
                            std::size_t* pos, std::string* out) {
    const std::string token = "\"" + key + "\"";
    const std::size_t key_pos = json.find(token, *pos);
    if (key_pos == std::string::npos) return false;
    const std::size_t colon = json.find(':', key_pos + token.size());
    if (colon == std::string::npos) return false;
    const std::size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return false;
    std::size_t end = 0;
    if (!json_string_at(json, quote, out, &end)) return false;
    *pos = end;
    return true;
}

std::vector<Session::SearchResult> parse_ddg_json(const std::string& json) {
    std::vector<Session::SearchResult> results;
    constexpr std::size_t kMaxResults = 20;
    std::size_t pos = 0;

    while (results.size() < kMaxResults) {
        const std::size_t text_pos = json.find("\"Text\"", pos);
        if (text_pos == std::string::npos) break;
        std::size_t scan = text_pos;
        std::string text;
        if (!json_find_string_value(json, "Text", &scan, &text)) {
            pos = text_pos + 6;
            continue;
        }

        const std::size_t url_pos = json.find("\"FirstURL\"", scan);
        if (url_pos == std::string::npos) {
            pos = scan;
            continue;
        }
        std::size_t scan_url = url_pos;
        std::string url;
        if (!json_find_string_value(json, "FirstURL", &scan_url, &url)) {
            pos = scan_url;
            continue;
        }

        text = trim(html_unescape(text));
        url = trim(url);
        if (!text.empty() && !url.empty()) {
            results.push_back({text, url});
        }
        pos = scan_url;
    }

    return results;
}

std::string extract_ddg_redirect(const std::string& href) {
    const std::string key = "uddg=";
    const std::size_t pos = href.find(key);
    if (pos == std::string::npos) return href;
    const std::size_t start = pos + key.size();
    const std::size_t end = href.find('&', start);
    return decode_query_component(href.substr(start, end == std::string::npos ? end : end - start));
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
#ifdef __vita__
    if (m_worker_id >= 0) {
        sceKernelWaitThreadEnd(m_worker_id, nullptr, nullptr);
        sceKernelDeleteThread(m_worker_id);
        m_worker_id = -1;
    }
#endif
    save_storage();
}

bool Session::tick() {
    HttpResponse response;
    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_worker_mutex);
        if (!m_pending_ready) return false;
        response = std::move(m_pending_response);
        url = std::move(m_pending_url);
        m_pending_ready = false;
    }

    if (url != m_current_url) {
        if (m_has_queued_request) {
            const std::string queued = m_queued_url;
            m_has_queued_request = false;
            fetch_into_current(queued);
        }
        return false;
    }

    if (response.ok) {
        m_state = LoadState::Ready;
        m_search_results.clear();
        m_search_query.clear();
        m_view = ViewMode::Page;

        if (is_search_url(m_current_url)) {
            m_search_query = extract_search_query(m_current_url);
            m_search_results = parse_search_results(response.body);
            if (!m_search_results.empty()) {
                m_view = ViewMode::SearchResults;
                m_title = m_search_query.empty() ? "Search results" : "Search: " + m_search_query;
                m_status = "Search results: " + std::to_string(m_search_results.size());
            } else {
                m_title = extract_title(response.body);
                m_page_lines = parse_lines(response.body);
                m_status = "Loaded: HTTP " + std::to_string(response.status_code);
            }
        } else {
            m_title = extract_title(response.body);
            m_page_lines = parse_lines(response.body);
            m_status = "Loaded: HTTP " + std::to_string(response.status_code);
        }
    } else {
        m_state = LoadState::Error;
        m_search_results.clear();
        m_search_query.clear();
        m_view = ViewMode::Page;
        m_title = response.cert_error ? "Certificate error" : "Load error";
        m_page_lines = {
            "Unable to load page.",
            "",
            response.error.empty() ? "Unknown network error" : response.error,
            "",
            response.cert_error ? "Check system date/cert trust and try another site."
                                : "Press Circle for back or Square to edit URL.",
        };
        m_status = "Error: " + (response.error.empty() ? std::string("network failure") : response.error);
    }

    if (m_has_queued_request) {
        const std::string queued = m_queued_url;
        m_has_queued_request = false;
        fetch_into_current(queued);
    }

#ifdef __vita__
    if (!m_worker_running.load() && m_worker_id >= 0) {
        sceKernelWaitThreadEnd(m_worker_id, nullptr, nullptr);
        sceKernelDeleteThread(m_worker_id);
        m_worker_id = -1;
    }
#endif

    return true;
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
const std::vector<Session::SearchResult>& Session::search_results() const { return m_search_results; }
const std::string& Session::search_query() const { return m_search_query; }
bool Session::showing_search_results() const { return m_view == ViewMode::SearchResults; }

bool Session::can_go_back() const {
    return !m_history.empty() && m_history_index > 0;
}

bool Session::can_go_forward() const {
    return !m_history.empty() && (m_history_index + 1) < m_history.size();
}

bool Session::fetch_into_current(const std::string& url) {
    m_state = LoadState::Loading;
    m_status = "Loading " + url;
    m_title = "Loading...";
    m_page_lines = {"Loading...", "", url};
    m_search_results.clear();
    m_search_query.clear();
    m_view = ViewMode::Page;
    return start_async_load(url);
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
    m_search_engine_index = 0;

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

    std::ifstream search_in(search_engine_file());
    if (std::getline(search_in, line)) {
        line = trim(line);
        if (!line.empty()) {
            try {
                m_search_engine_index = clamp_engine_index(std::stoi(line));
            } catch (...) {
                m_search_engine_index = 0;
            }
        }
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

    {
        std::ofstream search_out(search_engine_file(), std::ios::trunc);
        search_out << m_search_engine_index << '\n';
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

std::string Session::search_engine_file() const {
    return storage_dir() + "/search_engine.txt";
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
        const int idx = clamp_engine_index(m_search_engine_index);
        return std::string(kSearchEngines[idx].query_url) + encode_query_component(url);
    }
    if (!starts_with_http(url) && url.find('.') == std::string::npos) {
        const int idx = clamp_engine_index(m_search_engine_index);
        return std::string(kSearchEngines[idx].query_url) + encode_query_component(url);
    }
    if (starts_with_http(url)) return url;
    return std::string("https://") + url;
}

bool Session::is_search_url(const std::string& url) const {
    for (const auto& engine : kSearchEngines) {
        if (url.rfind(engine.query_url, 0) == 0) return true;
    }
    return false;
}

std::string Session::extract_search_query(const std::string& url) const {
    for (const auto& engine : kSearchEngines) {
        if (url.rfind(engine.query_url, 0) == 0) {
            const std::string raw = url.substr(std::strlen(engine.query_url));
            return decode_query_component(raw);
        }
    }

    const std::size_t qpos = url.find("?q=");
    if (qpos == std::string::npos) return {};
    const std::size_t start = qpos + 3;
    const std::size_t end = url.find('&', start);
    return decode_query_component(url.substr(start, end == std::string::npos ? end : end - start));
}

std::vector<Session::SearchResult> Session::parse_search_results(const std::string& html) const {
    const std::string trimmed = trim(html);
    if (!trimmed.empty() && trimmed.front() == '{') {
        return parse_ddg_json(trimmed);
    }
    std::vector<SearchResult> results;
    constexpr std::size_t kMaxResults = 20;
    std::size_t pos = 0;

    while (results.size() < kMaxResults) {
        pos = html.find("<a", pos);
        if (pos == std::string::npos) break;
        const std::size_t tag_end = html.find('>', pos);
        if (tag_end == std::string::npos) break;

        const std::string tag = html.substr(pos, tag_end - pos + 1);
        if (tag.find("result-link") == std::string::npos &&
            tag.find("result__a") == std::string::npos) {
            pos = tag_end + 1;
            continue;
        }

        const std::size_t href_pos = tag.find("href=\"");
        if (href_pos == std::string::npos) {
            pos = tag_end + 1;
            continue;
        }
        const std::size_t href_start = href_pos + 6;
        const std::size_t href_end = tag.find('"', href_start);
        if (href_end == std::string::npos) {
            pos = tag_end + 1;
            continue;
        }
        std::string href = tag.substr(href_start, href_end - href_start);
        if (!href.empty() && href[0] == '/') {
            href = std::string("https://duckduckgo.com") + href;
        }
        href = extract_ddg_redirect(href);

        const std::size_t close = html.find("</a>", tag_end + 1);
        if (close == std::string::npos) break;
        std::string text = html.substr(tag_end + 1, close - tag_end - 1);
        text = html_unescape(strip_tags(text));
        text = trim(text);
        if (!text.empty() && !href.empty()) {
            results.push_back({text, href});
        }

        pos = close + 4;
    }

    return results;
}

int Session::search_engine_index() const {
    return clamp_engine_index(m_search_engine_index);
}

int Session::search_engine_count() const {
    return static_cast<int>(kSearchEngines.size());
}

const char* Session::search_engine_name() const {
    return kSearchEngines[clamp_engine_index(m_search_engine_index)].name;
}

void Session::set_search_engine_index(int index) {
    const int clamped = clamp_engine_index(index);
    if (clamped == m_search_engine_index) return;
    m_search_engine_index = clamped;
    m_status = std::string("Search engine: ") + kSearchEngines[m_search_engine_index].name;
    save_storage();
}

bool Session::start_async_load(const std::string& url) {
    if (m_worker_running.load()) {
        m_queued_url = url;
        m_has_queued_request = true;
        m_status = "Queued: " + url;
        return true;
    }

#ifdef __vita__
    if (m_worker_id >= 0) {
        sceKernelWaitThreadEnd(m_worker_id, nullptr, nullptr);
        sceKernelDeleteThread(m_worker_id);
        m_worker_id = -1;
    }

    auto* job = new LoadJob{this, url};
    SceUID tid = sceKernelCreateThread("chromium-vita-net", &Session::load_thread,
                                       0x10000100, 0x10000, 0, 0, nullptr);
    if (tid < 0) {
        delete job;
        m_state = LoadState::Error;
        m_status = "Network thread create failed";
        return false;
    }

    m_worker_id = tid;
    m_worker_running.store(true);

    int start_res = sceKernelStartThread(tid, sizeof(LoadJob*), &job);
    if (start_res < 0) {
        delete job;
        m_worker_running.store(false);
        m_worker_id = -1;
        m_state = LoadState::Error;
        m_status = "Network thread start failed";
        return false;
    }

    return true;
#else
    HttpResponse resp = m_http.get(url, 8000, MAX_PAGE_BYTES);
    on_load_complete(url, std::move(resp));
    return true;
#endif
}

void Session::on_load_complete(const std::string& url, HttpResponse response) {
    {
        std::lock_guard<std::mutex> lock(m_worker_mutex);
        m_pending_url = url;
        m_pending_response = std::move(response);
        m_pending_ready = true;
    }
    m_worker_running.store(false);
}

#ifdef __vita__
int Session::load_thread(SceSize, void* argv) {
    auto* job = *static_cast<LoadJob**>(argv);
    HttpClient http;
    HttpResponse resp = http.get(job->url, 8000, MAX_PAGE_BYTES);
    job->session->on_load_complete(job->url, std::move(resp));
    delete job;
    return 0;
}
#endif

} // namespace browser
