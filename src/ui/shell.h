#pragma once

#include <string>

#include <vita2d.h>

#include "browser/session.h"
#include "platform/vita/ime.h"
#include "platform/vita/input.h"
#include "platform/vita/netsurf_frontend.h"

namespace ui {

class Shell {
public:
    void init();
    void shutdown();

    void handle_input(const platform::vita::Input& input);
    void render();
    void post_render();
    bool should_exit() const;

private:
    enum class Focus {
        Back,
        Forward,
        Reload,
        Home,
        Url,
        Viewport,
    };

    static constexpr int BUTTON_COUNT = 6;

    void move_focus(int direction);
    void activate_focus();
    void open_url_ime();
    void open_menu();
    void cycle_search_engine(int delta);
    void update_ime();
    void update_debug_overlay(const platform::vita::Input& input);
    void handle_touch(const platform::vita::Input& input);
    void sync_netsurf_document();
    int  focused_index() const;

    Focus           m_focus{Focus::Viewport};
    vita2d_pgf* m_font{nullptr};
    browser::Session m_session;
    platform::vita::Ime m_ime;
    platform::vita::NetSurfFrontend m_netsurf;

    int  m_scroll_line{0};
    int  m_result_index{0};
    bool m_should_exit{false};
    bool m_menu_open{false};
    bool m_netsurf_ready{false};
    std::string m_menu_status;
    int m_menu_engine_index{0};
    bool m_touch_down_on_url{false};
    bool m_pending_exit{false};
    std::string m_ime_text;
    int m_common_dialog_result{0};
    bool m_debug_enabled{true};
    std::string m_debug_input;
    std::string m_debug_sticks;
    std::string m_debug_url;
    std::string m_debug_status;

    float m_cursor_x{480.0f};
    float m_cursor_y{272.0f};
};

} // namespace ui
