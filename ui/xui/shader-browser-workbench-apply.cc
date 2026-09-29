// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-workbench-apply.hh"
#include "shader-browser-override-store.hh"
#include "shader-browser-replacement-library.hh"

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>

namespace xemu::shader_browser {
namespace {

bool Fail(std::string *error, const char *reason)
{
    if (error) *error = reason;
    return false;
}

bool IsZeroDigest(const PreviewDigest &digest)
{
    return std::all_of(digest.begin(), digest.end(),
                       [](uint8_t byte) { return byte == 0; });
}

bool WriteBytes(const std::filesystem::path &path, const std::string &bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file && file.write(bytes.data(), bytes.size()) && file.flush();
}

std::string Hex(const uint8_t *bytes, size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 15]);
    }
    return out;
}

} // namespace

bool EvaluateDraftGameApply(const DraftGameApplyRequest &request,
                            const Entry &entry, const OverrideContext &context,
                            std::string *reason)
{
    if (request.key != entry.key) {
        return Fail(reason, "Selected shader changed since draft creation");
    }
    if (request.key.stage != Stage::Pixel ||
        request.source_stage != HostSourceStage::Fragment) {
        return Fail(reason, "Stage 3 game Apply supports pixel fragment drafts only");
    }
    if (request.interface_abi != 1) {
        return Fail(reason, "Stage 3 game Apply requires interface ABI 1");
    }
    if (request.backend == OverrideBackend::Unknown ||
        request.backend != context.backend) {
        return Fail(reason, "Draft backend differs from active renderer");
    }
    if (!request.scope.title_id ||
        !request.scope.executable_fingerprint_version ||
        request.scope.title_id != context.title_id ||
        request.scope.executable_fingerprint_version !=
            context.executable_fingerprint_version ||
        request.scope.executable_fingerprint !=
            context.executable_fingerprint ||
        std::find(entry.scopes.begin(), entry.scopes.end(), request.scope) ==
            entry.scopes.end()) {
        return Fail(reason, "Draft title/build scope differs from the active shader");
    }
    if (!request.draft_id || !request.draft_revision ||
        request.attempted_revision != request.draft_revision ||
        request.successful_revision != request.draft_revision ||
        !request.preview_submission_id ||
        request.preview_submission_id != request.successful_submission_id ||
        !request.compiled_success) {
        return Fail(reason, "Current draft revision has no successful preview compile");
    }
    if (request.source.empty() ||
        request.source.size() > kMaxReplacementSourceBytes ||
        request.source.find('\0') != std::string::npos ||
        IsZeroDigest(request.successful_source_digest) ||
        request.successful_source_digest != ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(request.source.data()),
            request.source.size())) {
        return Fail(reason, "Draft source differs from the successful preview compile");
    }
    ReplacementDescriptor descriptor{};
    descriptor.id = 1;
    descriptor.content_revision = 1;
    descriptor.stage = Stage::Pixel;
    descriptor.interface_version = request.interface_abi;
    descriptor.backend_mask = BackendMaskFor(request.backend);
    if (!IsReplacementCompatible(request.key, descriptor, request.backend,
                                 reason)) {
        return false;
    }
    return true;
}

