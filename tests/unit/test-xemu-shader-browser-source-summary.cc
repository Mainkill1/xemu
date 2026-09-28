// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-source-summary.hh"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace xemu::shader_browser;

static void Check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "source summary: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main()
{
    // Counting substrings, or lexing comments/macros as code, breaks this test.
    const std::string source = R"GLSL(#version 450
#define FAKE texture(t, p); if (x) discard; \
    dFdx(p); for (;;) {}
// texture(t,p); if (x) discard; EmitVertex();
/* uniform sampler2D fake; while (x) EndPrimitive(); */
layout(location = 0) in vec3 position;
flat out vec4 color;
uniform sampler2D image;
uniform Data { vec4 constant; } data;
void helper(in vec3 parameter, out vec4 result) { result = vec4(parameter, 1); }
void main() {
    vec4 textureName = texture(image, vec2(0));
    vec4 v = texelFetch(image, ivec2(0), 0);
    if (v.x > 0) { discard; } else if (v.y > 0) { v.x = dFdx(v.y); }
    switch (int(v.x)) { case 0: break; }
    for (int i = 0; i < 2; ++i) { v += textureLod(image, vec2(i), 0); }
    while (v.x > 4) { v.x -= fwidthFine(v.y); }
    do { v.y += dFdyCoarse(v.x); } while (v.y < 0);
    EmitVertex(); EndPrimitive();
    "texture(image,p); if (x) discard;";
    'd';
}
)GLSL";
    const auto summary = SummarizeGlslSource(source);
    Check(summary.status == GlslSourceSummaryStatus::Complete,
          "valid source was not summarized");
    Check(summary.bytes == source.size() && summary.lines == 22,
          "physical byte/line counts are wrong");
    Check(summary.texture_calls == 3,
          "fetch calls include non-code or substrings");
    Check(summary.conditional_statements == 3, "if/switch heads are wrong");
    Check(summary.loop_heads == 4, "for/while/do heads are wrong");
    Check(summary.derivative_calls == 3, "derivative call count is wrong");
    Check(summary.discard_statements == 1,
          "discard count includes comments/strings");
    Check(summary.emit_vertex_calls == 1 && summary.end_primitive_calls == 1,
          "geometry output calls are wrong");
    Check(summary.interface_declarations == 4 && summary.interfaces.size() == 4,
          "parameters or block members counted as global interfaces");
    Check(summary.interfaces[0].storage == "in" &&
              summary.interfaces[0].text.find("position") !=
                  std::string::npos &&
              summary.interfaces[0].text.find("location") != std::string::npos,
          "input layout declaration was lost");
    Check(summary.interfaces[3].storage == "uniform" &&
              summary.interfaces[3].text.find("constant") != std::string::npos,
          "uniform block declaration was lost");

    Check(SummarizeGlslSource("").lines == 0,
          "empty source has a physical line");
    Check(SummarizeGlslSource("a\n").lines == 1,
          "terminal newline adds a line");
    Check(SummarizeGlslSource("a\nb").lines == 2,
          "last unterminated line is missing");
    Check(SummarizeGlslSource("texture/* gap */(x); textureCount(x);")
                  .texture_calls == 1,
          "identifier call boundaries are wrong");
    Check(SummarizeGlslSource("/*\n*/ #define X texture(x)\ntexture(x);")
                  .texture_calls == 1,
          "comment before preprocessor directive was treated as code");
    Check(SummarizeGlslSource("/* unfinished").status ==
              GlslSourceSummaryStatus::Malformed,
          "unterminated comment silently accepted");
    Check(SummarizeGlslSource("\"unfinished").status ==
              GlslSourceSummaryStatus::Malformed,
          "unterminated string silently accepted");
    constexpr char nul_source[] = "in vec3 a;\0texture(x);";
    Check(SummarizeGlslSource(
              std::string_view(nul_source, sizeof(nul_source) - 1))
                  .status == GlslSourceSummaryStatus::Malformed,
          "embedded NUL silently accepted");

    // Large declarations and huge sources must not bypass the UI memory bound.
    std::string declarations;
    for (size_t i = 0; i < kGlslSummaryMaxInterfaces + 3; ++i)
        declarations += "uniform float u" + std::to_string(i) + ";\n";
    const auto bounded = SummarizeGlslSource(declarations);
    Check(bounded.interfaces.size() == 256 &&
              bounded.interface_declarations == 259 &&
              bounded.interfaces_truncated,
          "global interface list is not bounded with honest total coverage");
    const auto long_name =
        SummarizeGlslSource("uniform float " + std::string(8192, 'a') + ";");
    Check(long_name.interfaces.size() == 1 &&
              long_name.interfaces[0].truncated &&
              long_name.interfaces[0].text.size() <=
                  kGlslSummaryMaxDeclarationBytes,
          "a single global declaration is unbounded");
    Check(SummarizeGlslSource(std::string(kGlslSummaryMaxSourceBytes, ' '))
                  .status == GlslSourceSummaryStatus::Complete,
          "source at the byte limit was rejected");
    const auto oversized =
        SummarizeGlslSource(std::string(kGlslSummaryMaxSourceBytes + 1, ' '));
    Check(oversized.status == GlslSourceSummaryStatus::TooLarge &&
              oversized.bytes == kGlslSummaryMaxSourceBytes + 1 &&
              oversized.lines == 0 && oversized.interfaces.empty(),
          "oversized source was scanned or reported as complete");

    // Equal length source edits and alternating sources must not reuse stale
    // counts.
    GlslSourceSummaryCache cache;
    const auto first = cache.Get("texture(x);");
    const auto second = cache.Get("dFdx(xx);  ");
    Check(first->texture_calls == 1 && second->derivative_calls == 1 &&
              second->texture_calls == 0,
          "equal length edit reused a stale summary");
    Check(cache.Get("texture(x);") == first &&
              cache.Get("dFdx(xx);  ") == second,
          "unchanged alternating sources are re-lexed");
    Check(cache.Get("texture(y);") != first,
          "cache identity did not include exact source bytes");
    for (size_t i = 0; i < 12; ++i)
        cache.Get("uniform float u" + std::to_string(i) + ";");
    Check(first->texture_calls == 1, "eviction invalidated an owned summary");
    std::cout << "shader browser source summary tests passed\n";
}
