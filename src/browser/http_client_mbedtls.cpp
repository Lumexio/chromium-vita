#include "http_client.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/io/fcntl.h>

#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#endif

namespace browser {

namespace {
#ifdef __vita__
constexpr int NET_POOL_SIZE = 4 * 1024 * 1024;
constexpr const char* CA_BUNDLE_PATH = "ux0:data/chromium-vita/ca-bundle.pem";
constexpr int MAX_REDIRECTS = 5;
constexpr std::size_t MAX_HEADER_BYTES = 32 * 1024;
constexpr std::size_t READ_CHUNK = 2048;

bool g_net_ready = false;
int g_ref_count = 0;

bool g_ca_ready = false;
std::string g_ca_error;
std::vector<unsigned char> g_ca_bundle;
mbedtls_x509_crt g_ca;

std::array<char, NET_POOL_SIZE> g_net_mem{};

std::string format_error(const char* what, int code) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s (0x%08X)", what, static_cast<unsigned int>(code));
    return std::string(buf);
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(begin, end - begin + 1);
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool starts_with(const std::string& s, const char* prefix) {
    const std::size_t len = std::strlen(prefix);
    return s.size() >= len && s.compare(0, len, prefix) == 0;
}

std::string normalize_url(const std::string& url) {
    if (url.find("://") != std::string::npos) return url;
    return std::string("https://") + url;
}

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string path;
    int port{0};
};

bool parse_url(const std::string& raw, ParsedUrl* out, std::string* error) {
    std::string url = normalize_url(raw);
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        *error = "Invalid URL";
        return false;
    }

    out->scheme = to_lower(url.substr(0, scheme_end));
    const std::size_t host_start = scheme_end + 3;
    const std::size_t path_start = url.find('/', host_start);
    const std::string host_port = (path_start == std::string::npos)
                                      ? url.substr(host_start)
                                      : url.substr(host_start, path_start - host_start);
    out->path = (path_start == std::string::npos) ? "/" : url.substr(path_start);

    if (host_port.empty()) {
        *error = "Invalid URL host";
        return false;
    }

    const std::size_t colon = host_port.find(':');
    if (colon == std::string::npos) {
        out->host = host_port;
    } else {
        out->host = host_port.substr(0, colon);
        const std::string port_str = host_port.substr(colon + 1);
        try {
            out->port = std::stoi(port_str);
        } catch (...) {
            *error = "Invalid URL port";
            return false;
        }
    }

    if (out->host.empty()) {
        *error = "Invalid URL host";
        return false;
    }

    if (out->port == 0) {
        out->port = (out->scheme == "https") ? 443 : 80;
    }

    if (out->path.empty()) {
        out->path = "/";
    }

    return true;
}

unsigned short to_net_port(unsigned short port) {
    return static_cast<unsigned short>((port << 8) | (port >> 8));
}

bool resolve_host(const std::string& host, SceNetInAddr* out_addr, int timeout_ms, std::string* error) {
    if (sceNetInetPton(SCE_NET_AF_INET, host.c_str(), out_addr) == 1) {
        return true;
    }

    SceNetResolverParam param{};
    const int resolver = sceNetResolverCreate("chromium-vita", &param, 0);
    if (resolver < 0) {
        *error = format_error("sceNetResolverCreate failed", resolver);
        return false;
    }

    const int timeout_us = timeout_ms * 1000;
    const int ret = sceNetResolverStartNtoa(resolver, host.c_str(), out_addr, timeout_us, 2, 0);
    sceNetResolverDestroy(resolver);
    if (ret < 0) {
        *error = format_error("DNS lookup failed", ret);
        return false;
    }

    return true;
}

