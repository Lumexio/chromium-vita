#pragma once

#include <string>

namespace platform::vita {

class Ime {
public:
    bool prompt_url(const std::string& title, const std::string& initial_text, std::string& out_text) const;
};

} // namespace platform::vita
