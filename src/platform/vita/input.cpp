#include "input.h"

namespace platform::vita {

// Vita front touchscreen reports coordinates in half-pixel units on a 960×544
// logical screen, so dividing by 2 maps them to screen coordinates.
static constexpr float TOUCH_SCALE = 0.5f;
static constexpr float AXIS_SCALE  = 1.0f / 128.0f;

Input::Input() {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
}

void Input::poll() {
    m_prev = m_curr;
    m_prev_touch = m_touch;
    sceCtrlReadBufferPositive(0, &m_curr, 1);
    sceTouchRead(SCE_TOUCH_PORT_FRONT, &m_touch, 1);
}

bool Input::held(Button btn) const {
    return (m_curr.buttons & static_cast<uint32_t>(btn)) != 0;
}

bool Input::pressed(Button btn) const {
    const uint32_t b = static_cast<uint32_t>(btn);
    return (m_curr.buttons & b) != 0 && (m_prev.buttons & b) == 0;
}

bool Input::released(Button btn) const {
    const uint32_t b = static_cast<uint32_t>(btn);
    return (m_curr.buttons & b) == 0 && (m_prev.buttons & b) != 0;
}

int Input::touch_count() const {
    return static_cast<int>(m_touch.reportNum);
}

TouchPoint Input::touch_point(int idx) const {
    if (idx < 0 || idx >= touch_count()) return {0.0f, 0.0f};
    return {
        static_cast<float>(m_touch.report[idx].x) * TOUCH_SCALE,
        static_cast<float>(m_touch.report[idx].y) * TOUCH_SCALE,
    };
}

TouchPoint Input::previous_touch_point(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_prev_touch.reportNum)) return {0.0f, 0.0f};
    return {
        static_cast<float>(m_prev_touch.report[idx].x) * TOUCH_SCALE,
        static_cast<float>(m_prev_touch.report[idx].y) * TOUCH_SCALE,
    };
}

bool Input::had_touch() const {
    return m_prev_touch.reportNum > 0;
}

bool Input::touch_pressed() const {
    return m_prev_touch.reportNum == 0 && m_touch.reportNum > 0;
}

bool Input::touch_released() const {
    return m_prev_touch.reportNum > 0 && m_touch.reportNum == 0;
}

float Input::left_stick_x()  const { return (static_cast<float>(m_curr.lx) - 128.0f) * AXIS_SCALE; }
float Input::left_stick_y()  const { return (static_cast<float>(m_curr.ly) - 128.0f) * AXIS_SCALE; }
float Input::right_stick_x() const { return (static_cast<float>(m_curr.rx) - 128.0f) * AXIS_SCALE; }
float Input::right_stick_y() const { return (static_cast<float>(m_curr.ry) - 128.0f) * AXIS_SCALE; }

} // namespace platform::vita
