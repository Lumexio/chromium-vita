#pragma once

#include <string>

#include <vita2d.h>

#include "browser/session.h"
#include "platform/vita/ime.h"
#include "platform/vita/input.h"

namespace ui {

class Shell {
public:
    void init();
    void shutdown();

    void handle_input(const platform::vita::Input& input);
    void render() const;
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
    void handle_touch(const platform::vita::Input& input);
    int  focused_index() const;

    Focus           m_focus{Focus::Viewport};
    vita2d_pgf* m_font{nullptr};
    browser::Session m_session;
    platform::vita::Ime m_ime;

    int  m_scroll_line{0};
    bool m_should_exit{false};
    bool m_menu_open{false};
    std::string m_menu_status;

    float m_cursor_x{480.0f};
    float m_cursor_y{272.0f};
};

} // namespace ui
