// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-source-summary.hh"

#include <algorithm>
#include <array>

namespace xemu::shader_browser {
namespace {

bool IdentifierStart(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           value == '_';
}

bool IdentifierPart(char value)
{
    return IdentifierStart(value) || (value >= '0' && value <= '9');
}

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source)
    {
    }

    std::string_view Next()
    {
        while (position_ < source_.size()) {
            const char value = source_[position_];
            if (value == '\n') {
                line_start_ = true;
                ++position_;
            } else if (value == ' ' || value == '\t' || value == '\r' ||
                       value == '\v' || value == '\f') {
                ++position_;
            } else if (line_start_ && value == '#') {
                SkipDirective();
            } else if (Starts("//")) {
                position_ += 2;
                while (position_ < source_.size() && source_[position_] != '\n')
                    ++position_;
            } else if (Starts("/*")) {
                position_ += 2;
                while (position_ < source_.size() && !Starts("*/")) {
                    if (source_[position_] == '\n')
                        line_start_ = true;
                    ++position_;
                }
                if (position_ == source_.size()) {
                    malformed_ = true;
                    break;
                }
                position_ += 2;
            } else if (value == '"' || value == '\'') {
                line_start_ = false;
                SkipString(value);
            } else {
                line_start_ = false;
                const size_t start = position_++;
                if (IdentifierStart(value)) {
                    while (position_ < source_.size() &&
                           IdentifierPart(source_[position_]))
                        ++position_;
                }
                return source_.substr(start, position_ - start);
            }
        }
        return {};
    }

    bool Malformed() const
    {
        return malformed_;
    }

private:
    bool Starts(std::string_view text) const
    {
        return source_.substr(position_, text.size()) == text;
    }

    void SkipDirective()
    {
        while (position_ < source_.size()) {
            if (source_[position_++] != '\n')
                continue;
            size_t end = position_ - 1;
            if (end && source_[end - 1] == '\r')
                --end;
            if (!end || source_[end - 1] != '\\')
                break;
        }
        line_start_ = true;
    }

    void SkipString(char quote)
    {
        ++position_;
        while (position_ < source_.size()) {
            const char value = source_[position_++];
            if (value == quote)
                return;
            if (value == '\\' && position_ < source_.size())
                ++position_;
        }
        malformed_ = true;
    }

    std::string_view source_;
    size_t position_ = 0;
    bool line_start_ = true, malformed_ = false;
};

template <size_t Size>
bool OneOf(std::string_view token,
           const std::array<std::string_view, Size> &names)
{
    return std::find(names.begin(), names.end(), token) != names.end();
}

void CountCall(GlslSourceSummary &summary, std::string_view name)
{
    static constexpr std::array<std::string_view, 30> texture_names = {
        "texture",
        "textureProj",
        "textureLod",
        "textureOffset",
        "texelFetch",
        "texelFetchOffset",
        "textureProjOffset",
        "textureLodOffset",
        "textureProjLod",
        "textureProjLodOffset",
        "textureGrad",
        "textureGradOffset",
        "textureProjGrad",
        "textureProjGradOffset",
        "textureGather",
        "textureGatherOffset",
        "textureGatherOffsets",
        "texture1D",
        "texture2D",
        "texture3D",
        "textureCube",
        "texture1DProj",
        "texture2DProj",
        "texture3DProj",
        "texture1DLod",
        "texture2DLod",
        "texture3DLod",
        "textureCubeLod",
        "shadow1D",
        "shadow2D"
    };
    static constexpr std::array<std::string_view, 9> derivative_names = {
        "dFdx",       "dFdy",       "fwidth",     "dFdxFine",    "dFdyFine",
        "fwidthFine", "dFdxCoarse", "dFdyCoarse", "fwidthCoarse"
    };
    if (OneOf(name, texture_names))
        ++summary.texture_calls;
    if (OneOf(name, derivative_names))
        ++summary.derivative_calls;
    if (name == "if" || name == "switch")
        ++summary.conditional_statements;
    if (name == "for" || name == "while")
        ++summary.loop_heads;
    if (name == "EmitVertex" || name == "EmitStreamVertex")
        ++summary.emit_vertex_calls;
    if (name == "EndPrimitive" || name == "EndStreamPrimitive")
        ++summary.end_primitive_calls;
}

void AppendDeclaration(GlslInterfaceDeclaration &declaration,
                       std::string_view token)
{
    if (!declaration.text.empty() &&
        declaration.text.size() < kGlslSummaryMaxDeclarationBytes)
        declaration.text += ' ';
    const size_t room =
        kGlslSummaryMaxDeclarationBytes - declaration.text.size();
    declaration.text.append(token.data(), std::min(room, token.size()));
    declaration.truncated |= token.size() > room;
}

} // namespace

GlslSourceSummary SummarizeGlslSource(std::string_view source)
{
    GlslSourceSummary summary;
    summary.bytes = source.size();
    if (source.size() > kGlslSummaryMaxSourceBytes) {
        summary.status = GlslSourceSummaryStatus::TooLarge;
        return summary;
    }
    summary.lines = std::count(source.begin(), source.end(), '\n');
    if (!source.empty() && source.back() != '\n')
        ++summary.lines;
    if (source.find('\0') != std::string_view::npos) {
        summary.status = GlslSourceSummaryStatus::Malformed;
        return summary;
    }

    Lexer lexer(source);
    std::string_view previous;
    int braces = 0, parentheses = 0;
    bool unbalanced = false;
    GlslInterfaceDeclaration declaration;
    for (auto token = lexer.Next(); !token.empty(); token = lexer.Next()) {
        if (token == "(")
            CountCall(summary, previous);
        if (token == ";" && previous == "discard")
            ++summary.discard_statements;
        if (token == "do")
            ++summary.loop_heads;

        if (!braces || !declaration.storage.empty())
            AppendDeclaration(declaration, token);
        if (!braces && !parentheses && declaration.storage.empty() &&
            (token == "in" || token == "out" || token == "uniform"))
            declaration.storage = std::string(token);

        if (token == "{") {
            ++braces;
            if (declaration.storage.empty())
                declaration = {};
        } else if (token == "}") {
            if (!braces)
                unbalanced = true;
            else
                --braces;
            if (!braces && declaration.storage.empty())
                declaration = {};
        } else if (token == "(") {
            ++parentheses;
        } else if (token == ")") {
            if (!parentheses)
                unbalanced = true;
            else
                --parentheses;
        } else if (token == ";" && !braces && !parentheses) {
            if (!declaration.storage.empty()) {
                ++summary.interface_declarations;
                if (summary.interfaces.size() < kGlslSummaryMaxInterfaces)
                    summary.interfaces.push_back(std::move(declaration));
                else
                    summary.interfaces_truncated = true;
            }
            declaration = {};
        }
        previous = token;
    }
    if (lexer.Malformed() || braces || parentheses || unbalanced)
        summary.status = GlslSourceSummaryStatus::Malformed;
    return summary;
}

std::shared_ptr<const GlslSourceSummary>
GlslSourceSummaryCache::Get(std::string_view source)
{
    for (const auto &entry : entries_)
        if (entry.source == source)
            return entry.summary;
    auto summary =
        std::make_shared<const GlslSourceSummary>(SummarizeGlslSource(source));
    if (source.size() <= kGlslSummaryMaxSourceBytes) {
        if (entries_.size() == 4)
            entries_.erase(entries_.begin());
        entries_.push_back({ std::string(source), summary });
    }
    return summary;
}
} // namespace xemu::shader_browser
