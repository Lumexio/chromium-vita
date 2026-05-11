#include "http_client.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#ifdef __vita__
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/io/fcntl.h>
#include <psp2/libssl.h>
#endif

namespace browser {

#ifdef __vita__
namespace {
constexpr int NET_POOL_SIZE  = 4 * 1024 * 1024;
constexpr int HTTP_POOL_SIZE = 4 * 1024 * 1024;
constexpr const char* CA_BUNDLE_PATH = "ux0:data/chromium-vita/ca-bundle.der";

bool g_net_ready  = false;
bool g_http_ready = false;
bool g_ssl_ready  = false;
bool g_https_configured = false;
bool g_ca_ready = false;
bool g_ca_loaded = false;
int  g_ref_count  = 0;

std::vector<char> g_ca_bundle;
std::string g_https_error;

std::array<char, NET_POOL_SIZE> g_net_mem{};

std::string format_error(const char* what, int code) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s (0x%08X)", what, static_cast<unsigned int>(code));
    return std::string(buf);
}

bool load_ca_bundle_from_file() {
    SceUID fd = sceIoOpen(CA_BUNDLE_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) return false;

    const SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (size <= 0) {
        sceIoClose(fd);
        return false;
    }

    sceIoLseek(fd, 0, SCE_SEEK_SET);
    g_ca_bundle.assign(static_cast<std::size_t>(size), 0);
    const int read_bytes = sceIoRead(fd, g_ca_bundle.data(), static_cast<unsigned int>(size));
    sceIoClose(fd);

    if (read_bytes != static_cast<int>(size)) {
        g_ca_bundle.clear();
        return false;
    }

    SceHttpsData ca_data{g_ca_bundle.data(), static_cast<unsigned int>(g_ca_bundle.size())};
    const SceHttpsData* ca_list[] = {&ca_data};
    const int res = sceHttpsLoadCert(1, ca_list, nullptr, nullptr);
    if (res < 0) {
        g_ca_bundle.clear();
        return false;
    }

    g_ca_loaded = true;
    return true;
}

bool detect_system_ca_list() {
    SceHttpsCaList list{};
    const int res = sceHttpsGetCaList(&list);
    if (res < 0) return false;
    const bool has_ca = (list.caNum > 0 && list.caCerts != nullptr);
    sceHttpsFreeCaList(&list);
    return has_ca;
}

void init_runtime() {
    if (g_ref_count > 0 && g_net_ready && g_ssl_ready && g_http_ready) {
        ++g_ref_count;
        return;
    }

    ++g_ref_count;

    SceNetInitParam net_param{};
    net_param.memory = g_net_mem.data();
    net_param.size = NET_POOL_SIZE;
    net_param.flags = 0;

    if (!g_net_ready && sceNetInit(&net_param) >= 0) {
        g_net_ready = true;
        sceNetCtlInit();
    }

    if (!g_ssl_ready && sceSslInit(HTTP_POOL_SIZE) >= 0) {
        g_ssl_ready = true;
    }

    if (!g_http_ready && sceHttpInit(HTTP_POOL_SIZE) >= 0) {
        g_http_ready = true;
    }

    if (!g_https_configured) {
        g_ca_ready = detect_system_ca_list();
        if (!g_ca_ready) {
            g_ca_ready = load_ca_bundle_from_file();
            if (!g_ca_ready) {
                g_https_error = "TLS CA bundle missing. Install " + std::string(CA_BUNDLE_PATH);
            }
        }

        if (g_ca_ready) {
            sceHttpsEnableOption(SCE_HTTPS_FLAG_SERVER_VERIFY | SCE_HTTPS_FLAG_CN_CHECK |
                                 SCE_HTTPS_FLAG_NOT_AFTER_CHECK | SCE_HTTPS_FLAG_NOT_BEFORE_CHECK |
                                 SCE_HTTPS_FLAG_KNOWN_CA_CHECK);
        }
        g_https_configured = true;
    }
}

void term_runtime() {
    if (--g_ref_count > 0) return;

    if (g_ca_loaded) {
        sceHttpsUnloadCert();
        g_ca_loaded = false;
        g_ca_bundle.clear();
    }
    g_ca_ready = false;
    g_https_configured = false;
    g_https_error.clear();

    if (g_http_ready) {
        sceHttpTerm();
        g_http_ready = false;
    }

    if (g_ssl_ready) {
        sceSslTerm();
        g_ssl_ready = false;
    }

    if (g_net_ready) {
        sceNetCtlTerm();
        sceNetTerm();
        g_net_ready = false;
    }
}

bool starts_with_http(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string normalize_url(const std::string& url) {
    if (starts_with_http(url)) return url;
    return std::string("https://") + url;
}

bool is_https_url(const std::string& url) {
    return url.rfind("https://", 0) == 0;
}
} // namespace
#endif

HttpClient::HttpClient() {
#ifdef __vita__
    init_runtime();
    m_ready = g_net_ready && g_http_ready && g_ssl_ready;
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

    std::string url = normalize_url(raw_url);

    if (is_https_url(url) && !g_ca_ready) {
        out.error = g_https_error.empty() ? "TLS CA list unavailable" : g_https_error;
        out.cert_error = true;
        return out;
    }

    int state = SCE_NETCTL_STATE_DISCONNECTED;
    if (sceNetCtlInetGetState(&state) >= 0 && state != SCE_NETCTL_STATE_CONNECTED) {
        out.error = (state == SCE_NETCTL_STATE_CONNECTING || state == SCE_NETCTL_STATE_FINALIZING)
                        ? "network connecting"
                        : "network disconnected";
        return out;
    }

    int tmpl = sceHttpCreateTemplate("chromium-vita/0.2", SCE_HTTP_VERSION_1_1, 1);
    if (tmpl < 0) {
        out.error = format_error("sceHttpCreateTemplate failed", tmpl);
        return out;
    }

    sceHttpSetAutoRedirect(tmpl, SCE_HTTP_ENABLE);

    sceHttpSetConnectTimeOut(tmpl, timeout_ms * 1000);
    sceHttpSetRecvTimeOut(tmpl, timeout_ms * 1000);
    sceHttpSetSendTimeOut(tmpl, timeout_ms * 1000);

    int conn = sceHttpCreateConnectionWithURL(tmpl, url.c_str(), 0);
    if (conn < 0) {
        sceHttpDeleteTemplate(tmpl);
        out.error = format_error("sceHttpCreateConnectionWithURL failed", conn);
        return out;
    }

    int req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url.c_str(), 0);
    if (req < 0) {
        sceHttpDeleteConnection(conn);
        sceHttpDeleteTemplate(tmpl);
        out.error = format_error("sceHttpCreateRequestWithURL failed", req);
        return out;
    }

