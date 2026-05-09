#pragma once

#include <cstdint>
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace platform::vita {

enum class Button : uint32_t {
    Select   = SCE_CTRL_SELECT,
    Start    = SCE_CTRL_START,
    Up       = SCE_CTRL_UP,
    Right    = SCE_CTRL_RIGHT,
    Down     = SCE_CTRL_DOWN,
    Left     = SCE_CTRL_LEFT,
    LTrigger = SCE_CTRL_LTRIGGER,
    RTrigger = SCE_CTRL_RTRIGGER,
    L1       = SCE_CTRL_L1,
    R1       = SCE_CTRL_R1,
    Triangle = SCE_CTRL_TRIANGLE,
    Circle   = SCE_CTRL_CIRCLE,
    Cross    = SCE_CTRL_CROSS,
    Square   = SCE_CTRL_SQUARE,
};

struct TouchPoint {
    float x, y;
};

// Thin abstraction over SceCtrl / SceTouch for the rest of the codebase.
class Input {
public:
    Input();

    // Call once per frame before querying state.
    void poll();

    // True while button is held this frame.
    bool held(Button btn) const;
    // True only on the frame the button was first pressed.
    bool pressed(Button btn) const;
    // True only on the frame the button was released.
    bool released(Button btn) const;

    // Front touchscreen.
    int        touch_count() const;
    TouchPoint touch_point(int idx) const;
    TouchPoint previous_touch_point(int idx) const;
    bool       had_touch() const;
    bool       touch_pressed() const;
    bool       touch_released() const;

    // Analogue sticks normalised to [-1, +1].
    float left_stick_x()  const;
    float left_stick_y()  const;
    float right_stick_x() const;
    float right_stick_y() const;

private:
    SceCtrlData  m_curr{};
    SceCtrlData  m_prev{};
    SceTouchData m_touch{};
    SceTouchData m_prev_touch{};
};

} // namespace platform::vita
