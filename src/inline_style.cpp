#include "inline_style.h"
#include "platform_paths.h"

#include <cctype>
#include <filesystem>

namespace {

// These stay as local names so the call sites below read unchanged, but the
// conversion itself now lives in the platform seam. The original comment here
// said "Local converters keep this file free of editor.cpp for the test target"
// -- that was the real reason for the duplication, and the seam serves it
// better: no editor.cpp, and no windows.h either.
std::wstring refToWide(const std::string& str) {
    return platform::fromUtf8(str);
}

std::string refToUtf8(const std::wstring& wide) {
    return platform::toUtf8(wide);
}

// %20 and friends, for [text](my%20notes.md) targets
std::string percentDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size() &&
            std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            out += static_cast<char>(
                std::stoi(value.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

// A real [text](target) whose target is a local text-file path gets the
// same live/broken treatment as a plain-text reference - any extension
// the plain-path scanner recognizes, not just markdown (#162)
bool isLocalFileRefUrl(const std::string& url) {
    if (url.empty() || url[0] == '#') return false;
    if (url.find("://") != std::string::npos) return false;
    if (url.rfind("//", 0) == 0 || url.rfind("\\\\", 0) == 0) return false;
    for (size_t i = 0; i < url.size(); i++) {
        if (url[i] == ':' &&
            !(i == 1 && std::isalpha(static_cast<unsigned char>(url[0])))) {
            return false;  // wiki:, mailto:, and other schemes
        }
    }
    return qmd::fileRefKnownExtension(url);
}

// Resolves against the current file's folder and checks the disk, cached
// per resolved path until the next full relayout
bool resolveFileRef(App& app, const std::string& raw, std::string& absOut) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path path(refToWide(percentDecode(raw)));
    if (path.is_relative()) {
        if (app.currentFile.empty()) {
            absOut = raw;
            return false;  // unsaved buffer: nothing to resolve against
        }
        path = fs::path(refToWide(app.currentFile)).parent_path() / path;
    }
    path = path.lexically_normal();
    absOut = refToUtf8(path.wstring());
    auto found = app.fileRefCache.find(absOut);
    if (found != app.fileRefCache.end()) return found->second;
    bool exists = fs::exists(path, ec) && !fs::is_directory(path, ec);
    app.fileRefCache[absOut] = exists;
    return exists;
}

}  // namespace

tinta::TextFormat* emphasisFormat(App& app, tinta::TextFormat* base, bool bold, bool italic) {
    // A heading keeps its own face and size. It is bold already, so **bold** leaves it as
    // it is and *italic* slants it. Answering with the body formats below drew the word at
    // 16px on a heading's line, top-aligned, where it read as a superscript.
    for (int i = 0; i < 6; i++) {
        if (base && (base == app.headingFormats[i] || base == app.headingItalicFormats[i])) {
            if (italic && app.headingItalicFormats[i]) return app.headingItalicFormats[i];
            return app.headingFormats[i];
        }
    }
    // A table header cell lays out in boldFormat, so *italic* there stays bold
    if (base && (base == app.boldFormat || base == app.boldItalicFormat)) bold = true;
    tinta::TextFormat* fmt = nullptr;
    if (bold && italic) fmt = app.boldItalicFormat ? app.boldItalicFormat : app.boldFormat;
    else if (bold) fmt = app.boldFormat;
    else if (italic) fmt = app.italicFormat;
    return fmt ? fmt : base;
}

tinta::TextFormat* inlineCodeFormat(App& app, bool bold, bool italic) {
    tinta::TextFormat* fmt = nullptr;
    if (bold && italic) fmt = app.codeBoldItalicFormat;
    else if (bold) fmt = app.codeBoldFormat;
    else if (italic) fmt = app.codeItalicFormat;
    return fmt ? fmt : app.codeFormat;
}

void flattenInline(App& app, const std::vector<ElementPtr>& elements,
                   const InlineStyle& inherited, float lineHeight,
                   std::vector<StyledRun>& out) {
    for (const auto& elem : elements) {
        InlineStyle st = inherited;
        switch (elem->type) {
            case ElementType::Text:
            case ElementType::Code:
            case ElementType::Image:
            case ElementType::Ruby:
            case ElementType::SoftBreak:
            case ElementType::HardBreak:
            case ElementType::MathInline:
            case ElementType::MathDisplay:
                out.push_back({elem, st});
                continue;

            case ElementType::Strong:
            case ElementType::Emphasis:
                st.bold = st.bold || elem->type == ElementType::Strong;
                st.italic = st.italic || elem->type == ElementType::Emphasis;
                if (!st.fixedSize) st.format = emphasisFormat(app, st.format, st.bold, st.italic);
                break;

            case ElementType::Strikethrough:
                st.hasStrike = true;
                break;

            case ElementType::Highlight:
                // ==text== renders on a marker-pen background
                st.hasBg = true;
                st.bgColor = app.theme.isDark
                    ? tinta::colorF(0.98f, 0.80f, 0.25f, 0.28f)
                    : tinta::colorF(1.00f, 0.88f, 0.20f, 0.45f);
                break;

            case ElementType::Superscript:
            case ElementType::Subscript:
                // Small text; NEAR alignment already sits sup at the top of the line
                if (app.supSubFormat) {
                    st.format = app.supSubFormat;
                    st.fixedSize = true;
                }
                if (elem->type == ElementType::Subscript) st.drawYOffset = lineHeight * 0.38f;
                break;

            case ElementType::Link: {
                st.color = app.theme.link;
                st.linkUrl = elem->url;
                st.isLink = true;
                std::string target;
                if (elem->url.rfind("fileref:", 0) == 0) {
                    target = elem->url.substr(8);
                } else if (isLocalFileRefUrl(elem->url)) {
                    target = elem->url;
                }
                if (!target.empty()) {
                    std::string resolved;
                    if (resolveFileRef(app, target, resolved)) {
                        st.linkUrl = "fileref-ok:" + resolved;
                    } else {
                        // Missing target: a ghost of the link color, inert
                        st.linkUrl = "fileref-missing:" + resolved;
                        st.color.a *= 0.45f;
                    }
                }
                break;
            }

            default:
                break;  // unknown wrapper: keep the style and descend
        }
        flattenInline(app, elem->children, st, lineHeight, out);
    }
}
