#include "../../ui/xui/shader-browser-override-lifecycle.hh"
#include "../../ui/xui/shader-browser-replacement-library.hh"
#include "../../ui/xui/shader-browser-saved-rules.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace xemu::shader_browser;
namespace fs = std::filesystem;

static void Write(const fs::path &path, const char *contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << contents;
    assert(file.good());
}

int main()
{
    fs::path root =
        fs::temp_directory_path() / "xemu-stage3-override-lifecycle-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::path package = root / "shader-replacements" / "startup";
    Write(package / "manifest.ini",
          "[replacement]\nid=startup\nname=Startup replacement\n"
          "stage=pixel\ninterface_version=1\nentry_point=main\n"
          "opengl=gl.glsl\nvulkan=vk.glsl\n");
    Write(package / "gl.glsl", "#version 330 core\nvoid main(){}\n");
    Write(package / "vk.glsl", "#version 450\nvoid main(){}\n");

    OverrideStore seed_store;
    ReplacementLibrary seed_library;
    seed_library.Configure(root.u8string());
    std::string error;
    assert(seed_library.Reload(&seed_store, &error));
    ReplacementLibrarySnapshot packages;
    seed_library.CopySnapshot(&packages);
    assert(packages.packages.size() == 1);

    SavedOverrideRules seed_saved;
    assert(seed_saved.Configure(root.u8string(), true, &seed_store, &error));
    OverrideRule rule{};
    rule.id = 109;
    rule.title_id = 0x4d530064;
    rule.shader.hash.version = 1;
    rule.shader.hash.bytes[0] = 7;
    rule.shader.stage = Stage::Pixel;
    rule.action = OverrideAction::Replacement;
    rule.replacement_id = packages.packages[0].descriptor.id;
    rule.origin = OverrideOrigin::Saved;
    rule.revision = 1;
    assert(seed_saved.Save(rule, &seed_store, &error));
    seed_saved.Close();

    OverrideStore startup_store;
    ReplacementLibrary startup_library;
    SavedOverrideRules startup_saved;
    assert(LoadShaderOverrides(root.u8string(), true, &startup_store,
                               &startup_library, &startup_saved, &error));
    OverrideContext context{};
    context.title_id = rule.title_id;
    context.backend = OverrideBackend::Vulkan;
    startup_store.SetContext(context);
    OverrideResolution resolved = startup_store.Resolve(rule.shader);
    assert(resolved.status == OverrideResolutionStatus::Matched);
    assert(resolved.policy.action == OverrideAction::Replacement);
    assert(startup_store.AcquireReplacement(
        rule.replacement_id, resolved.policy.replacement_revision,
        OverrideBackend::Vulkan));

    OverrideRule force = rule;
    force.id = 110;
    force.origin = OverrideOrigin::Session;
    force.action = OverrideAction::ForceUber;
    force.replacement_id = 0;
    assert(
        ApplyShaderOverrideRule(force, &startup_store, &startup_saved, &error));
    assert(startup_store.Resolve(rule.shader).policy.action ==
           OverrideAction::ForceUber);
    force.action = OverrideAction::ForceSpecialized;
    force.revision++;
    assert(
        ApplyShaderOverrideRule(force, &startup_store, &startup_saved, &error));
    assert(startup_store.Resolve(rule.shader).policy.action ==
           OverrideAction::ForceSpecialized);
    context.backend = OverrideBackend::OpenGL;
    startup_store.SetContext(context);
    force.action = OverrideAction::ForceUber;
    assert(!ApplyShaderOverrideRule(force, &startup_store, &startup_saved,
                                    &error));
    startup_store.RemoveRule(force.id);

    assert(startup_saved.Configure(root.u8string(), false, &startup_store,
                                   &error));
    assert(startup_store.Resolve(rule.shader).status ==
           OverrideResolutionStatus::None);
    startup_saved.Close();
    fs::remove_all(root, ec);
    std::cout << "shader override lifecycle tests passed\n";
}
