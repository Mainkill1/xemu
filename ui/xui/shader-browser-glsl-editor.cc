// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-glsl-editor.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace xemu::shader_browser {
namespace {

bool IsWord(unsigned char c)
{
    return std::isalnum(c) || c == '_';
}

bool IsKeyword(std::string_view word)
{
    constexpr std::array<std::string_view, 25> keywords = {
        "bool", "break", "const", "continue", "discard", "do", "else",
        "false", "float", "for", "if", "in", "inout", "int", "layout",
        "mat4", "out", "return", "sampler2D", "samplerCube", "struct",
        "true", "uniform", "vec4", "void"
    };
    return std::find(keywords.begin(), keywords.end(), word) !=
           keywords.end();
}

uint32_t ParseDecimal(const std::string &text, size_t start, size_t *end)
{
    uint64_t value = 0;
    size_t i = start;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        value = value * 10 + static_cast<unsigned>(text[i] - '0');
        if (value > std::numeric_limits<uint32_t>::max()) return 0;
        ++i;
    }
    if (i == start) return 0;
    if (end) *end = i;
    return static_cast<uint32_t>(value);
}

} // namespace

GlslDiagnostic ParseGlslDiagnosticLine(const std::string &message)
{
    GlslDiagnostic diagnostic{};
    diagnostic.message = message.substr(0, 8192);
    // glslang: ERROR: 0:12: ..., drivers: 0(12) : ...
    const size_t start = message.find("0:");
    if (start != std::string::npos) {
        size_t end = start + 2;
        uint32_t line = ParseDecimal(message, end, &end);
        if (line && end < message.size() && message[end] == ':') {
            diagnostic.line = line;
            return diagnostic;
        }
    }
    const size_t driver = message.find("0(");
    if (driver != std::string::npos) {
        size_t end = driver + 2;
        uint32_t line = ParseDecimal(message, end, &end);
        if (line && end < message.size() && message[end] == ')')
            diagnostic.line = line;
    }
    return diagnostic;
}

bool GlslEditorModel::SetSource(const std::string &source)
{
    constexpr size_t max_source_bytes = 4U * 1024U * 1024U;
    if (source.size() > max_source_bytes ||
        source.find('\0') != std::string::npos) return false;
    source_ = source;
    offsets_.clear();
    offsets_.push_back(0);
    for (size_t i = 0; i < source_.size(); ++i) {
        if (source_[i] == '\n' && i + 1 < source_.size())
            offsets_.push_back(i + 1);
    }
    return true;
}

std::string_view GlslEditorModel::Line(size_t line) const
{
    if (line >= offsets_.size()) return {};
    size_t begin = offsets_[line];
    size_t end = line + 1 < offsets_.size() ? offsets_[line + 1] :
                                               source_.size();
    if (end > begin && source_[end - 1] == '\n') --end;
    if (end > begin && source_[end - 1] == '\r') --end;
    return std::string_view(source_.data() + begin, end - begin);
}

size_t GlslEditorModel::LineForOffset(size_t offset) const
{
    auto it = std::upper_bound(offsets_.begin(), offsets_.end(),
                               std::min(offset, source_.size()));
    return static_cast<size_t>(it - offsets_.begin() - 1);
}

std::optional<size_t> GlslEditorModel::FindNext(
    std::string_view query, size_t after_offset) const
{
    if (query.empty() || query.size() > source_.size()) return std::nullopt;
    size_t begin = std::min(after_offset + (after_offset < source_.size()),
                            source_.size());
    size_t found = source_.find(query, begin);
    if (found == std::string::npos) found = source_.find(query, 0);
    if (found == std::string::npos) return std::nullopt;
    return found;
}

std::vector<GlslToken> GlslEditorModel::TokenizeLine(size_t line) const
{
    std::vector<GlslToken> tokens;
    std::string_view text = Line(line);
    size_t leading = text.find_first_not_of(" \t");
    if (leading != std::string_view::npos && text[leading] == '#') {
        tokens.push_back({leading, text.size(), GlslTokenKind::Preprocessor});
        return tokens;
    }
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            tokens.push_back({i, text.size(), GlslTokenKind::Comment});
            break;
        }
        if (std::isdigit(c) ||
            (c == '.' && i + 1 < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            size_t start = i++;
            while (i < text.size() &&
                   (IsWord(static_cast<unsigned char>(text[i])) ||
                    text[i] == '.')) ++i;
            tokens.push_back({start, i, GlslTokenKind::Number});
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            size_t start = i++;
            while (i < text.size() &&
                   IsWord(static_cast<unsigned char>(text[i]))) ++i;
            if (IsKeyword(text.substr(start, i - start)))
                tokens.push_back({start, i, GlslTokenKind::Keyword});
            continue;
        }
        ++i;
    }
    return tokens;
}

} // namespace xemu::shader_browser
