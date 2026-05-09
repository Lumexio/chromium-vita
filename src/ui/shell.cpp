#include "shell.h"

#include <vita2d.h>

namespace ui {

// ── Colour palette ──────────────────────────────────────────────────────────
static constexpr unsigned int C_BG         = RGBA8(0x10, 0x10, 0x18, 0xFF);
static constexpr unsigned int C_TOOLBAR    = RGBA8(0x1E, 0x1E, 0x2E, 0xFF);
static constexpr unsigned int C_URLBAR     = RGBA8(0x2A, 0x2A, 0x3A, 0xFF);
static constexpr unsigned int C_URLBAR_BD  = RGBA8(0x55, 0x55, 0x88, 0xFF);
static constexpr unsigned int C_STATUS     = RGBA8(0x14, 0x14, 0x20, 0xFF);
static constexpr unsigned int C_TEXT       = RGBA8(0xE0, 0xE0, 0xFF, 0xFF);
static constexpr unsigned int C_TEXT_DIM   = RGBA8(0x80, 0x80, 0xA0, 0xFF);
static constexpr unsigned int C_CURSOR     = RGBA8(0xFF, 0xFF, 0xFF, 0xC0);
static constexpr unsigned int C_NAV_BTN    = RGBA8(0x30, 0x30, 0x48, 0xFF);

// ── Layout constants (pixels) ───────────────────────────────────────────────
static constexpr int   SCR_W        = 960;
static constexpr int   SCR_H        = 544;
static constexpr int   TOOLBAR_H    = 48;
static constexpr int   STATUS_H     = 24;
static constexpr int   URLBAR_X     = 120;
static constexpr int   URLBAR_Y     = 8;
static constexpr int   URLBAR_W     = 720;
static constexpr int   URLBAR_H     = 32;
static constexpr int   NAV_BTN_W    = 36;
static constexpr int   NAV_BTN_H    = 32;
static constexpr int   NAV_BTN_Y    = 8;
static constexpr int   NAV_BTN_GAP  = 4;
static constexpr float CURSOR_R     = 6.0f;
static constexpr float STICK_SPEED  = 6.0f;

// ── Shell ────────────────────────────────────────────────────────────────────

void Shell::init() {
    m_font     = vita2d_load_default_pgf();
    m_cursor_x = SCR_W / 2.0f;
    m_cursor_y = SCR_H / 2.0f;
}

void Shell::shutdown() {
    if (m_font) {
        vita2d_free_pgf(m_font);
        m_font = nullptr;
    }
}

void Shell::handle_input(const platform::vita::Input& input) {
    // Analogue stick movement
    float dx = input.left_stick_x();
    float dy = input.left_stick_y();

    // D-pad overrides stick
    if (input.held(platform::vita::Button::Left))  dx = -1.0f;
    if (input.held(platform::vita::Button::Right)) dx =  1.0f;
    if (input.held(platform::vita::Button::Up))    dy = -1.0f;
    if (input.held(platform::vita::Button::Down))  dy =  1.0f;

    m_cursor_x += dx * STICK_SPEED;
    m_cursor_y += dy * STICK_SPEED;

    // Clamp to screen bounds
    if (m_cursor_x < 0.0f)          m_cursor_x = 0.0f;
    if (m_cursor_x > SCR_W - 1.0f)  m_cursor_x = SCR_W - 1.0f;
    if (m_cursor_y < 0.0f)          m_cursor_y = 0.0f;
    if (m_cursor_y > SCR_H - 1.0f)  m_cursor_y = SCR_H - 1.0f;

    // Touch overrides cursor position (first touch point)
    if (input.touch_count() > 0) {
        const auto tp = input.touch_point(0);
        m_cursor_x = tp.x;
        m_cursor_y = tp.y;
    }
}

// Helper: draw a single-pixel border rectangle (unfilled).
static void draw_border(float x, float y, float w, float h, unsigned int col) {
    vita2d_draw_rectangle(x,         y,         w, 1.0f, col); // top
    vita2d_draw_rectangle(x,         y + h - 1, w, 1.0f, col); // bottom
    vita2d_draw_rectangle(x,         y,         1.0f, h,  col); // left
    vita2d_draw_rectangle(x + w - 1, y,         1.0f, h,  col); // right
}

// Helper: draw a small nav button box.
static void draw_nav_btn(int x, int y) {
    vita2d_draw_rectangle(x, y, NAV_BTN_W, NAV_BTN_H, C_NAV_BTN);
    draw_border(x, y, NAV_BTN_W, NAV_BTN_H, C_URLBAR_BD);
}

void Shell::render() const {
    // ── Background ──────────────────────────────────────────────────────────
    vita2d_draw_rectangle(0, 0, SCR_W, SCR_H, C_BG);

    // ── Toolbar ─────────────────────────────────────────────────────────────
    vita2d_draw_rectangle(0, 0, SCR_W, TOOLBAR_H, C_TOOLBAR);

    // Nav buttons: Back | Forward | Reload (left of URL bar)
    int btn_x = NAV_BTN_GAP;
    for (int i = 0; i < 3; ++i) {
        draw_nav_btn(btn_x, NAV_BTN_Y);
        btn_x += NAV_BTN_W + NAV_BTN_GAP;
    }

    // URL bar
    vita2d_draw_rectangle(URLBAR_X, URLBAR_Y, URLBAR_W, URLBAR_H, C_URLBAR);
    draw_border(URLBAR_X, URLBAR_Y, URLBAR_W, URLBAR_H, C_URLBAR_BD);

    // Placeholder URL text
    if (m_font) {
        vita2d_pgf_draw_text(m_font, URLBAR_X + 8, URLBAR_Y + 22,
                             C_TEXT_DIM, 0.75f, "about:blank");
    }

    // ── Status bar ──────────────────────────────────────────────────────────
    vita2d_draw_rectangle(0, SCR_H - STATUS_H, SCR_W, STATUS_H, C_STATUS);

    if (m_font) {
        vita2d_pgf_draw_text(m_font, 6, SCR_H - 6,
                             C_TEXT_DIM, 0.6f, "START to exit");
    }

    // ── Software cursor ─────────────────────────────────────────────────────
    vita2d_draw_fill_circle(m_cursor_x, m_cursor_y, CURSOR_R, C_CURSOR);
}

} // namespace ui
