#include "ime.h"

#include <algorithm>
#include <array>

#ifdef __vita__
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#endif

namespace platform::vita {

namespace {
constexpr std::size_t MAX_TEXT = Ime::kMaxText;
constexpr char32_t REPLACEMENT_CHAR = 0xFFFD;

static void append_utf8(std::string& out, char32_t code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

static void append_utf16(std::u16string& out, char32_t code_point) {
    if (code_point <= 0xFFFF) {
        if (code_point >= 0xD800 && code_point <= 0xDFFF) {
            out.push_back(static_cast<char16_t>(REPLACEMENT_CHAR));
        } else {
            out.push_back(static_cast<char16_t>(code_point));
        }
        return;
    }

    if (code_point > 0x10FFFF) {
        out.push_back(static_cast<char16_t>(REPLACEMENT_CHAR));
        return;
    }

    code_point -= 0x10000;
    out.push_back(static_cast<char16_t>(0xD800 + (code_point >> 10)));
    out.push_back(static_cast<char16_t>(0xDC00 + (code_point & 0x3FF)));
}

static std::u16string to_utf16(const std::string& in) {
    std::u16string out;
    out.reserve(in.size());

    for (std::size_t i = 0; i < in.size();) {
        unsigned char lead = static_cast<unsigned char>(in[i]);

        if (lead <= 0x7F) {
            out.push_back(static_cast<char16_t>(lead));
            ++i;
            continue;
        }

        char32_t code_point = REPLACEMENT_CHAR;
        std::size_t length = 1;
        bool valid = false;

        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            if (i + 1 < in.size()) {
                unsigned char b1 = static_cast<unsigned char>(in[i + 1]);
                if ((b1 & 0xC0) == 0x80) {
                    char32_t cp = ((lead & 0x1F) << 6) | (b1 & 0x3F);
                    if (cp >= 0x80) {
                        code_point = cp;
                        valid = true;
                    }
                }
            }
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            if (i + 2 < in.size()) {
                unsigned char b1 = static_cast<unsigned char>(in[i + 1]);
                unsigned char b2 = static_cast<unsigned char>(in[i + 2]);
                if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                    char32_t cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                    if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        code_point = cp;
                        valid = true;
                    }
                }
            }
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            if (i + 3 < in.size()) {
                unsigned char b1 = static_cast<unsigned char>(in[i + 1]);
                unsigned char b2 = static_cast<unsigned char>(in[i + 2]);
                unsigned char b3 = static_cast<unsigned char>(in[i + 3]);
                if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
                    char32_t cp = ((lead & 0x07) << 18) | ((b1 & 0x3F) << 12) |
                        ((b2 & 0x3F) << 6) | (b3 & 0x3F);
                    if (cp >= 0x10000 && cp <= 0x10FFFF) {
                        code_point = cp;
                        valid = true;
                    }
                }
            }
        }

        append_utf16(out, valid ? code_point : REPLACEMENT_CHAR);
        i += valid ? length : 1;
    }

    return out;
}

static std::string to_utf8(const char16_t* in) {
    std::string out;

    while (*in) {
        char32_t code_point = 0;
        char16_t first = *in++;

        if (first >= 0xD800 && first <= 0xDBFF) {
            char16_t second = *in;
            if (second >= 0xDC00 && second <= 0xDFFF) {
                ++in;
                code_point = 0x10000 + (((static_cast<char32_t>(first) - 0xD800) << 10) |
                    (static_cast<char32_t>(second) - 0xDC00));
            } else {
                code_point = REPLACEMENT_CHAR;
            }
        } else if (first >= 0xDC00 && first <= 0xDFFF) {
            code_point = REPLACEMENT_CHAR;
        } else {
            code_point = first;
        }

        append_utf8(out, code_point);
    }

    return out;
}
} // namespace

