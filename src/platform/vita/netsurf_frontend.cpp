#include "netsurf_frontend.h"

#include <algorithm>
#include <cstring>

#include <netsurf/scaffold.h>

namespace platform::vita {
namespace {
constexpr uint32_t kDefaultSurfaceColor = 0xFF101018u;
}

bool NetSurfFrontend::init(int width, int height) {
    m_width = width;
    m_height = height;
    m_surface.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height),
                     kDefaultSurfaceColor);

    if (ns_scaffold_init(&m_ctx) != 0 || !m_ctx) {
        m_status = "NetSurf core init failed";
        shutdown();
        return false;
    }

    m_window = ns_scaffold_create_window(m_ctx, m_width, m_height);
    if (!m_window) {
        m_status = "NetSurf window creation failed";
        shutdown();
        return false;
    }

    m_texture = vita2d_create_empty_texture(static_cast<unsigned int>(m_width),
                                            static_cast<unsigned int>(m_height));
    if (!m_texture) {
        m_status = "NetSurf texture allocation failed";
        shutdown();
        return false;
    }

    m_needs_upload = true;
    m_status = "NetSurf scaffold active";
    return true;
}

void NetSurfFrontend::shutdown() {
    if (m_texture) {
        vita2d_free_texture(m_texture);
        m_texture = nullptr;
    }
    if (m_window) {
        ns_scaffold_destroy_window(m_window);
        m_window = nullptr;
    }
    if (m_ctx) {
        ns_scaffold_shutdown(m_ctx);
        m_ctx = nullptr;
    }
    m_surface.clear();
}

void NetSurfFrontend::update_document(const std::string&                url,
                                      const std::string&                title,
                                      const std::vector<std::string>& lines) {
    if (!m_window) return;

    std::string body;
    constexpr std::size_t kMaxBodyBytes = NS_SCAFFOLD_BODY_CAPACITY - 1;
    for (const auto& line : lines) {
        const std::size_t needed = line.size() + (body.empty() ? 0u : 1u);
        if (body.size() + needed > kMaxBodyBytes) break;
        if (!body.empty()) body.push_back('\n');
        body.append(line);
    }

    const std::string signature = url + "\n" + title + "\n" + body;
    if (signature == m_last_signature) return;
    m_last_signature = signature;

    ns_scaffold_set_document(m_window, url.c_str(), title.c_str(), body.c_str());
    m_status = "NetSurf window: " + title;
    m_needs_upload = true;
}

void NetSurfFrontend::handle_input(const Input& input, int viewport_top, bool viewport_focused) {
    if (!m_window) return;
    bool changed = false;

    if (viewport_focused) {
        if (input.pressed(Button::Up)) {
            ns_scaffold_send_key(m_window, NS_SCAFFOLD_KEY_UP, 1);
            changed = true;
        }
        if (input.pressed(Button::Down)) {
            ns_scaffold_send_key(m_window, NS_SCAFFOLD_KEY_DOWN, 1);
            changed = true;
        }
        if (input.pressed(Button::Left)) {
            ns_scaffold_send_key(m_window, NS_SCAFFOLD_KEY_LEFT, 1);
            changed = true;
        }
        if (input.pressed(Button::Right)) {
            ns_scaffold_send_key(m_window, NS_SCAFFOLD_KEY_RIGHT, 1);
            changed = true;
        }
        if (input.pressed(Button::Cross)) {
            ns_scaffold_send_key(m_window, NS_SCAFFOLD_KEY_ACTIVATE, 1);
            changed = true;
        }
    }

    if (input.touch_count() > 0) {
        const auto p = input.touch_point(0);
        const int touch_x = static_cast<int>(p.x);
        const int touch_y = std::max(0, static_cast<int>(p.y) - viewport_top);
        ns_scaffold_send_pointer(m_window, touch_x, touch_y, input.touch_pressed() ? 1 : 0);
        changed = true;
    } else if (input.touch_released()) {
        ns_scaffold_send_pointer(m_window, 0, 0, 0);
        changed = true;
    }

    if (changed) m_needs_upload = true;
}

void NetSurfFrontend::upload_surface() {
    if (!m_window || !m_texture || !m_needs_upload || m_surface.empty()) return;

    ns_scaffold_render_rgba(m_window, m_surface.data(), m_width, m_height, m_width);

    auto* texture_data = static_cast<unsigned char*>(vita2d_texture_get_datap(m_texture));
    const unsigned int texture_stride = vita2d_texture_get_stride(m_texture);
    if (!texture_data || texture_stride == 0) return;

    for (int y = 0; y < m_height; ++y) {
        const auto* src = reinterpret_cast<const unsigned char*>(m_surface.data() +
                                                                  static_cast<std::size_t>(y * m_width));
        std::memcpy(texture_data + static_cast<std::size_t>(y) * texture_stride, src,
                    static_cast<std::size_t>(m_width * sizeof(uint32_t)));
    }

    m_needs_upload = false;
}

void NetSurfFrontend::render(float x, float y) {
    upload_surface();
    if (m_texture) {
        vita2d_draw_texture(m_texture, x, y);
    }
}

const std::string& NetSurfFrontend::status_message() const {
    return m_status;
}

} // namespace platform::vita
