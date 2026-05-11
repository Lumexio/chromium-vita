#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

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
static constexpr std::size_t RESULTS_MAX_CHARS = 70;
static constexpr std::size_t TITLE_DISPLAY_MAX_CHARS = 46;
static constexpr std::size_t STATUS_DISPLAY_MAX_CHARS = 52;
static constexpr std::array<const char*, 4> BUTTON_LABELS = {"<", ">", "Reload", "Home"};

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

std::string fit_text(const std::string& text, std::size_t max_chars) {
    if (text.size() <= max_chars) return text;
    std::string clipped = text;
    if (max_chars < 4) return text.substr(0, max_chars);
    clipped.resize(max_chars - 3);
    clipped += "...";
    return clipped;
}

std::string ime_status_text(const platform::vita::Ime& ime, int common_update) {
    char buf[120];
    std::snprintf(buf, sizeof(buf), "IME init=%d status=%d idle=%d running=%d cd=%d",
                  ime.last_init_result(), ime.last_status(), ime.idle_frames(),
                  ime.seen_running() ? 1 : 0, common_update);
    return std::string(buf);
}

std::string join_events(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + " " + b;
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
    m_netsurf_ready = m_netsurf.init(SCR_W, VIEW_H);
    sync_netsurf_document();
    m_focus = Focus::Viewport;
    m_scroll_line = 0;
    m_result_index = 0;
    m_menu_open = false;
    m_menu_status.clear();
    m_menu_engine_index = m_session.search_engine_index();
    m_ime_text = m_session.current_url();
}

void Shell::shutdown() {
    m_netsurf.shutdown();
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
    if (m_ime.active()) return;
    m_ime_text = m_session.current_url();
    if (!m_ime.begin_url("Open URL", m_ime_text)) {
        return;
    }
}

void Shell::update_debug_overlay(const platform::vita::Input& input) {
    if (!m_debug_enabled) return;

    std::string events;
    if (input.pressed(platform::vita::Button::Cross)) events = join_events(events, "X");
    if (input.pressed(platform::vita::Button::Circle)) events = join_events(events, "O");
    if (input.pressed(platform::vita::Button::Square)) events = join_events(events, "[]");
    if (input.pressed(platform::vita::Button::Triangle)) events = join_events(events, "TRI");
    if (input.pressed(platform::vita::Button::Start)) events = join_events(events, "START");
    if (input.pressed(platform::vita::Button::Select)) events = join_events(events, "SELECT");
    if (input.pressed(platform::vita::Button::Up)) events = join_events(events, "UP");
    if (input.pressed(platform::vita::Button::Down)) events = join_events(events, "DOWN");
    if (input.pressed(platform::vita::Button::Left)) events = join_events(events, "LEFT");
    if (input.pressed(platform::vita::Button::Right)) events = join_events(events, "RIGHT");
    if (input.pressed(platform::vita::Button::LTrigger)) events = join_events(events, "L");
    if (input.pressed(platform::vita::Button::RTrigger)) events = join_events(events, "R");

    if (events.empty()) events = "none";
    m_debug_input = "Input: " + events;

    char stick_buf[120];
    std::snprintf(stick_buf, sizeof(stick_buf), "L(%.2f,%.2f) R(%.2f,%.2f) Touch:%d",
                  input.left_stick_x(), input.left_stick_y(),
                  input.right_stick_x(), input.right_stick_y(),
                  input.touch_count());
    m_debug_sticks = stick_buf;

    m_debug_url = "URL: " + fit_text(m_session.current_url(), 60);
    m_debug_status = "Status: " + fit_text(m_session.status_message(), 60);
}

void Shell::update_ime() {
    if (!m_ime.active()) return;

    bool accepted = false;
    bool finished = false;
    m_ime.update_url(m_ime_text, accepted, finished);

    if (!finished) return;

    if (accepted && !m_ime_text.empty()) {
        m_session.navigate(m_ime_text);
        m_scroll_line = 0;
        sync_netsurf_document();
    }

    if (m_pending_exit) {
        m_should_exit = true;
        m_pending_exit = false;
    }
}

void Shell::open_menu() {
    m_menu_open = !m_menu_open;
    if (!m_menu_open) return;
    m_menu_engine_index = m_session.search_engine_index();
    m_menu_status = std::string("Search engine: ") + m_session.search_engine_name();
}

void Shell::cycle_search_engine(int delta) {
    const int count = m_session.search_engine_count();
    if (count <= 0) return;
    m_menu_engine_index = (m_menu_engine_index + delta + count) % count;
    m_session.set_search_engine_index(m_menu_engine_index);
    m_menu_status = std::string("Search engine: ") + m_session.search_engine_name();
}

