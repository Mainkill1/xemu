#include "../../ui/xui/shader-browser-artifact-store.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace xemu::shader_browser;

int main()
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 "xemu-shader-artifact-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    ShaderArtifactStore store;
    ArtifactStoreConfig config{};
    config.base_path = root.string();
    config.enabled = false;
    assert(store.Configure(config));

    ArtifactWrite write{};
    write.title_id = 0x4d530064;
    write.key.hash.version = 1;
    write.key.stage = Stage::Pixel;
    for (size_t i = 0; i < write.key.hash.bytes.size(); ++i) {
        write.key.hash.bytes[i] = static_cast<uint8_t>(i + 1);
    }
    write.backend = "vulkan";
    write.route = "specialized";
    write.kind = "spirv";
    write.extension = "spv";
    write.generator_abi = 1;
    write.bytes.assign(2 * 1024 * 1024, 0x5a);

    ArtifactMetadata metadata{};
    std::string error;
    assert(!store.Write(write, &metadata, &error));
    assert(!std::filesystem::exists(root / "shader-artifacts"));

    config.enabled = true;
    assert(store.Configure(config));
    assert(store.Write(write, &metadata, &error));
    assert(metadata.byte_size == write.bytes.size());
    assert(!metadata.relative_path.empty());
    std::filesystem::path written = root / "shader-artifacts" /
                                    metadata.relative_path;
    assert(std::filesystem::exists(written));
    assert(std::filesystem::file_size(written) == write.bytes.size());

    // Paths are human-browsable and title scoped.
    assert(metadata.relative_path.find("4D530064") != std::string::npos);
    assert(metadata.relative_path.find("ps") != std::string::npos);

    // Different generated variants of the same guest shader keep distinct
    // files when their content digests are supplied.
    std::string first_path = metadata.relative_path;
    write.content_hash = std::string(64, 'a');
    assert(store.Write(write, &metadata, &error));
    assert(metadata.relative_path != first_path);
    assert(std::filesystem::exists(root / "shader-artifacts" / first_path));
    assert(std::filesystem::exists(root / "shader-artifacts" /
                                   metadata.relative_path));

    std::filesystem::remove_all(root, ec);
    std::cout << "shader artifact store tests passed\n";
    return 0;
}