int open_socket(const ParsedUrl& url, int timeout_ms, std::string* error) {
    SceNetInAddr addr{};
    if (!resolve_host(url.host, &addr, timeout_ms, error)) {
        return -1;
    }

    const int sock = sceNetSocket("chromium-vita", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
    if (sock < 0) {
        *error = format_error("sceNetSocket failed", sock);
        return -1;
    }

    SceNetSockaddrIn sa{};
    sa.sin_family = SCE_NET_AF_INET;
    sa.sin_port = to_net_port(static_cast<unsigned short>(url.port));
    sa.sin_addr = addr;

    const int ret = sceNetConnect(sock, reinterpret_cast<SceNetSockaddr*>(&sa), sizeof(sa));
    if (ret < 0) {
        *error = format_error("sceNetConnect failed", ret);
        sceNetSocketClose(sock);
        return -1;
    }

    return sock;
}

std::string tls_error_string(int ret) {
    char buf[160];
    mbedtls_strerror(ret, buf, sizeof(buf));
    return std::string(buf);
}

bool read_file(const char* path, std::vector<unsigned char>* out) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;

    const SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (size <= 0) {
        sceIoClose(fd);
        return false;
    }

    sceIoLseek(fd, 0, SCE_SEEK_SET);
    out->assign(static_cast<std::size_t>(size) + 1, 0);
    const int read_bytes = sceIoRead(fd, out->data(), static_cast<unsigned int>(size));
    sceIoClose(fd);

    if (read_bytes != static_cast<int>(size)) {
        out->clear();
        return false;
    }

    (*out)[static_cast<std::size_t>(size)] = 0;
    return true;
}

bool load_ca_bundle() {
    mbedtls_x509_crt_init(&g_ca);

    if (!read_file(CA_BUNDLE_PATH, &g_ca_bundle)) {
        g_ca_error = std::string("TLS CA bundle missing: ") + CA_BUNDLE_PATH;
        return false;
    }

    const int ret = mbedtls_x509_crt_parse(&g_ca, g_ca_bundle.data(), g_ca_bundle.size());
    if (ret < 0) {
        g_ca_error = std::string("TLS CA bundle parse failed: ") + tls_error_string(ret);
        return false;
    }

    g_ca_ready = true;
    return true;
}

void init_runtime() {
    if (g_ref_count++ > 0) return;

    SceNetInitParam net_param{};
    net_param.memory = g_net_mem.data();
    net_param.size = NET_POOL_SIZE;
    net_param.flags = 0;

    if (!g_net_ready && sceNetInit(&net_param) >= 0) {
        g_net_ready = true;
        sceNetCtlInit();
    }

    if (!g_ca_ready) {
        load_ca_bundle();
    }
}

void term_runtime() {
    if (--g_ref_count > 0) return;

    if (g_net_ready) {
        sceNetCtlTerm();
        sceNetTerm();
        g_net_ready = false;
    }

    mbedtls_x509_crt_free(&g_ca);
    g_ca_ready = false;
    g_ca_bundle.clear();
    g_ca_error.clear();
}

struct Stream {
    bool tls{false};
    int sock{-1};
    mbedtls_ssl_context* ssl{nullptr};
};

struct TlsSession {
    mbedtls_ssl_config conf{};
    mbedtls_ssl_context ssl{};
    bool ready{false};

    bool init(int sock, const std::string& host, std::string* error, bool* cert_error) {
        *cert_error = false;

        mbedtls_ssl_config_init(&conf);
        mbedtls_ssl_init(&ssl);

        const psa_status_t psa_status = psa_crypto_init();
        if (psa_status != PSA_SUCCESS) {
            *error = format_error("TLS RNG init failed", static_cast<int>(psa_status));
            return false;
        }

        int ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                              MBEDTLS_SSL_TRANSPORT_STREAM,
                                              MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret < 0) {
            *error = std::string("TLS config failed: ") + tls_error_string(ret);
            return false;
        }

        mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &g_ca, nullptr);

        ret = mbedtls_ssl_setup(&ssl, &conf);
        if (ret < 0) {
            *error = std::string("TLS setup failed: ") + tls_error_string(ret);
            return false;
        }

        ret = mbedtls_ssl_set_hostname(&ssl, host.c_str());
        if (ret < 0) {
            *error = std::string("TLS hostname failed: ") + tls_error_string(ret);
            return false;
        }

        mbedtls_ssl_set_bio(&ssl, &sock,
                            [](void* ctx, const unsigned char* buf, size_t len) -> int {
                                const int fd = *static_cast<int*>(ctx);
                                const int ret = sceNetSend(fd, buf, static_cast<unsigned int>(len), 0);
                                if (ret < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
                                return ret;
                            },
                            [](void* ctx, unsigned char* buf, size_t len) -> int {
                                const int fd = *static_cast<int*>(ctx);
                                const int ret = sceNetRecv(fd, buf, static_cast<unsigned int>(len), 0);
                                if (ret < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
                                return ret;
                            },
                            nullptr);

        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            *error = std::string("TLS handshake failed: ") + tls_error_string(ret);
            *cert_error = true;
            return false;
        }

        const uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
        if (flags != 0) {
            char info[256];
            mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
            *error = std::string("TLS verify failed: ") + info;
            *cert_error = true;
            return false;
        }

        ready = true;
        return true;
    }

    void shutdown() {
        if (ready) {
            mbedtls_ssl_close_notify(&ssl);
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        ready = false;
    }
};