bool ApplyWorkbenchDraft(const DraftGameApplyRequest &request,
                         const Entry &entry, OverrideStore *store,
                         ReplacementLibrary *library,
                         const std::filesystem::path &config_directory,
                         WorkbenchAppliedRule *applied, std::string *error)
{
    if (!store || !library || !applied || config_directory.empty()) {
        return Fail(error, "Workbench game Apply services or path unavailable");
    }
    OverrideStoreSnapshot before{};
    store->CopySnapshot(&before);
    if (!EvaluateDraftGameApply(request, entry, before.context, error)) {
        return false;
    }
    const std::string root_text = library->RootPath();
    const auto expected_root = config_directory / "shader-replacements";
    if (root_text.empty() ||
        std::filesystem::u8path(root_text).lexically_normal() !=
            expected_root.lexically_normal() ||
        !library->EnsureRoot(error)) {
        if (error && error->empty()) {
            *error = "Replacement library is not configured for this profile";
        }
        return false;
    }

    std::string identity = PortableShaderHash(request.key.hash) + "|" +
        std::to_string(static_cast<unsigned>(request.key.stage)) + "|" +
        std::to_string(request.scope.title_id) + "|" +
        std::to_string(request.scope.executable_fingerprint_version) + "|" +
        Hex(request.scope.executable_fingerprint.data(),
            request.scope.executable_fingerprint.size()) + "|" +
        std::to_string(static_cast<unsigned>(request.backend)) + "|" +
        std::to_string(request.draft_id) + "|" +
        std::to_string(request.draft_revision) + "|" +
        std::to_string(request.successful_submission_id) + "|" +
        Hex(request.successful_source_digest.data(),
            request.successful_source_digest.size());
    XXH128_hash_t hash = XXH3_128bits(identity.data(), identity.size());
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, hash);
    const std::string logical_id = "workbench-" +
        Hex(canonical.digest, sizeof(canonical.digest));
    const auto root = std::filesystem::u8path(root_text);
    const auto target = root / logical_id;
    const std::string manifest =
        "[replacement]\n"
        "id=" + logical_id + "\n"
        "name=Workbench draft " + std::to_string(request.draft_id) +
        " revision " + std::to_string(request.draft_revision) + "\n"
        "stage=pixel\n"
        "interface_version=1\n"
        "entry_point=main\n" +
        (request.backend == OverrideBackend::OpenGL ?
            "opengl=source.glsl\n" : "vulkan=source.glsl\n");
    std::error_code ec;
    bool created = false;
    if (!std::filesystem::exists(target, ec)) {
        static std::atomic<uint64_t> nonce{1};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto temporary = root /
            (".workbench-" + std::to_string(stamp) + "-" +
             std::to_string(nonce.fetch_add(1)));
        if (!std::filesystem::create_directory(temporary, ec) ||
            !WriteBytes(temporary / "source.glsl", request.source) ||
            !WriteBytes(temporary / "manifest.ini", manifest)) {
            std::filesystem::remove_all(temporary, ec);
            return Fail(error, "Unable to write immutable workbench package");
        }
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            std::filesystem::remove_all(temporary, ec);
            return Fail(error, "Unable to publish immutable workbench package");
        }
        created = true;
    } else if (ec) {
        return Fail(error, "Unable to inspect workbench package path");
    }

    auto rollback = [&]() {
        if (created) {
            std::filesystem::remove_all(target, ec);
            std::string ignored;
            library->Reload(store, &ignored);
        }
    };
    // Reload can report an unrelated user package error while still admitting
    // this immutable package. Verify this package by identity and bytes below.
    std::string reload_error;
    library->Reload(store, &reload_error);
    ReplacementLibrarySnapshot packages{};
    library->CopySnapshot(&packages);
    const auto found = std::find_if(packages.packages.begin(),
                                    packages.packages.end(),
        [&](const ReplacementPackageInfo &info) {
            return info.logical_id == logical_id;
        });
    if (found == packages.packages.end()) {
        rollback();
        if (error) *error = reload_error.empty() ?
            "Workbench package was not admitted by Stage 3" : reload_error;
        return false;
    }
    const auto payload = store->AcquireReplacement(
        found->descriptor.id, found->descriptor.content_revision,
        request.backend);
    if (!payload ||
        (request.backend == OverrideBackend::OpenGL ?
            payload->opengl_source : payload->vulkan_source) != request.source) {
        rollback();
        return Fail(error, "Existing workbench package has different source bytes");
    }
    if (!IsReplacementCompatible(request.key, found->descriptor,
                                 request.backend, error)) {
        rollback();
        return false;
    }

    OverrideStoreSnapshot current{};
    store->CopySnapshot(&current);
    if (!EvaluateDraftGameApply(request, entry, current.context, error)) {
        rollback();
        return false;
    }
    OverrideRule rule{};
    rule.enabled = true;
    rule.title_id = request.scope.title_id;
    rule.shader = request.key;
    rule.restrict_build = true;
    rule.executable_fingerprint_version =
        request.scope.executable_fingerprint_version;
    rule.executable_fingerprint = request.scope.executable_fingerprint;
    rule.origin = OverrideOrigin::Session;
    rule.priority = 1000;
    rule.action = OverrideAction::Replacement;
    rule.replacement_id = found->descriptor.id;
    rule.revision = request.draft_revision;
    static std::atomic<uint64_t> rule_nonce{1};
    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        uint64_t counter = rule_nonce.fetch_add(1);
        rule.id = XXH3_64bits_withSeed(logical_id.data(), logical_id.size(),
                                      counter);
        if (!rule.id) continue;
        const auto existing = std::find_if(current.rules.begin(),
            current.rules.end(), [&](const OverrideRule &candidate) {
                return candidate.id == rule.id;
            });
        if (existing == current.rules.end()) break;
        rule.id = 0;
    }
    if (!rule.id || !store->UpsertRule(rule, error)) {
        rollback();
        if (rule.id == 0) {
            return Fail(error, "Unable to allocate a workbench rule ID");
        }
        return false;
    }
    *applied = {rule.id, request.key, request.scope, rule.replacement_id};
    return true;
}

bool RestoreWorkbenchDraft(const WorkbenchAppliedRule &applied,
                           OverrideStore *store, std::string *error)
{
    if (!store || !applied.id) {
        return Fail(error, "Workbench rule is unavailable for Restore");
    }
    OverrideStoreSnapshot snapshot{};
    store->CopySnapshot(&snapshot);
    const auto found = std::find_if(snapshot.rules.begin(),
        snapshot.rules.end(), [&](const OverrideRule &rule) {
            return rule.id == applied.id;
        });
    if (found == snapshot.rules.end() ||
        found->origin != OverrideOrigin::Session ||
        found->action != OverrideAction::Replacement ||
        !found->restrict_build || found->shader != applied.key ||
        found->title_id != applied.scope.title_id ||
        found->executable_fingerprint_version !=
            applied.scope.executable_fingerprint_version ||
        found->executable_fingerprint !=
            applied.scope.executable_fingerprint ||
        found->replacement_id != applied.replacement_id) {
        return Fail(error, "Workbench rule no longer matches its Apply token");
    }
    if (!store->RemoveRule(applied.id)) {
        return Fail(error, "Unable to restore workbench game rule");
    }
    return true;
}

} // namespace xemu::shader_browser
