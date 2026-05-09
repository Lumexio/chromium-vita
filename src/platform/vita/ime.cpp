#include "ime.h"

#include <algorithm>
#include <array>

#ifdef __vita__
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#endif

namespace platform::vita {

namespace {
constexpr std::size_t MAX_TEXT = 255;
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

bool Ime::prompt_url(const std::string& title, const std::string& initial_text, std::string& out_text) const {
#ifdef __vita__
    std::u16string title16 = to_utf16(title);
    if (title16.size() > MAX_TEXT) {
        title16.resize(MAX_TEXT);
    }

    std::u16string initial16 = to_utf16(initial_text);
    if (initial16.size() > MAX_TEXT) {
        initial16.resize(MAX_TEXT);
    }

    std::array<char16_t, MAX_TEXT + 1> buffer{};
    std::copy(initial16.begin(), initial16.end(), buffer.begin());

    SceImeDialogParam param{};
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = 1;
    param.type = SCE_IME_TYPE_URL;
    param.option = 0;
    param.title = reinterpret_cast<const SceWChar16*>(title16.c_str());
    param.initialText = reinterpret_cast<SceWChar16*>(const_cast<char16_t*>(initial16.c_str()));
    param.maxTextLength = MAX_TEXT;
    param.inputTextBuffer = reinterpret_cast<SceWChar16*>(buffer.data());

    if (sceImeDialogInit(&param) < 0) {
        return false;
    }

    while (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
    }

    bool accepted = false;
    if (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result{};
        if (sceImeDialogGetResult(&result) >= 0 && result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
            out_text = to_utf8(buffer.data());
            accepted = !out_text.empty();
        }
    }

    sceImeDialogTerm();
    return accepted;
#else
    (void)title;
    (void)initial_text;
    (void)out_text;
    return false;
#endif
}

} // namespace platform::vita