int stream_read(Stream* stream, unsigned char* buf, std::size_t len, std::string* error) {
    for (;;) {
        int ret = 0;
        if (stream->tls) {
            ret = mbedtls_ssl_read(stream->ssl, buf, len);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (ret < 0) {
                *error = std::string("TLS read failed: ") + tls_error_string(ret);
                return -1;
            }
            return ret;
        }

        ret = sceNetRecv(stream->sock, buf, static_cast<unsigned int>(len), 0);
        if (ret < 0) {
            *error = format_error("socket read failed", ret);
            return -1;
        }
        return ret;
    }
}

bool stream_write_all(Stream* stream, const std::string& data, std::string* error) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        int ret = 0;
        if (stream->tls) {
            ret = mbedtls_ssl_write(stream->ssl,
                                    reinterpret_cast<const unsigned char*>(data.data() + offset),
                                    data.size() - offset);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (ret <= 0) {
                *error = std::string("TLS write failed: ") + tls_error_string(ret);
                return false;
            }
        } else {
            ret = sceNetSend(stream->sock,
                             reinterpret_cast<const unsigned char*>(data.data() + offset),
                             static_cast<unsigned int>(data.size() - offset), 0);
            if (ret <= 0) {
                *error = format_error("socket write failed", ret);
                return false;
            }
        }
        offset += static_cast<std::size_t>(ret);
    }
    return true;
}

bool read_more(Stream* stream, std::string* buffer, std::string* error) {
    std::array<unsigned char, READ_CHUNK> temp{};
    const int rd = stream_read(stream, temp.data(), temp.size(), error);
    if (rd < 0) return false;
    if (rd == 0) return false;
    buffer->append(reinterpret_cast<const char*>(temp.data()), static_cast<std::size_t>(rd));
    return true;
}

bool read_until(Stream* stream, std::string* data, const std::string& needle, std::size_t max_bytes,
                std::string* error) {
    while (data->find(needle) == std::string::npos) {
        if (data->size() >= max_bytes) {
            *error = "Response headers too large";
            return false;
        }
        if (!read_more(stream, data, error)) {
            *error = "Connection closed while reading headers";
            return false;
        }
    }
    return true;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool parse_hex(const std::string& text, std::size_t* value) {
    std::size_t out = 0;
    if (text.empty()) return false;
    for (char ch : text) {
        const int hv = hex_value(ch);
        if (hv < 0) return false;
        out = (out << 4) + static_cast<std::size_t>(hv);
    }
    *value = out;
    return true;
}

struct HeaderInfo {
    int status{0};
    bool chunked{false};
    bool has_length{false};
    std::size_t content_length{0};
    std::string location;
};

bool parse_headers(const std::string& header_text, HeaderInfo* info, std::string* error) {
    std::size_t line_start = 0;
    std::size_t line_end = header_text.find("\r\n");
    if (line_end == std::string::npos) {
        *error = "Invalid HTTP response";
        return false;
    }

    const std::string status_line = header_text.substr(line_start, line_end - line_start);
    const std::size_t sp = status_line.find(' ');
    if (sp == std::string::npos) {
        *error = "Invalid HTTP status";
        return false;
    }
    const std::size_t sp2 = status_line.find(' ', sp + 1);
    const std::string code = status_line.substr(sp + 1, sp2 == std::string::npos ? sp2 : sp2 - sp - 1);
    try {
        info->status = std::stoi(code);
    } catch (...) {
        *error = "Invalid HTTP status";
        return false;
    }

    line_start = line_end + 2;
    while (line_start < header_text.size()) {
        line_end = header_text.find("\r\n", line_start);
        if (line_end == std::string::npos) break;
        const std::string line = header_text.substr(line_start, line_end - line_start);
        line_start = line_end + 2;
        if (line.empty()) continue;

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = to_lower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));

        if (key == "content-length") {
            try {
                info->content_length = static_cast<std::size_t>(std::stoul(value));
                info->has_length = true;
            } catch (...) {
                info->has_length = false;
            }
        } else if (key == "transfer-encoding") {
            if (to_lower(value).find("chunked") != std::string::npos) {
                info->chunked = true;
            }
        } else if (key == "location") {
            info->location = value;
        }
    }

    return true;
}

