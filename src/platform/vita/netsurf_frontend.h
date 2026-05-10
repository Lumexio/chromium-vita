#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vita2d.h>

#include "input.h"

struct ns_scaffold_context;
struct ns_scaffold_window;

namespace platform::vita {

class NetSurfFrontend {
public:
    bool init(int width, int height);
    void shutdown();

    void update_document(const std::string& url,
                         const std::string& title,
                         const std::vector<std::string>& lines);
    void handle_input(const Input& input, int viewport_top, bool viewport_focused);
    void render(float x, float y);

    const std::string& status_message() const;

private:
    void upload_surface();

    int m_width{0};
    int m_height{0};
    bool m_needs_upload{false};

    std::vector<uint32_t> m_surface;
    vita2d_texture*       m_texture{nullptr};

    ns_scaffold_context* m_ctx{nullptr};
    ns_scaffold_window*  m_window{nullptr};

    std::string m_status{"NetSurf not initialised"};
    std::string m_last_signature;
};

} // namespace platform::vita
