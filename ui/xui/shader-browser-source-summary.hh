// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xemu::shader_browser {

constexpr size_t kGlslSummaryMaxSourceBytes = 4U * 1024U * 1024U;
constexpr size_t kGlslSummaryMaxInterfaces = 256;
constexpr size_t kGlslSummaryMaxDeclarationBytes = 512;

enum class GlslSourceSummaryStatus : uint8_t { Complete, TooLarge, Malformed };
struct GlslInterfaceDeclaration {
    std::string storage, text;
    bool truncated = false;
};
// Lexical source structure, not compiler instructions or executed operations.
// Comments, strings and preprocessor directive bodies are excluded. Macro
// expansion, conditional compilation and data-dependent execution are unknown.
struct GlslSourceSummary {
    GlslSourceSummaryStatus status = GlslSourceSummaryStatus::Complete;
    size_t bytes = 0, lines = 0;
    uint32_t texture_calls = 0, conditional_statements = 0, loop_heads = 0;
    uint32_t derivative_calls = 0, discard_statements = 0;
    uint32_t emit_vertex_calls = 0, end_primitive_calls = 0;
    uint32_t interface_declarations = 0;
    bool interfaces_truncated = false;
    std::vector<GlslInterfaceDeclaration> interfaces;
};
GlslSourceSummary SummarizeGlslSource(std::string_view source);

// Exact byte equality is the cache identity. Four bounded sources are retained
// so switching between Details panes does not run the lexer every frame.
class GlslSourceSummaryCache {
public:
    std::shared_ptr<const GlslSourceSummary> Get(std::string_view source);

private:
    struct Entry {
        std::string source;
        std::shared_ptr<const GlslSourceSummary> summary;
    };
    std::vector<Entry> entries_;
};

} // namespace xemu::shader_browser
