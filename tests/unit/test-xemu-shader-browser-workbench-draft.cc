#include "../../ui/xui/shader-browser-workbench-draft.hh"

#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace xemu::shader_browser;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #condition);                         \
            std::abort();                                               \
        }                                                               \
    } while (false)

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
    CHECK(draft.Create(source, 42, &error));
    CHECK(draft.StatusLabel() == "Draft r1 uncompiled");
    // Same generated source can back every occurrence of a shared shader.
    for (unsigned occurrence = 0; occurrence < 32; ++occurrence) {
        auto shared_source = source;
        shared_source.resident = occurrence % 2;
        shared_source.build_scope_verified = occurrence % 3;
        CHECK(draft.MatchesSource(shared_source));
    }
    auto other_archive = source;
    other_archive.text += "// another captured generator revision\n";
    other_archive.digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(other_archive.text.data()),
        other_archive.text.size());
    CHECK(!draft.MatchesSource(other_archive));
    other_archive = source;
    ++other_archive.generator_abi;
    CHECK(!draft.MatchesSource(other_archive));
    other_archive = source;
    ++other_archive.interface_abi;
    CHECK(!draft.MatchesSource(other_archive));
    other_archive = source;
    other_archive.stage = HostSourceStage::Vertex;
    CHECK(!draft.MatchesSource(other_archive));
    other_archive = source;
    other_archive.key.hash.bytes[0] ^= 1;
    CHECK(!draft.MatchesSource(other_archive));
    other_archive = source;
    other_archive.scope.executable_fingerprint[0] ^= 1;
    CHECK(!draft.MatchesSource(other_archive));
    other_archive = source;
    other_archive.backend = PreviewBackend::Vulkan;
    CHECK(!draft.MatchesSource(other_archive));
    other_archive = source;
    other_archive.route = Route::Uber;
    CHECK(!draft.MatchesSource(other_archive));
    CHECK(draft.Base().text == source.text);
    CHECK(draft.Revision() == 1);
    CHECK(draft.Edit("#version 400\nvoid main(){ }\n", 100));
    CHECK(draft.MatchesSource(source));
    CHECK(draft.Revision() == 2);
    CHECK(draft.Base().text == source.text);
    CHECK(!draft.FreezeCompile(100 + kWorkbenchDraftDebounceNs - 1, false)
               .has_value());
    auto compile = draft.FreezeCompile(100 + kWorkbenchDraftDebounceNs, false);
    CHECK(compile.has_value() && compile->revision == 2);
    CHECK(draft.StatusLabel() == "Draft r2 requested");
    CHECK(compile->source == "#version 400\nvoid main(){ }\n");
    CHECK(!draft.FreezeCompile(100 + kWorkbenchDraftDebounceNs + 1, false)
               .has_value());
    FrozenDraftCompile wrong = *compile;
    wrong.digest[0] ^= 1;
    CHECK(!draft.MarkCompiled(wrong, true, "forged"));
    CHECK(draft.MarkCompiled(*compile, true, ""));
    CHECK(draft.StatusLabel() == "Compiled draft r2");
    CHECK(draft.LastSuccessfulRevision() == 2);
    CHECK(draft.LastSuccessfulCompile()->source == compile->source);
    CHECK(draft.LastSuccessfulCompile()->digest == compile->digest);
    CHECK(draft.Edit("invalid", 1000));
    auto manual = draft.FreezeCompile(1001, true);
    CHECK(manual.has_value() && manual->revision == 3);
    CHECK(draft.MarkCompiled(*manual, false, "0:2: error"));
    CHECK(draft.LastSuccessfulRevision() == 2);
    CHECK(draft.AttemptedRevision() == 3);
    CHECK(draft.StatusLabel() == "Draft r3 failed; last compiled r2");
    auto retry = draft.FreezeCompile(1002, true);
    CHECK(retry.has_value() && retry->revision == manual->revision);
    CHECK(retry->source == manual->source && retry->digest == manual->digest);
    CHECK(retry->submission_id != manual->submission_id);
    CHECK(draft.StatusLabel() == "Draft r3 requested; last compiled r2");
    CHECK(!draft.MarkCompiled(*manual, true, "late first attempt"));
    CHECK(draft.MarkCompiled(*retry, false, "transient backend failure"));
    CHECK(draft.StatusLabel() == "Draft r3 failed; last compiled r2");
    CHECK(!draft.FreezeCompile(1003, false).has_value());
    auto recovered = draft.FreezeCompile(1003, true);
    CHECK(recovered.has_value());
    CHECK(recovered->submission_id != retry->submission_id);
    CHECK(!draft.MarkCompiled(*retry, true, "late retry"));
    CHECK(draft.MarkCompiled(*recovered, true, ""));
    CHECK(draft.StatusLabel() == "Compiled draft r3");
    CHECK(draft.LastSuccessfulCompile()->submission_id ==
          recovered->submission_id);
    CHECK(draft.LastSuccessfulRevision() == 3);
    CHECK(draft.Edit("fixed", 2000));
    CHECK(draft.StatusLabel() == "Draft r4 uncompiled; last compiled r3");
    auto latest = draft.FreezeCompile(2001, true);
    CHECK(latest.has_value() && latest->revision == 4);
    CHECK(draft.StatusLabel() == "Draft r4 requested; last compiled r3");
    CHECK(!draft.MarkCompiled(*manual, true, "stale"));
    CHECK(draft.LastSuccessfulRevision() == 3);
    CHECK(draft.Edit("newer uncompiled text", 2002));
    CHECK(draft.MarkCompiled(*latest, true, ""));
    CHECK(draft.LastSuccessfulRevision() == 4);
    CHECK(draft.StatusLabel() == "Draft r5 uncompiled; last compiled r4");
    CHECK(draft.LastSuccessfulCompile()->source == latest->source);
    CHECK(draft.LastSuccessfulCompile()->digest == latest->digest);
    CHECK(draft.Text() != draft.LastSuccessfulCompile()->source);
    CHECK(!draft.MarkCompiled(*latest, false, "duplicate"));
    std::cout << "workbench draft tests passed\n";
}
