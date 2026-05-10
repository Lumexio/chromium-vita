#pragma once

#include <cstddef>
#include <string>

namespace browser {

struct HttpResponse {
    bool        ok{false};
    int         status_code{0};
    bool        cert_error{false};
    std::string body;
    std::string error;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpResponse get(const std::string& url, int timeout_ms, std::size_t max_bytes) const;

private:
    bool m_ready{false};
};

} // namespace browser
