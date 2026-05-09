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

static std::u16string to_utf16(const std::string& in) {
    std::u16string out;
    out.reserve(in.size());
    for (unsigned char c : in) out.push_back(static_cast<char16_t>(c));
    return out;
}

static std::string to_utf8(const char16_t* in) {
    std::string out;
    while (*in) {
        out.push_back(static_cast<char>(*in & 0xFF));
        ++in;
    }
    return out;
}
} // namespace

bool Ime::prompt_url(const std::string& title, const std::string& initial_text, std::string& out_text) const {
#ifdef __vita__
    std::u16string title16   = to_utf16(title.substr(0, MAX_TEXT));
    std::u16string initial16 = to_utf16(initial_text.substr(0, MAX_TEXT));
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