    int send_res = sceHttpSendRequest(req, nullptr, 0);
    if (send_res < 0) {
        if (is_https_url(url) && send_res == SCE_HTTP_ERROR_SSL) {
            int ssl_err = 0;
            unsigned int ssl_detail = 0;
            if (sceHttpsGetSslError(req, &ssl_err, &ssl_detail) >= 0) {
                if (ssl_err == SCE_SSL_ERROR_INVALID_VALUE) {
                    out.error = "TLS handshake failed (possible TLS version mismatch)";
                } else {
                    out.error = format_error("TLS error", ssl_err);
                }
                out.cert_error = true;
            } else {
                out.error = format_error("TLS handshake failed", send_res);
                out.cert_error = true;
            }
        } else {
            out.error = format_error("sceHttpSendRequest failed", send_res);
        }
        sceHttpDeleteRequest(req);
        sceHttpDeleteConnection(conn);
        sceHttpDeleteTemplate(tmpl);
        return out;
    }

    int status = 0;
    sceHttpGetStatusCode(req, &status);
    out.status_code = status;

    std::array<char, 2048> buf{};
    while (out.body.size() < max_bytes) {
        int rd = sceHttpReadData(req, buf.data(), static_cast<unsigned int>(buf.size()));
        if (rd <= 0) break;
        std::size_t remaining = max_bytes - out.body.size();
        std::size_t n = std::min<std::size_t>(remaining, static_cast<std::size_t>(rd));
        out.body.append(buf.data(), n);
        if (n < static_cast<std::size_t>(rd)) break;
    }

    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tmpl);

    if (out.status_code >= 200 && out.status_code < 400) {
        out.ok = true;
    } else {
        out.error = "HTTP status " + std::to_string(out.status_code);
    }
#else
    (void)raw_url;
    (void)timeout_ms;
    (void)max_bytes;
    out.error = "HTTP backend available only on Vita runtime";
#endif

    return out;
}

} // namespace browser
