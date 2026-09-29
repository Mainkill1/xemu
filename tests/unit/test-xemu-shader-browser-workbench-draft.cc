#include "../../ui/xui/shader-browser-workbench-draft.hh"

#include <cassert>
#include <iostream>

using namespace xemu::shader_browser;

int main()
{
    GeneratedSourceSnapshot source{};
    source.scope.title_id = 0x4d530064;
    source.scope.executable_fingerprint_version = 1;
    source.scope.executable_fingerprint[0] = 7;
    source.key.stage = Stage::Pixel;
    source.key.hash.version = 1;
    source.backend = PreviewBackend::OpenGL;
    source.stage = HostSourceStage::Fragment;
    source.route = Route::Specialized;
    source.generator_abi = 1;
    source.interface_abi = 1;
    source.text = "#version 400\nvoid main(){}\n";
    source.digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(source.text.data()), source.text.size());
    std::string error;
    WorkbenchDraft draft;
    assert(draft.Create(source, 42, &error));
    assert(draft.StatusLabel() == "Draft r1 uncompiled");
    assert(draft.Base().text == source.text);
    assert(draft.Revision() == 1);
    assert(draft.Edit("#version 400\nvoid main(){ }\n", 100));
    assert(draft.Revision() == 2);
    assert(draft.Base().text == source.text);
    assert(!draft.FreezeCompile(100 + kWorkbenchDraftDebounceNs - 1,
                                false).has_value());
    auto compile = draft.FreezeCompile(100 + kWorkbenchDraftDebounceNs, false);
    assert(compile.has_value() && compile->revision == 2);
    assert(draft.StatusLabel() == "Draft r2 requested");
    assert(compile->source == "#version 400\nvoid main(){ }\n");
    assert(!draft.FreezeCompile(100 + kWorkbenchDraftDebounceNs + 1,
                                false).has_value());
    FrozenDraftCompile wrong = *compile;
    wrong.digest[0] ^= 1;
    assert(!draft.MarkCompiled(wrong, true, "forged"));
    assert(draft.MarkCompiled(*compile, true, ""));
    assert(draft.StatusLabel() == "Compiled draft r2");
    assert(draft.LastSuccessfulRevision() == 2);
    assert(draft.LastSuccessfulCompile()->source == compile->source);
    assert(draft.LastSuccessfulCompile()->digest == compile->digest);
    assert(draft.Edit("invalid", 1000));
    auto manual = draft.FreezeCompile(1001, true);
    assert(manual.has_value() && manual->revision == 3);
    assert(draft.MarkCompiled(*manual, false, "0:2: error"));
    assert(draft.LastSuccessfulRevision() == 2);
    assert(draft.AttemptedRevision() == 3);
    assert(draft.StatusLabel() == "Draft r3 failed; last compiled r2");
    auto retry = draft.FreezeCompile(1002, true);
    assert(retry.has_value() && retry->revision == manual->revision);
    assert(retry->source == manual->source && retry->digest == manual->digest);
    assert(retry->submission_id != manual->submission_id);
    assert(draft.StatusLabel() == "Draft r3 requested; last compiled r2");
    assert(!draft.MarkCompiled(*manual, true, "late first attempt"));
    assert(draft.MarkCompiled(*retry, false, "transient backend failure"));
    assert(draft.StatusLabel() == "Draft r3 failed; last compiled r2");
    assert(!draft.FreezeCompile(1003, false).has_value());
    auto recovered = draft.FreezeCompile(1003, true);
    assert(recovered.has_value());
    assert(recovered->submission_id != retry->submission_id);
    assert(!draft.MarkCompiled(*retry, true, "late retry"));
    assert(draft.MarkCompiled(*recovered, true, ""));
    assert(draft.StatusLabel() == "Compiled draft r3");
    assert(draft.LastSuccessfulCompile()->submission_id ==
           recovered->submission_id);
    assert(draft.LastSuccessfulRevision() == 3);
    assert(draft.Edit("fixed", 2000));
    assert(draft.StatusLabel() == "Draft r4 uncompiled; last compiled r3");
    auto latest = draft.FreezeCompile(2001, true);
    assert(latest.has_value() && latest->revision == 4);
    assert(draft.StatusLabel() == "Draft r4 requested; last compiled r3");
    assert(!draft.MarkCompiled(*manual, true, "stale"));
    assert(draft.LastSuccessfulRevision() == 3);
    assert(draft.Edit("newer uncompiled text", 2002));
    assert(draft.MarkCompiled(*latest, true, ""));
    assert(draft.LastSuccessfulRevision() == 4);
    assert(draft.StatusLabel() == "Draft r5 uncompiled; last compiled r4");
    assert(draft.LastSuccessfulCompile()->source == latest->source);
    assert(draft.LastSuccessfulCompile()->digest == latest->digest);
    assert(draft.Text() != draft.LastSuccessfulCompile()->source);
    assert(!draft.MarkCompiled(*latest, false, "duplicate"));
    std::cout << "workbench draft tests passed\n";
}
