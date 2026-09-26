#include "../../ui/xui/shader-browser-replacement-library.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace xemu::shader_browser;
namespace fs = std::filesystem;

static void Write(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    assert(file.good());
}

static std::string Manifest(const std::string &gl, const std::string &vk)
{
    return "[replacement]\n"
           "id=water-fix\n"
           "name=Water Fix\n"
           "stage=pixel\n"
           "interface_version=1\n"
           "entry_point=main\n"
           "opengl=" + gl + "\n"
           "vulkan=" + vk + "\n";
}

int main()
{
    fs::path root = fs::temp_directory_path() /
        "xemu-stage3-replacement-library-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "shader-replacements" / "water-fix");

    fs::path package = root / "shader-replacements" / "water-fix";
    Write(package / "manifest.ini", Manifest("gl.frag.glsl", "vk.frag.glsl"));
    Write(package / "gl.frag.glsl", "#version 450\nvoid main(){}\n");
    Write(package / "vk.frag.glsl", "#version 450\nvoid main(){}\n");

    OverrideStore store;
    ReplacementLibrary library;
    std::string error;
    library.Configure("");
    assert(library.RootPath().empty());
    assert(!library.EnsureRoot(&error));
    library.Configure(root.u8string());
    assert(library.Reload(&store, &error));

    ReplacementLibrarySnapshot snapshot;
    library.CopySnapshot(&snapshot);
    assert(snapshot.packages.size() == 1);
    assert(snapshot.errors.empty());
    assert(snapshot.packages[0].logical_id == "water-fix");
    assert(snapshot.packages[0].descriptor.stage == Stage::Pixel);
    assert(snapshot.packages[0].descriptor.backend_mask ==
           (BackendOpenGL | BackendVulkan));

    const uint64_t id = snapshot.packages[0].descriptor.id;
    const uint64_t revision = snapshot.packages[0].descriptor.content_revision;
    assert(id != 0 && revision != 0);
    assert(store.AcquireReplacement(id, revision, OverrideBackend::OpenGL));

    // Editing contents preserves the logical ID while changing revision.
    Write(package / "gl.frag.glsl",
          "#version 450\nvoid main(){ /* changed */ }\n");
    assert(library.Reload(&store, &error));
    library.CopySnapshot(&snapshot);
    assert(snapshot.packages[0].descriptor.id == id);
    assert(snapshot.packages[0].descriptor.content_revision != revision);

    // A bad reload keeps the last valid payload available and reports error.
    Write(package / "manifest.ini", Manifest("../escape.glsl", "vk.frag.glsl"));
    assert(!library.Reload(&store, &error));
    library.CopySnapshot(&snapshot);
    assert(snapshot.packages.size() == 1);
    assert(!snapshot.errors.empty());
    assert(store.AcquireReplacement(
        id, snapshot.packages[0].descriptor.content_revision,
        OverrideBackend::OpenGL));

    // Removing the package removes the library-owned payload on the next scan.
    fs::remove_all(package, ec);
    assert(library.Reload(&store, &error));
    library.CopySnapshot(&snapshot);
    assert(snapshot.packages.empty());
    assert(!store.AcquireReplacement(id, revision, OverrideBackend::OpenGL));

    fs::remove_all(root, ec);
    std::cout << "shader browser replacement library tests passed\n";
    return 0;
}
