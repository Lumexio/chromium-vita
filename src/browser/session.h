#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif

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
    struct SearchResult {
        std::string title;
        std::string url;
    };

    Session();

    void init();
    void shutdown();

    bool tick();

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
    const std::vector<SearchResult>& search_results() const;
    const std::string& search_query() const;
    bool showing_search_results() const;

    bool can_go_back() const;
    bool can_go_forward() const;

    int  search_engine_index() const;
    int  search_engine_count() const;
    const char* search_engine_name() const;
    void set_search_engine_index(int index);

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
    std::string search_engine_file() const;

    std::vector<std::string> parse_lines(const std::string& html) const;
    std::string extract_title(const std::string& html) const;
    std::string normalize_url(const std::string& url) const;
    bool is_search_url(const std::string& url) const;
    std::string extract_search_query(const std::string& url) const;
    std::vector<SearchResult> parse_search_results(const std::string& html) const;

    bool start_async_load(const std::string& url);
    void on_load_complete(const std::string& url, HttpResponse response);
#ifdef __vita__
    static int load_thread(SceSize argc, void* argv);
#endif

    HttpClient m_http;

    std::atomic<bool> m_worker_running{false};
    std::mutex m_worker_mutex;
    bool m_pending_ready{false};
    HttpResponse m_pending_response;
    std::string m_pending_url;
    std::string m_queued_url;
    bool m_has_queued_request{false};
#ifdef __vita__
    int m_worker_id{-1};
#endif

    std::vector<std::string> m_history;
    std::vector<std::string> m_bookmarks;
    std::size_t              m_history_index{0};

    std::string m_current_url{"https://example.com"};
    std::string m_title{"Chromium Vita"};
    std::string m_status{"Ready"};
    LoadState   m_state{LoadState::Idle};

    enum class ViewMode {
        Page,
        SearchResults,
    };
    ViewMode m_view{ViewMode::Page};
    std::vector<SearchResult> m_search_results;
    std::string m_search_query;

    int m_search_engine_index{0};

    std::vector<std::string> m_page_lines;
};

} // namespace browser
