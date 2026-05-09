#pragma once

namespace platform::vita {

// Thin display-context holder.  vita2d owns the actual framebuffer lifecycle;
// this class exists as a placeholder for future display-mode negotiation and
// to establish the canonical screen dimensions used throughout the codebase.
class Display {
public:
    static constexpr int WIDTH  = 960;
    static constexpr int HEIGHT = 544;

    Display()  = default;
    ~Display() = default;

    // Deleted to make move/copy intent explicit for future expansion.
    Display(const Display&)            = delete;
    Display& operator=(const Display&) = delete;
};

} // namespace platform::vita