bool read_chunked_body(Stream* stream, std::string* initial, std::size_t max_bytes, std::string* error,
                       std::string* out_body) {
    std::string buffer = std::move(*initial);
    std::string body;
    std::size_t pos = 0;

    while (true) {
        std::size_t line_end = buffer.find("\r\n", pos);
        while (line_end == std::string::npos) {
            if (!read_more(stream, &buffer, error)) {
                *error = "Connection closed while reading chunk size";
                return false;
            }
            line_end = buffer.find("\r\n", pos);
        }

        std::string line = buffer.substr(pos, line_end - pos);
        const std::size_t semi = line.find(';');
        if (semi != std::string::npos) {
            line = line.substr(0, semi);
        }
        line = trim(line);

        std::size_t chunk_size = 0;
        if (!parse_hex(line, &chunk_size)) {
            *error = "Invalid chunk size";
            return false;
        }

        pos = line_end + 2;
        if (chunk_size == 0) break;

        while (buffer.size() < pos + chunk_size + 2) {
            if (!read_more(stream, &buffer, error)) {
                *error = "Connection closed while reading chunk";
                return false;
            }
        }

        if (body.size() < max_bytes) {
            const std::size_t can_copy = std::min(chunk_size, max_bytes - body.size());
            body.append(buffer.data() + pos, can_copy);
        }

        pos += chunk_size + 2;
        if (body.size() >= max_bytes) break;
    }

    *out_body = std::move(body);
    return true;
}

bool read_fixed_body(Stream* stream, std::string* body, std::size_t length, std::size_t max_bytes,
                     std::string* error) {
    if (body->size() >= length) {
        if (body->size() > max_bytes) body->resize(max_bytes);
        return true;
    }

    std::size_t remaining = length - body->size();
    while (remaining > 0 && body->size() < max_bytes) {
        std::array<unsigned char, READ_CHUNK> temp{};
        const std::size_t want = std::min<std::size_t>(temp.size(), remaining);
        const int rd = stream_read(stream, temp.data(), want, error);
        if (rd < 0) return false;
        if (rd == 0) break;
        body->append(reinterpret_cast<const char*>(temp.data()), static_cast<std::size_t>(rd));
        remaining -= static_cast<std::size_t>(rd);
    }

    if (body->size() > max_bytes) body->resize(max_bytes);
    return true;
}

bool read_until_close(Stream* stream, std::string* body, std::size_t max_bytes, std::string* error) {
    while (body->size() < max_bytes) {
        std::array<unsigned char, READ_CHUNK> temp{};
        const int rd = stream_read(stream, temp.data(), temp.size(), error);
        if (rd < 0) return false;
        if (rd == 0) break;
        const std::size_t can_copy = std::min<std::size_t>(static_cast<std::size_t>(rd), max_bytes - body->size());
        body->append(reinterpret_cast<const char*>(temp.data()), can_copy);
        if (can_copy < static_cast<std::size_t>(rd)) break;
    }
    return true;
}

bool read_response(Stream* stream, std::size_t max_bytes, HttpResponse* out,
                   std::string* redirect, std::string* error) {
    std::string header_blob;
    if (!read_until(stream, &header_blob, "\r\n\r\n", MAX_HEADER_BYTES, error)) {
        return false;
    }

    const std::size_t header_end = header_blob.find("\r\n\r\n");
    const std::string header_text = header_blob.substr(0, header_end);
    std::string body = header_blob.substr(header_end + 4);

    HeaderInfo info;
    if (!parse_headers(header_text, &info, error)) {
        return false;
    }

    if (info.chunked) {
        std::string decoded;
        if (!read_chunked_body(stream, &body, max_bytes, error, &decoded)) {
            return false;
        }
        body = std::move(decoded);
    } else if (info.has_length) {
        if (!read_fixed_body(stream, &body, info.content_length, max_bytes, error)) {
            return false;
        }
    } else {
        if (!read_until_close(stream, &body, max_bytes, error)) {
            return false;
        }
    }

    out->status_code = info.status;
    out->body = std::move(body);
    if (!info.location.empty()) {
        *redirect = info.location;
    }
    return true;
}

