#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <vita2d.h>

namespace ui {

namespace {
static constexpr unsigned int C_BG          = RGBA8(0x0F, 0x11, 0x16, 0xFF);
static constexpr unsigned int C_TOPBAR      = RGBA8(0x1C, 0x22, 0x2D, 0xFF);
static constexpr unsigned int C_BOTTOMBAR   = RGBA8(0x1A, 0x1F, 0x28, 0xFF);
static constexpr unsigned int C_VIEWPORT    = RGBA8(0x10, 0x12, 0x18, 0xFF);
static constexpr unsigned int C_BUTTON      = RGBA8(0x2A, 0x33, 0x44, 0xFF);
static constexpr unsigned int C_URLBAR      = RGBA8(0x1D, 0x25, 0x31, 0xFF);
static constexpr unsigned int C_FOCUS       = RGBA8(0xF1, 0x9F, 0x4F, 0xFF);
static constexpr unsigned int C_BORDER      = RGBA8(0x4F, 0x5D, 0x74, 0xFF);
static constexpr unsigned int C_TEXT        = RGBA8(0xE6, 0xEC, 0xFF, 0xFF);
static constexpr unsigned int C_TEXT_DIM    = RGBA8(0xA7, 0xB2, 0xC8, 0xFF);
static constexpr unsigned int C_MENU_BG     = RGBA8(0x00, 0x00, 0x00, 0xD0);
static constexpr unsigned int C_POINTER     = RGBA8(0xFF, 0xFF, 0xFF, 0xD0);

static constexpr int SCR_W = 960;
static constexpr int SCR_H = 544;
static constexpr int TOP_H = 64;
static constexpr int BOT_H = 38;
static constexpr int VIEW_Y = TOP_H;
static constexpr int VIEW_H = SCR_H - TOP_H - BOT_H;

static constexpr int BTN_WIDTH = 72;
static constexpr int BTN_HEIGHT = 40;
static constexpr int BTN_Y = 12;
static constexpr int BTN_GAP = 8;
static constexpr int URL_X = 4 + (BTN_WIDTH + BTN_GAP) * 4;
static constexpr int URL_Y = 10;
static constexpr int URL_W = SCR_W - URL_X - 8;
static constexpr int URL_H = 44;

static constexpr float CURSOR_R = 6.0f;
static constexpr float STICK_DEADZONE = 0.35f;
static constexpr float STICK_FOCUS_THRESHOLD = 0.85f;
static constexpr float STICK_SCROLL_SPEED = 2.5f;
static constexpr float STICK_CURSOR_SPEED = 4.0f;
static constexpr float TOUCH_SCROLL_FACTOR = 12.0f;
static constexpr int VIEW_PADDING_TOP = 20;
static constexpr int VIEW_LINE_HEIGHT = 18;
static constexpr std::size_t URL_DISPLAY_MAX_CHARS = 76;
static constexpr std::size_t TITLE_DISPLAY_MAX_CHARS = 46;
static constexpr std::size_t STATUS_DISPLAY_MAX_CHARS = 52;

struct Rect {
    int x;
    int y;
    int w;
    int h;