bool Ime::prompt_url(const std::string& title, const std::string& initial_text, std::string& out_text) {
#ifdef __vita__
    bool accepted = false;
    bool finished = false;
    std::string current = initial_text;

    if (!begin_url(title, initial_text)) {
        return false;
    }

    while (active()) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();

        update_url(current, accepted, finished);
        // Yield ~1 frame to avoid busy-waiting while the dialog is active.
        sceKernelDelayThread(16 * 1000);
    }

    if (accepted && !current.empty()) {
        out_text = current;
        return true;
    }
    return false;
#else
    (void)title;
    (void)initial_text;
    (void)out_text;
    return false;
#endif
}

bool Ime::begin_url(const std::string& title, const std::string& initial_text) {
#ifdef __vita__
    if (m_active) return false;

    m_title16 = to_utf16(title);
    if (m_title16.size() > MAX_TEXT) {
        m_title16.resize(MAX_TEXT);
    }

    m_initial16 = to_utf16(initial_text);
    if (m_initial16.size() > MAX_TEXT) {
        m_initial16.resize(MAX_TEXT);
    }

    m_buffer.fill(0);
    std::copy(m_initial16.begin(), m_initial16.end(), m_buffer.begin());

    SceImeDialogParam param{};
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = 1;
    param.type = SCE_IME_TYPE_URL;
    param.option = 0;
    param.title = reinterpret_cast<const SceWChar16*>(m_title16.c_str());
    param.initialText = reinterpret_cast<SceWChar16*>(const_cast<char16_t*>(m_initial16.c_str()));
    param.maxTextLength = MAX_TEXT;
    param.inputTextBuffer = reinterpret_cast<SceWChar16*>(m_buffer.data());

    const int init_res = sceImeDialogInit(&param);
    m_last_init = init_res;
    if (init_res < 0) {
        return false;
    }

    m_active = true;
    m_cancel_requested = false;
    m_seen_running = false;
    m_idle_frames = 0;
    m_last_status = SCE_COMMON_DIALOG_STATUS_NONE;
    return true;
#else
    (void)title;
    (void)initial_text;
    return false;
#endif
}

bool Ime::update_url(std::string& out_text, bool& accepted, bool& finished) {
    accepted = false;
    finished = false;
#ifdef __vita__
    if (!m_active) return false;

    m_buffer[MAX_TEXT] = 0;
    out_text = to_utf8(m_buffer.data());

    const auto status = sceImeDialogGetStatus();
    m_last_status = static_cast<int>(status);
    if (status == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        m_seen_running = true;
        m_idle_frames = 0;
    } else if (!m_seen_running && status == SCE_COMMON_DIALOG_STATUS_NONE) {
        ++m_idle_frames;
    }

    if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result{};
        if (!m_cancel_requested && sceImeDialogGetResult(&result) >= 0 &&
            result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
            accepted = true;
        }
    }

    if (!m_seen_running && m_idle_frames > 120) {
        sceImeDialogTerm();
        m_active = false;
        m_cancel_requested = false;
        m_seen_running = false;
        m_idle_frames = 0;
        finished = true;
        return true;
    }

    if (status != SCE_COMMON_DIALOG_STATUS_RUNNING && (m_seen_running || m_cancel_requested)) {
        sceImeDialogTerm();
        m_active = false;
        m_cancel_requested = false;
        m_seen_running = false;
        m_idle_frames = 0;
        finished = true;
    }

    return true;
#else
    (void)out_text;
    return false;
#endif
}

void Ime::cancel() {
#ifdef __vita__
    if (!m_active) return;
    m_cancel_requested = true;
    sceImeDialogAbort();
#endif
}

bool Ime::active() const {
#ifdef __vita__
    return m_active;
#else
    return false;
#endif
}

int Ime::last_init_result() const {
#ifdef __vita__
    return m_last_init;
#else
    return 0;
#endif
}

int Ime::last_status() const {
#ifdef __vita__
    return m_last_status;
#else
    return 0;
#endif
}

int Ime::idle_frames() const {
#ifdef __vita__
    return m_idle_frames;
#else
    return 0;
#endif
}

bool Ime::seen_running() const {
#ifdef __vita__
    return m_seen_running;
#else
    return false;
#endif
}

} // namespace platform::vita