bool is_redirect_status(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

std::string build_origin(const ParsedUrl& url) {
    const bool default_port = (url.scheme == "https" && url.port == 443) ||
                              (url.scheme == "http" && url.port == 80);
    std::string origin = url.scheme + "://" + url.host;
    if (!default_port) {
        origin += ":" + std::to_string(url.port);
    }
    return origin;
}

std::string build_absolute_url(const ParsedUrl& base, const std::string& location) {
    if (starts_with(location, "http://") || starts_with(location, "https://")) {
        return location;
    }
    if (starts_with(location, "//")) {
        return base.scheme + ":" + location;
    }
    if (!location.empty() && location[0] == '/') {
        return build_origin(base) + location;
    }
    return build_origin(base) + "/" + location;
}

std::string build_request(const ParsedUrl& url) {
    std::string host = url.host;
    if (!((url.scheme == "https" && url.port == 443) || (url.scheme == "http" && url.port == 80))) {
        host += ":" + std::to_string(url.port);
    }

    std::string request = "GET " + url.path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "User-Agent: chromium-vita/0.3\r\n";
    request += "Accept: */*\r\n";
    request += "Accept-Encoding: identity\r\n";
    request += "Connection: close\r\n\r\n";
    return request;
}

#endif // __vita__
} // namespace

HttpClient::HttpClient() {
#ifdef __vita__
    init_runtime();
    m_ready = g_net_ready;
#else
    m_ready = false;
#endif
}

HttpClient::~HttpClient() {
#ifdef __vita__
    term_runtime();
#endif
}

HttpResponse HttpClient::get(const std::string& raw_url, int timeout_ms, std::size_t max_bytes) const {
    HttpResponse out;

#ifdef __vita__
    if (!m_ready) {
        out.error = "network stack unavailable";
        return out;
    }

    int state = SCE_NETCTL_STATE_DISCONNECTED;
    if (sceNetCtlInetGetState(&state) >= 0 && state != SCE_NETCTL_STATE_CONNECTED) {
        out.error = (state == SCE_NETCTL_STATE_CONNECTING || state == SCE_NETCTL_STATE_FINALIZING)
                        ? "network connecting"
                        : "network disconnected";
        return out;
    }

    std::string url = normalize_url(raw_url);
    for (int redirect = 0; redirect <= MAX_REDIRECTS; ++redirect) {
        ParsedUrl parsed;
        if (!parse_url(url, &parsed, &out.error)) {
            return out;
        }

        const int sock = open_socket(parsed, timeout_ms, &out.error);
        if (sock < 0) return out;

        Stream stream;
        stream.tls = (parsed.scheme == "https");
        stream.sock = sock;

        TlsSession tls;
        bool cert_error = false;
        if (stream.tls) {
            if (!g_ca_ready) {
                out.error = g_ca_error.empty() ? "TLS CA bundle missing" : g_ca_error;
                out.cert_error = true;
                sceNetSocketClose(sock);
                return out;
            }
            if (!tls.init(sock, parsed.host, &out.error, &cert_error)) {
                out.cert_error = cert_error;
                tls.shutdown();
                sceNetSocketClose(sock);
                return out;
            }
            stream.ssl = &tls.ssl;
        }

        const std::string request = build_request(parsed);
        if (!stream_write_all(&stream, request, &out.error)) {
            tls.shutdown();
            sceNetSocketClose(sock);
            return out;
        }

        std::string redirect_url;
        if (!read_response(&stream, max_bytes, &out, &redirect_url, &out.error)) {
            tls.shutdown();
            sceNetSocketClose(sock);
            return out;
        }

        tls.shutdown();
        sceNetSocketClose(sock);

        if (!redirect_url.empty() && is_redirect_status(out.status_code) && redirect < MAX_REDIRECTS) {
            url = build_absolute_url(parsed, redirect_url);
            continue;
        }

        if (out.status_code >= 200 && out.status_code < 400) {
            out.ok = true;
        } else {
            out.error = "HTTP status " + std::to_string(out.status_code);
        }
        return out;
    }

    out.error = "Too many redirects";
#else
    (void)raw_url;
    (void)timeout_ms;
    (void)max_bytes;
    out.error = "HTTP backend available only on Vita runtime";
#endif

    return out;
}

} // namespace browser