void Shell::activate_focus() {
    switch (m_focus) {
        case Focus::Back:
            m_session.go_back();
            m_scroll_line = 0;
            sync_netsurf_document();
            break;
        case Focus::Forward:
            m_session.go_forward();
            m_scroll_line = 0;
            sync_netsurf_document();
            break;
        case Focus::Reload:
            m_session.reload();
            m_scroll_line = 0;
            sync_netsurf_document();
            break;
        case Focus::Home:
            m_session.go_home();
            m_scroll_line = 0;
            sync_netsurf_document();
            break;
        case Focus::Url:
            open_url_ime();
            break;
        case Focus::Viewport:
            break;
    }
}

void Shell::handle_touch(const platform::vita::Input& input) {
    if (input.touch_count() == 0) {
        if (input.touch_released()) {
            const auto prev = input.previous_touch_point(0);
            if (m_touch_down_on_url && url_rect().contains(prev.x, prev.y)) {
                open_url_ime();
            }
            m_touch_down_on_url = false;
        }
        return;
    }

    const auto point = input.touch_point(0);
    m_cursor_x = point.x;
    m_cursor_y = point.y;

    if (input.touch_pressed()) {
        m_touch_down_on_url = url_rect().contains(point.x, point.y);
        for (int i = 0; i < 4; ++i) {
            if (button_rect(i).contains(point.x, point.y)) {
                m_focus = static_cast<Focus>(i);
                m_touch_down_on_url = false;
                activate_focus();
                return;
            }
        }

        if (m_touch_down_on_url) {
            m_focus = Focus::Url;
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
    update_debug_overlay(input);
    if (input.pressed(platform::vita::Button::Select)) {
        if (m_ime.active()) {
            m_pending_exit = true;
            m_ime.cancel();
        } else {
            m_should_exit = true;
        }
    }

    if (m_ime.active()) {
        if (input.pressed(platform::vita::Button::Circle)) {
            m_ime.cancel();
        }
            if (input.pressed(platform::vita::Button::Cross) ||
                input.pressed(platform::vita::Button::Square)) {
                m_ime.cancel();
            }
        update_ime();
        if (m_session.tick()) {
            sync_netsurf_document();
            if (m_session.showing_search_results()) {
                m_result_index = 0;
                m_scroll_line = 0;
            }
        }
        return;
    }

    if (m_menu_open) {
        if (input.pressed(platform::vita::Button::Start)) {
            open_menu();
        }
        if (input.pressed(platform::vita::Button::Left) || input.pressed(platform::vita::Button::LTrigger)) {
            cycle_search_engine(-1);
        }
        if (input.pressed(platform::vita::Button::Right) || input.pressed(platform::vita::Button::RTrigger)) {
            cycle_search_engine(+1);
        }
        if (input.pressed(platform::vita::Button::Triangle)) {
            m_session.add_bookmark();
            m_menu_status = "Menu: bookmarked current page";
        }
        if (input.pressed(platform::vita::Button::Cross)) {
            open_menu();
        }
        if (m_session.tick()) {
            sync_netsurf_document();
            if (m_session.showing_search_results()) {
                m_result_index = 0;
                m_scroll_line = 0;
            }
        }
        return;
    }

    if (input.pressed(platform::vita::Button::Start)) {
        open_menu();
    }

    if (input.pressed(platform::vita::Button::Circle)) {
        m_session.go_back();
        m_focus = Focus::Viewport;
        m_scroll_line = 0;
        sync_netsurf_document();
    }

    if (input.pressed(platform::vita::Button::Cross)) {
        if (m_focus == Focus::Viewport && m_session.showing_search_results()) {
            const auto& results = m_session.search_results();
            if (!results.empty()) {
                const int max_idx = static_cast<int>(results.size() - 1);
                const int idx = std::clamp(m_result_index, 0, max_idx);
                m_session.navigate(results[idx].url);
                m_scroll_line = 0;
                sync_netsurf_document();
            }
        } else {
            activate_focus();
        }
    }

    if (input.pressed(platform::vita::Button::Square)) {
        m_focus = Focus::Url;
        open_url_ime();
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
        if (m_session.showing_search_results()) {
            const int count = static_cast<int>(m_session.search_results().size());
            if (count > 0) {
                if (input.pressed(platform::vita::Button::Up)) {
                    m_result_index = std::max(0, m_result_index - 1);
                }
                if (input.pressed(platform::vita::Button::Down)) {
                    m_result_index = std::min(count - 1, m_result_index + 1);
                }

                const int visible_lines = (VIEW_H - VIEW_PADDING_TOP) / VIEW_LINE_HEIGHT;
                const int selected_line = 1 + m_result_index * 2;
                if (selected_line < m_scroll_line) {
                    m_scroll_line = selected_line;
                } else if (selected_line >= m_scroll_line + visible_lines) {
                    m_scroll_line = selected_line - visible_lines + 1;
                }
            }
        } else {
            if (input.held(platform::vita::Button::Up)) {
                m_scroll_line = std::max(0, m_scroll_line - 1);
            }
            if (input.held(platform::vita::Button::Down)) {
                m_scroll_line += 1;
            }
            if (std::fabs(ly) > STICK_DEADZONE) {
                m_scroll_line = std::max(0, m_scroll_line + static_cast<int>(ly * STICK_SCROLL_SPEED));
            }
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
    m_netsurf.handle_input(input, VIEW_Y, m_focus == Focus::Viewport);

    if (m_session.tick()) {
        sync_netsurf_document();
        if (m_session.showing_search_results()) {
            m_result_index = 0;
            m_scroll_line = 0;
        }
    }

    const int visible_lines = (VIEW_H - VIEW_PADDING_TOP) / VIEW_LINE_HEIGHT;
    int total_lines = static_cast<int>(m_session.page_lines().size());
    if (m_session.showing_search_results()) {
        const int result_lines = static_cast<int>(m_session.search_results().size()) * 2 + 1;
        total_lines = std::max(1, result_lines);
    }
    const int max_scroll = std::max(0, total_lines - visible_lines);
    m_scroll_line = std::clamp(m_scroll_line, 0, max_scroll);
}

void Shell::sync_netsurf_document() {
    m_netsurf.update_document(m_session.current_url(), m_session.display_title(), m_session.page_lines());
}

int Shell::focused_index() const {
    return static_cast<int>(m_focus);
}

bool Shell::should_exit() const {
    return m_should_exit;
}

void Shell::render() {
    vita2d_draw_rectangle(0, 0, SCR_W, SCR_H, C_BG);

    vita2d_draw_rectangle(0, 0, SCR_W, TOP_H, C_TOPBAR);

    for (int i = 0; i < 4; ++i) {
        const Rect r = button_rect(i);
        vita2d_draw_rectangle(r.x, r.y, r.w, r.h, C_BUTTON);
        draw_border(r, focused_index() == i ? C_FOCUS : C_BORDER);

        if (m_font) {
            vita2d_pgf_draw_text(m_font, r.x + 10, r.y + 26, C_TEXT, 0.85f, BUTTON_LABELS[i]);
        }
    }

    const Rect url = url_rect();
    vita2d_draw_rectangle(url.x, url.y, url.w, url.h, C_URLBAR);
    draw_border(url, focused_index() == static_cast<int>(Focus::Url) ? C_FOCUS : C_BORDER);

    if (m_font) {
        const std::string shown_url = fit_text(m_ime.active() ? m_ime_text : m_session.current_url(),
                                               URL_DISPLAY_MAX_CHARS);
        vita2d_pgf_draw_text(m_font, url.x + 8, url.y + 28, C_TEXT, 0.8f, shown_url.c_str());
    }

    vita2d_draw_rectangle(0, VIEW_Y, SCR_W, VIEW_H, C_VIEWPORT);
    if (focused_index() == static_cast<int>(Focus::Viewport)) {
        draw_border(viewport_rect(), C_FOCUS, 3.0f);
    }
    if (m_session.showing_search_results()) {
        if (m_font) {
            const auto& results = m_session.search_results();
            const std::string header = m_session.search_query().empty()
                                           ? "Search results"
                                           : "Search: " + m_session.search_query();
            const int base_y = VIEW_Y + VIEW_PADDING_TOP;
            const int line_h = VIEW_LINE_HEIGHT;
            const int visible_lines = (VIEW_H - VIEW_PADDING_TOP) / line_h;

            for (int i = 0; i < visible_lines; ++i) {
                const int line_idx = m_scroll_line + i;
                const int y = base_y + i * line_h;
                if (line_idx == 0) {
                    const std::string text = fit_text(header, RESULTS_MAX_CHARS);
                    vita2d_pgf_draw_text(m_font, 12, y, C_TEXT, 0.8f, text.c_str());
                    continue;
                }

                const int rel = line_idx - 1;
                const int res_idx = rel / 2;
                const bool url_line = (rel % 2) == 1;
                if (res_idx >= static_cast<int>(results.size())) break;

                if (url_line) {
                    const std::string text = fit_text(results[res_idx].url, RESULTS_MAX_CHARS);
                    vita2d_pgf_draw_text(m_font, 24, y, C_TEXT_DIM, 0.68f, text.c_str());
                } else {
                    const std::string title = std::to_string(res_idx + 1) + ". " + results[res_idx].title;
                    const std::string text = fit_text(title, RESULTS_MAX_CHARS);
                    const unsigned int color = (res_idx == m_result_index) ? C_FOCUS : C_TEXT;
                    vita2d_pgf_draw_text(m_font, 12, y, color, 0.75f, text.c_str());
                }
            }
        }
    } else if (m_netsurf_ready) {
        m_netsurf.render(0.0f, static_cast<float>(VIEW_Y));
    } else if (m_font) {
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
        std::string status;
        if (m_menu_open) {
            status = m_menu_status;
        } else if (m_netsurf_ready) {
            status = m_netsurf.status_message();
        } else {
            status = m_session.status_message();
        }
        status = fit_text(status, STATUS_DISPLAY_MAX_CHARS);
        vita2d_pgf_draw_text(m_font, 8, SCR_H - 16, C_TEXT_DIM, 0.62f,
                     "DPad/LStick Focus+Scroll  X Select  O Back  [] URL  L/R Pg  START Menu  SELECT Exit");
        vita2d_pgf_draw_text(m_font, 8, SCR_H - BOT_H + 14, C_TEXT, 0.7f, title.c_str());
        vita2d_pgf_draw_text(m_font, SCR_W / 2, SCR_H - BOT_H + 14, C_TEXT_DIM, 0.7f, status.c_str());
    }

    if (m_menu_open) {
        const Rect menu{SCR_W / 2 - 170, SCR_H / 2 - 56, 340, 112};
        vita2d_draw_rectangle(menu.x, menu.y, menu.w, menu.h, C_MENU_BG);
        draw_border(menu, C_FOCUS);
        if (m_font) {
            vita2d_pgf_draw_text(m_font, menu.x + 18, menu.y + 34, C_TEXT, 0.85f, m_menu_status.c_str());
            vita2d_pgf_draw_text(m_font, menu.x + 18, menu.y + 64, C_TEXT_DIM, 0.7f,
                                 "L/R: change engine  X: close  TRI: bookmark");
        }
    }

    if (m_ime.active()) {
        if (m_font) {
            const std::string info = fit_text(ime_status_text(m_ime, m_common_dialog_result),
                                               STATUS_DISPLAY_MAX_CHARS);
            vita2d_pgf_draw_text(m_font, 8, VIEW_Y + 18, C_TEXT_DIM, 0.6f, info.c_str());
        }
    }

    if (m_debug_enabled && m_font) {
        const int box_x = 8;
        const int box_y = 6;
        const int line_h = 14;
        const int lines = 5;
        const int box_w = SCR_W - 16;
        const int box_h = line_h * lines + 8;
        vita2d_draw_rectangle(box_x, box_y, box_w, box_h, RGBA8(0, 0, 0, 0x90));

        int y = box_y + 14;
        vita2d_pgf_draw_text(m_font, box_x + 8, y, C_TEXT_DIM, 0.6f,
                             fit_text(m_debug_input, 100).c_str());
        y += line_h;
        vita2d_pgf_draw_text(m_font, box_x + 8, y, C_TEXT_DIM, 0.6f,
                             fit_text(m_debug_sticks, 100).c_str());
        y += line_h;
        vita2d_pgf_draw_text(m_font, box_x + 8, y, C_TEXT_DIM, 0.6f,
                             fit_text(m_debug_url, 100).c_str());
        y += line_h;
        vita2d_pgf_draw_text(m_font, box_x + 8, y, C_TEXT_DIM, 0.6f,
                             fit_text(m_debug_status, 100).c_str());
        y += line_h;
        if (m_ime.active()) {
            const std::string info = fit_text(ime_status_text(m_ime, m_common_dialog_result), 100);
            vita2d_pgf_draw_text(m_font, box_x + 8, y, C_TEXT_DIM, 0.6f, info.c_str());
        } else {
            vita2d_pgf_draw_text(m_font, box_x + 8, y, C_TEXT_DIM, 0.6f, "IME: inactive");
        }
    }

    vita2d_draw_fill_circle(m_cursor_x, m_cursor_y, CURSOR_R, C_POINTER);
}

void Shell::post_render() {
    if (!m_ime.active()) return;
    m_common_dialog_result = vita2d_common_dialog_update();
    if (m_common_dialog_result < 0) {
        m_ime.cancel();
    }
}

} // namespace ui
