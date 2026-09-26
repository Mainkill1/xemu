// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xemu::shader_browser {

enum class GlslTokenKind : uint8_t {
    Keyword,
    Number,
    Comment,
    Preprocessor,
};

struct GlslToken {
    size_t start = 0;
    size_t end = 0;
    GlslTokenKind kind = GlslTokenKind::Keyword;
};

struct GlslDiagnostic {
    uint32_t line = 0; // One-based; zero means the compiler supplied no line.
    std::string message;
};

GlslDiagnostic ParseGlslDiagnosticLine(const std::string &message);

class GlslEditorModel
{
public:
    bool SetSource(const std::string &source);
    const std::string &Source() const { return source_; }
    size_t LineCount() const { return offsets_.size(); }
    std::string_view Line(size_t line) const;
    size_t LineForOffset(size_t offset) const;
    std::optional<size_t> FindNext(std::string_view query,
                                   size_t after_offset) const;
    std::vector<GlslToken> TokenizeLine(size_t line) const;

private:
    std::string source_;
    std::vector<size_t> offsets_{0};
};

} // namespace xemu::shader_browser
