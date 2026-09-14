#include "text_utils.h"
#include "platform_paths.h"

#include <cwctype>

// Same local using-directive src/export_html.cpp and src/export_docx.cpp use.
// In utils.cpp this came free from app.h, which this file must not include.
using namespace qmd;

// Moved verbatim out of src/utils.cpp. The bodies are unchanged -- this commit
// is a topology change, not a behaviour change, and the assertions in
// tests/text_utils_tests.cpp were written against the behaviour these had
// before the move rather than after it.

std::wstring toWide(const std::string& str) {
    return platform::fromUtf8(str);
}

std::wstring toLower(const std::wstring& str) {
    std::wstring result = str;
    for (auto& c : result) {
        c = towlower(c);
    }
    return result;
}

bool isWordBoundary(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' ||
           c == L'.' || c == L',' || c == L';' || c == L':' ||
           c == L'!' || c == L'?' || c == L'"' || c == L'\'' ||
           c == L'(' || c == L')' || c == L'[' || c == L']' ||
           c == L'{' || c == L'}' || c == L'<' || c == L'>' ||
           c == L'/' || c == L'\\' || c == L'-' || c == L'=' ||
           c == L'+' || c == L'*' || c == L'&' || c == L'|';
}

std::string slugifyHeading(const std::wstring& text) {
    std::wstring filtered;
    filtered.reserve(text.size());
    auto isWidePunct = [](wchar_t c) {
        // Punctuation is stripped from slugs in any script (GitHub rule):
        // general punctuation (— …), CJK symbols/punctuation (、。「」【】),
        // vertical/compat forms, and fullwidth ASCII punctuation variants
        return (c >= 0x2000 && c <= 0x206F) ||
               (c >= 0x3000 && c <= 0x303F) ||
               (c >= 0xFE30 && c <= 0xFE4F) ||
               (c >= 0xFF00 && c <= 0xFF0F) ||
               (c >= 0xFF1A && c <= 0xFF20) ||
               (c >= 0xFF3B && c <= 0xFF40) ||
               (c >= 0xFF5B && c <= 0xFF65);
    };
    for (wchar_t c : text) {
        if (c >= 0x80) {
            // GitHub keeps non-ASCII letters in slugs (CJK headings form
            // valid anchors) but still strips punctuation; iswalnum() is
            // unreliable for both under the default C locale
            if (!isWidePunct(c)) filtered += c;
        } else if (iswalnum(c)) {
            filtered += towlower(c);
        } else if (c == L' ') {
            filtered += L'-';
        } else if (c == L'-' || c == L'_') {
            filtered += c;
        }
        // else: drop ASCII punctuation, matching GitHub's slug rules
    }
    if (filtered.empty()) return {};

    return platform::toUtf8(filtered);
}

void extractText(const qmd::ElementPtr& elem, std::wstring& out) {
    if (!elem) return;

    switch (elem->type) {
        case ElementType::Text:
            out += toWide(elem->text);
            break;
        case ElementType::SoftBreak:
            out += L" ";
            break;
        case ElementType::HardBreak:
            out += L"\n";
            break;
        case ElementType::Paragraph:
        case ElementType::Heading:
        case ElementType::ListItem:
            for (const auto& child : elem->children) {
                extractText(child, out);
            }
            out += L"\n\n";
            break;
        case ElementType::CodeBlock: {
            out += L"\n";
            for (const auto& child : elem->children) {
                if (child->type == ElementType::Text) {
                    out += toWide(child->text);
                }
            }
            out += L"\n\n";
            break;
        }
        case ElementType::MermaidDiagram:
            out += toWide(elem->text);
            if (!out.empty() && out.back() != L'\n') out += L"\n";
            break;
        case ElementType::Ruby:
            // Include base text but skip RubyText (annotation)
            for (const auto& child : elem->children) {
                if (child->type != ElementType::RubyText) {
                    extractText(child, out);
                }
            }
            break;
        default:
            for (const auto& child : elem->children) {
                extractText(child, out);
            }
            break;
    }
}