    bool contains(float px, float py) const {
        return px >= static_cast<float>(x) && px < static_cast<float>(x + w) &&
               py >= static_cast<float>(y) && py < static_cast<float>(y + h);
    }
};

void draw_border(const Rect& r, unsigned int color, float thickness = 2.0f) {
    vita2d_draw_rectangle(r.x, r.y, static_cast<float>(r.w), thickness, color);
    vita2d_draw_rectangle(r.x, r.y + r.h - thickness, static_cast<float>(r.w), thickness, color);
    vita2d_draw_rectangle(r.x, r.y, thickness, static_cast<float>(r.h), color);
    vita2d_draw_rectangle(r.x + r.w - thickness, r.y, thickness, static_cast<float>(r.h), color);
}

std::string fit_text(std::string text, std::size_t max_chars) {
    if (text.size() <= max_chars) return text;
    if (max_chars < 4) return text.substr(0, max_chars);
    text.resize(max_chars - 3);
    text += "...";
    return text;
}

Rect button_rect(int idx) {
    return Rect{4 + idx * (BTN_WIDTH + BTN_GAP), BTN_Y, BTN_WIDTH, BTN_HEIGHT};
}

Rect url_rect() {
    return Rect{URL_X, URL_Y, URL_W, URL_H};
}

Rect viewport_rect() {
    return Rect{0, VIEW_Y, SCR_W, VIEW_H};
}

} // namespace

void Shell::init() {
    m_font = vita2d_load_default_pgf();
    m_session.init();
    m_focus = Focus::Viewport;
    m_scroll_line = 0;
    m_menu_open = false;
    m_menu_status.clear();
}

void Shell::shutdown() {
    m_session.shutdown();
    if (m_font) {
        vita2d_free_pgf(m_font);
        m_font = nullptr;
    }
}

void Shell::move_focus(int direction) {
    int idx = focused_index();
    idx = (idx + direction + BUTTON_COUNT) % BUTTON_COUNT;
    m_focus = static_cast<Focus>(idx);
}

void Shell::open_url_ime() {
    std::string entered;
    if (m_ime.prompt_url("Open URL", m_session.current_url(), entered)) {
        m_session.navigate(entered);
        m_scroll_line = 0;
    }
}

void Shell::open_menu() {
    m_menu_open = !m_menu_open;
    if (!m_menu_open) return;

    m_session.add_bookmark();
    m_menu_status = "Menu: bookmarked current page";
}

void Shell::activate_focus() {
    switch (m_focus) {
        case Focus::Back:
            m_session.go_back();
            m_scroll_line = 0;
            break;
        case Focus::Forward:
            m_session.go_forward();
            m_scroll_line = 0;
            break;
        case Focus::Reload:
            m_session.reload();
            m_scroll_line = 0;
            break;
        case Focus::Home:
            m_session.go_home();
            m_scroll_line = 0;
            break;
        case Focus::Url:
            open_url_ime();
            break;
        case Focus::Viewport:
            break;
    }
}

void Shell::handle_touch(const platform::vita::Input& input) {
    if (input.touch_count() == 0) return;

    const auto point = input.touch_point(0);
    m_cursor_x = point.x;
    m_cursor_y = point.y;

    if (input.touch_pressed()) {
        for (int i = 0; i < 4; ++i) {
            if (button_rect(i).contains(point.x, point.y)) {
                m_focus = static_cast<Focus>(i);
                activate_focus();
                return;
            }
        }

        if (url_rect().contains(point.x, point.y)) {
            m_focus = Focus::Url;
            open_url_ime();
            return;
        }

        if (viewport_rect().contains(point.x, point.y)) {
            m_focus = Focus::Viewport;
        }
    }

    if (m_focus == Focus::Viewport && input.touch_count() > 0) {
        const auto prev = input.previous_touch_point(0);
        if (input.had_touch()) {
            const float dy = point.y - prev.y;
            m_scroll_line = std::max(0, m_scroll_line - static_cast<int>(dy / TOUCH_SCROLL_FACTOR));
        }
    }
}

void Shell::handle_input(const platform::vita::Input& input) {
    if (input.pressed(platform::vita::Button::Select)) {
        m_should_exit = true;
    }

    if (input.pressed(platform::vita::Button::Start)) {
        open_menu();
    }

    if (input.pressed(platform::vita::Button::Circle)) {
        m_session.go_back();
        m_focus = Focus::Viewport;
        m_scroll_line = 0;
    }

    if (input.pressed(platform::vita::Button::Cross)) {
        activate_focus();
    }

    if (input.pressed(platform::vita::Button::LTrigger)) {
        m_scroll_line = std::max(0, m_scroll_line - 10);
    }
    if (input.pressed(platform::vita::Button::RTrigger)) {
        m_scroll_line += 10;
    }

    const float lx = input.left_stick_x();
    const float ly = input.left_stick_y();

    if (input.pressed(platform::vita::Button::Left) || lx < -STICK_FOCUS_THRESHOLD) {
        move_focus(-1);
    }
    if (input.pressed(platform::vita::Button::Right) || lx > STICK_FOCUS_THRESHOLD) {
        move_focus(+1);
    }

    if (m_focus == Focus::Viewport) {
        if (input.held(platform::vita::Button::Up)) {
            m_scroll_line = std::max(0, m_scroll_line - 1);
        }
        if (input.held(platform::vita::Button::Down)) {
            m_scroll_line += 1;
        }
        if (std::fabs(ly) > STICK_DEADZONE) {
            m_scroll_line = std::max(0, m_scroll_line + static_cast<int>(ly * STICK_SCROLL_SPEED));
        }
    } else {
        if (input.pressed(platform::vita::Button::Up)) move_focus(-1);
        if (input.pressed(platform::vita::Button::Down)) move_focus(+1);
    }

    if (std::fabs(lx) > STICK_DEADZONE || std::fabs(ly) > STICK_DEADZONE) {
        m_cursor_x = std::clamp(m_cursor_x + (lx * STICK_CURSOR_SPEED), 0.0f, static_cast<float>(SCR_W - 1));
        m_cursor_y = std::clamp(m_cursor_y + (ly * STICK_CURSOR_SPEED), 0.0f, static_cast<float>(SCR_H - 1));
    }

    handle_touch(input);

    const int visible_lines = (VIEW_H - VIEW_PADDING_TOP) / VIEW_LINE_HEIGHT;
    const int max_scroll = std::max(0, static_cast<int>(m_session.page_lines().size()) - visible_lines);
    m_scroll_line = std::clamp(m_scroll_line, 0, max_scroll);
}

int Shell::focused_index() const {
    return static_cast<int>(m_focus);
}

bool Shell::should_exit() const {
    return m_should_exit;
}

void Shell::render() const {
    vita2d_draw_rectangle(0, 0, SCR_W, SCR_H, C_BG);

    vita2d_draw_rectangle(0, 0, SCR_W, TOP_H, C_TOPBAR);

    static constexpr std::array<const char*, 4> button_labels = {"<", ">", "Reload", "Home"};
    for (int i = 0; i < 4; ++i) {
        const Rect r = button_rect(i);
        vita2d_draw_rectangle(r.x, r.y, r.w, r.h, C_BUTTON);
        draw_border(r, focused_index() == i ? C_FOCUS : C_BORDER);

        if (m_font) {
            vita2d_pgf_draw_text(m_font, r.x + 10, r.y + 26, C_TEXT, 0.85f, button_labels[i]);
        }
    }

    const Rect url = url_rect();
    vita2d_draw_rectangle(url.x, url.y, url.w, url.h, C_URLBAR);
    draw_border(url, focused_index() == static_cast<int>(Focus::Url) ? C_FOCUS : C_BORDER);

    if (m_font) {
        const std::string shown_url = fit_text(m_session.current_url(), URL_DISPLAY_MAX_CHARS);
        vita2d_pgf_draw_text(m_font, url.x + 8, url.y + 28, C_TEXT, 0.8f, shown_url.c_str());
    }

    vita2d_draw_rectangle(0, VIEW_Y, SCR_W, VIEW_H, C_VIEWPORT);
    if (focused_index() == static_cast<int>(Focus::Viewport)) {
        draw_border(viewport_rect(), C_FOCUS, 3.0f);
    }

    if (m_font) {
        const auto& lines = m_session.page_lines();
        const int base_y = VIEW_Y + VIEW_PADDING_TOP;
        const int line_h = VIEW_LINE_HEIGHT;
        const int visible_lines = (VIEW_H - VIEW_PADDING_TOP) / line_h;

        for (int i = 0; i < visible_lines; ++i) {
            const int idx = m_scroll_line + i;
            if (idx >= static_cast<int>(lines.size())) break;
            vita2d_pgf_draw_text(m_font, 12, base_y + i * line_h, C_TEXT, 0.75f, lines[idx].c_str());
        }
    }

    vita2d_draw_rectangle(0, SCR_H - BOT_H, SCR_W, BOT_H, C_BOTTOMBAR);
    if (m_font) {
        const std::string title = fit_text(m_session.display_title(), TITLE_DISPLAY_MAX_CHARS);
        const std::string status = fit_text(m_menu_open ? m_menu_status : m_session.status_message(), STATUS_DISPLAY_MAX_CHARS);
        vita2d_pgf_draw_text(m_font, 8, SCR_H - 16, C_TEXT_DIM, 0.62f,
                             "DPad/LStick Focus+Scroll  X Select  O Back  L/R Pg  Start Menu  Select Exit");
        vita2d_pgf_draw_text(m_font, 8, SCR_H - BOT_H + 14, C_TEXT, 0.7f, title.c_str());
        vita2d_pgf_draw_text(m_font, SCR_W / 2, SCR_H - BOT_H + 14, C_TEXT_DIM, 0.7f, status.c_str());
    }

    if (m_menu_open) {
        const Rect menu{SCR_W / 2 - 170, SCR_H / 2 - 56, 340, 112};
        vita2d_draw_rectangle(menu.x, menu.y, menu.w, menu.h, C_MENU_BG);
        draw_border(menu, C_FOCUS);
        if (m_font) {
            vita2d_pgf_draw_text(m_font, menu.x + 18, menu.y + 36, C_TEXT, 0.9f, "Menu action: Bookmark added");
            vita2d_pgf_draw_text(m_font, menu.x + 18, menu.y + 68, C_TEXT_DIM, 0.7f, "Press START to close menu");
        }
    }

    vita2d_draw_fill_circle(m_cursor_x, m_cursor_y, CURSOR_R, C_POINTER);
}

} // namespace ui
