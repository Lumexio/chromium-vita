#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "http_client.h"

namespace browser {

enum class LoadState {
    Idle,
    Loading,
    Ready,
    Error,
};

class Session {
public:
    Session();

    void init();
    void shutdown();

    bool navigate(const std::string& url);
    bool reload();
    bool go_back();
    bool go_forward();
    void go_home();

    void add_bookmark();

    const std::string& current_url() const;
    const std::string& display_title() const;
    const std::string& status_message() const;
    LoadState load_state() const;
    const std::vector<std::string>& page_lines() const;
    const std::vector<std::string>& bookmarks() const;

    bool can_go_back() const;
    bool can_go_forward() const;

private:
    static constexpr std::size_t MAX_HISTORY = 64;
    static constexpr std::size_t MAX_BOOKMARKS = 64;
    static constexpr std::size_t MAX_PAGE_BYTES = 256 * 1024;

    bool fetch_into_current(const std::string& url);
    bool push_history_and_load(const std::string& url);

    void load_storage();
    void save_storage() const;

    std::string storage_dir() const;
    std::string history_file() const;
    std::string bookmarks_file() const;

    std::vector<std::string> parse_lines(const std::string& html) const;
    std::string extract_title(const std::string& html) const;
    std::string normalize_url(const std::string& url) const;

    HttpClient m_http;

    std::vector<std::string> m_history;
    std::vector<std::string> m_bookmarks;
    std::size_t              m_history_index{0};

    std::string m_current_url{"https://example.com"};
    std::string m_title{"Chromium Vita"};
    std::string m_status{"Ready"};
    LoadState   m_state{LoadState::Idle};

    std::vector<std::string> m_page_lines;
};

} // namespace browser
