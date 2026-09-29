#include "../../ui/xui/shader-browser-glsl-editor.hh"

#include <cassert>
#include <iostream>
#include <string>

using namespace xemu::shader_browser;

int main()
{
    GlslEditorModel editor;
    editor.SetSource("#version 400\nvoid main() {\n  // α texture\n  float x = 1.0;\n}\n");
    assert(editor.LineCount() == 5);
    assert(editor.Line(1) == "void main() {");
    assert(editor.Line(2) == "  // α texture");
    assert(editor.Line(4) == "}");
    assert(editor.LineForOffset(editor.Source().find("float")) == 3);
    assert(editor.FindNext("texture", 0).value() ==
           editor.Source().find("texture"));
    assert(editor.FindNext("#version", editor.Source().size() - 1).value() == 0);
    assert(!editor.FindNext("missing", 0).has_value());
    auto comment = editor.TokenizeLine(2);
    assert(comment.size() == 1);
    assert(comment[0].kind == GlslTokenKind::Comment);
    auto tokens = editor.TokenizeLine(3);
    bool keyword = false;
    bool number = false;
    for (const auto &token : tokens) {
        keyword |= token.kind == GlslTokenKind::Keyword;
        number |= token.kind == GlslTokenKind::Number;
    }
    assert(keyword && number);
    assert(ParseGlslDiagnosticLine("ERROR: 0:4: unknown name").line == 4);
    assert(ParseGlslDiagnosticLine("0(17) : error C0000").line == 17);
    assert(ParseGlslDiagnosticLine("driver error").line == 0);
    editor.SetSource(std::string(4 * 1024 * 1024, 'x'));
    assert(editor.LineCount() == 1);
    assert(editor.FindNext("xxxxx", editor.Source().size() - 1).value() == 0);
    std::cout << "GLSL editor tests passed\n";
}
