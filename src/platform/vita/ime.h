#pragma once

#include <array>
#include <string>

namespace platform::vita {

class Ime {
public:
    static constexpr std::size_t kMaxText = 255;

    bool prompt_url(const std::string& title, const std::string& initial_text, std::string& out_text);
    bool begin_url(const std::string& title, const std::string& initial_text);
    bool update_url(std::string& out_text, bool& accepted, bool& finished);
    void cancel();
    bool active() const;
    int last_init_result() const;
    int last_status() const;
    int idle_frames() const;
    bool seen_running() const;

private:
#ifdef __vita__
    bool m_active{false};
    bool m_cancel_requested{false};
    bool m_seen_running{false};
    int m_idle_frames{0};
    int m_last_init{0};
    int m_last_status{0};
    std::array<char16_t, kMaxText + 1> m_buffer{};
    std::u16string m_title16;
    std::u16string m_initial16;
#endif
};

} // namespace platform::vita
