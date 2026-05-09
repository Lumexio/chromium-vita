#pragma once

#include <vita2d.h>
#include "platform/vita/input.h"

namespace ui {

// Top-level UI shell rendered during Milestone 0.
// Draws the browser chrome (toolbar, URL bar, status bar) and a software
// cursor driven by D-pad / left-stick / touchscreen.
class Shell {
public:
    void init();
    void shutdown();

    void handle_input(const platform::vita::Input& input);
    void render() const;

private:
    vita2d_pgf* m_font{nullptr};

    float m_cursor_x{480.0f};
    float m_cursor_y{272.0f};
};

} // namespace ui
