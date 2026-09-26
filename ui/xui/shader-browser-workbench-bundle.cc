// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-workbench-bundle.hh"
#include "shader-browser-preview-adapter.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace xemu::shader_browser {
namespace {

using Json = nlohmann::json;
constexpr char kSchema[] = "xemu.shader-workbench-test.v1";
constexpr char kLabel[] = "Synthetic inputs — not an in-game draw";
constexpr size_t kMaxTextureBytes = 6U * 8U * 8U * 4U;

// A structural pass bounds the JSON tree before nlohmann constructs its DOM.
// The format uses a fixed number of small arrays; 4096 nodes and depth 32
// leave generous headroom without accepting pathological tiny-element trees.
class BundleShapeSax final : public Json::json_sax_t
{
public:
    bool null() override { return Item(); }
    bool boolean(bool) override { return Item(); }
    bool number_integer(number_integer_t) override { return Item(); }
    bool number_unsigned(number_unsigned_t) override { return Item(); }
    bool number_float(number_float_t value, const string_t &) override
    {
        return std::isfinite(value) && Item();
    }
    bool string(string_t &value) override
    {
        return value.size() <= kPreviewMaxSourceBytes && Item();
    }
    bool binary(binary_t &) override { return false; }
    bool start_object(std::size_t) override { return Enter(); }
    bool key(string_t &value) override
    {
        return value.size() <= 128 && Item();
    }
    bool end_object() override { --depth_; return true; }
    bool start_array(std::size_t) override { return Enter(); }
    bool end_array() override { --depth_; return true; }
    bool parse_error(std::size_t, const std::string &,
                     const Json::exception &) override { return false; }

private:
    bool Item() { return ++items_ <= 4096; }
    bool Enter()
    {
        if (++depth_ > 32) return false;
        return Item();
    }
    size_t depth_ = 0;
    size_t items_ = 0;
};

bool PublishNoReplace(const std::filesystem::path &temporary,
                      const std::filesystem::path &target,
                      bool *collision)
{
    *collision = false;
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_WRITE_THROUGH)) return true;
    DWORD result = GetLastError();
    *collision = result == ERROR_FILE_EXISTS || result == ERROR_ALREADY_EXISTS;
    return false;
#else
    if (::link(temporary.c_str(), target.c_str()) == 0) return true;
    *collision = errno == EEXIST;
    return false;
#endif
}

bool Fail(std::string *error, const char *message)
{
    if (error) *error = message;
    return false;
}

std::string Hex(const uint8_t *bytes, size_t count)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 15]);
    }
    return out;
}

bool Unhex(const std::string &text, uint8_t *out, size_t max_bytes,
           size_t *bytes)
{
    if (text.size() % 2 || text.size() / 2 > max_bytes) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < text.size() / 2; ++i) {
        int high = nibble(text[i * 2]);
        int low = nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    *bytes = text.size() / 2;
    return true;
}

std::string DigestHex(const PreviewDigest &digest)
{
    return Hex(digest.data(), digest.size());
}

PreviewDigest ParseDigest(const Json &json)
{
    PreviewDigest digest{};
    size_t count = 0;
    if (!Unhex(json.get<std::string>(), digest.data(), digest.size(), &count) ||
        count != digest.size()) {
        throw std::runtime_error("invalid digest");
    }
    return digest;
}

std::vector<uint8_t> ParseBytes(const Json &json, size_t limit)
{
    const std::string text = json.get<std::string>();
    if (text.size() / 2 > limit) throw std::runtime_error("bytes exceed limit");
    std::vector<uint8_t> bytes(text.size() / 2);
    size_t count = 0;
    if (!Unhex(text, bytes.data(), limit, &count)) {
        throw std::runtime_error("invalid hex bytes");
    }
    return bytes;
}

Json ScopeJson(const ShaderScope &scope)
{
    return {{"title_id", scope.title_id},
            {"fingerprint_version", scope.executable_fingerprint_version},
            {"fingerprint", Hex(scope.executable_fingerprint.data(),
                                scope.executable_fingerprint.size())}};
}

ShaderScope ParseScope(const Json &json)
{
    ShaderScope scope{};
    scope.title_id = json.at("title_id").get<uint32_t>();
    scope.executable_fingerprint_version =
        json.at("fingerprint_version").get<uint32_t>();
    size_t count = 0;
    if (!Unhex(json.at("fingerprint").get<std::string>(),
               scope.executable_fingerprint.data(),
               scope.executable_fingerprint.size(), &count) ||
        count != scope.executable_fingerprint.size()) {
        throw std::runtime_error("invalid executable fingerprint");
    }
    return scope;
}

Json SceneJson(const PreviewScene &scene)
{
    Json references = Json::array();
    for (const PreviewReference &reference : scene.references) {
        references.push_back({{"visible", reference.visible},
                              {"translation", reference.translation}});
    }
    return {{"version", scene.version}, {"mesh", scene.mesh},
            {"yaw", scene.yaw}, {"pitch", scene.pitch},
            {"distance", scene.distance}, {"pan", scene.pan},
            {"target_pivot", scene.target_pivot},
            {"references", std::move(references)}};
}

PreviewScene ParseScene(const Json &json)
{
    PreviewScene scene{};
    scene.version = json.at("version").get<uint32_t>();
    scene.mesh = static_cast<PreviewMesh>(json.at("mesh").get<unsigned>());
    scene.yaw = json.at("yaw").get<float>();
    scene.pitch = json.at("pitch").get<float>();
    scene.distance = json.at("distance").get<float>();
    scene.pan = json.at("pan").get<std::array<float, 2>>();
    scene.target_pivot = json.at("target_pivot").get<std::array<float, 3>>();
    const auto &references = json.at("references");
    if (!references.is_array() || references.size() != kPreviewReferenceCount) {
        throw std::runtime_error("invalid reference count");
    }
    for (size_t i = 0; i < scene.references.size(); ++i) {
        scene.references[i].visible = references[i].at("visible").get<bool>();
        scene.references[i].translation = references[i].at("translation")
            .get<std::array<float, 3>>();
    }
    return scene;
}

Json RenderJson(const PreviewRenderState &state)
{
    return {{"blend", state.blend}, {"depth_test", state.depth_test},
            {"depth_write", state.depth_write}, {"cull", state.cull},
            {"alpha_test", state.alpha_test},
            {"alpha_reference", state.alpha_reference},
            {"clear_color", state.clear_color}};
}

PreviewRenderState ParseRender(const Json &json)
{
    PreviewRenderState state{};
    state.blend = static_cast<PreviewBlendMode>(json.at("blend").get<unsigned>());
    state.depth_test = json.at("depth_test").get<bool>();
    state.depth_write = json.at("depth_write").get<bool>();
    state.cull = static_cast<PreviewCullMode>(json.at("cull").get<unsigned>());
    state.alpha_test = json.at("alpha_test").get<bool>();
    state.alpha_reference = json.at("alpha_reference").get<uint8_t>();
    state.clear_color = json.at("clear_color").get<std::array<float, 4>>();
    return state;
}

Json BindingJson(const PreviewInputBinding &binding)
{
    return {{"target", binding.target}, {"enabled", binding.enabled},
            {"base", binding.base}, {"amplitude", binding.amplitude},
            {"period_seconds", binding.period_seconds}};
}

PreviewInputBinding ParseBinding(const Json &json)
{
    PreviewInputBinding binding{};
    binding.target = static_cast<PreviewInputTarget>(
        json.at("target").get<unsigned>());
    binding.enabled = json.at("enabled").get<bool>();
    binding.base = json.at("base").get<float>();
    binding.amplitude = json.at("amplitude").get<float>();
    binding.period_seconds = json.at("period_seconds").get<float>();
    return binding;
}

Json ClockJson(const PreviewClockState &clock)
{
    return {{"playing", clock.playing}, {"loop", clock.loop},
            {"time_seconds", clock.time_seconds}, {"speed", clock.speed},
            {"loop_seconds", clock.loop_seconds}, {"frame", clock.frame},
            {"revision", clock.revision}};
}

PreviewClockState ParseClock(const Json &json)
{
    PreviewClockState clock{};
    clock.playing = json.at("playing").get<bool>();
    clock.loop = json.at("loop").get<bool>();
    clock.time_seconds = json.at("time_seconds").get<double>();
    clock.speed = json.at("speed").get<double>();
    clock.loop_seconds = json.at("loop_seconds").get<double>();
    clock.frame = json.at("frame").get<uint64_t>();
    clock.revision = json.at("revision").get<uint64_t>();
    return clock;
}

} // namespace

bool ValidateWorkbenchExperiment(const WorkbenchExperiment &experiment,
                                 std::string *error)
{
    const PreviewPacket &packet = experiment.packet;
    RecipeInspection inspected{};
    if (!InspectCanonicalRecipe(experiment.recipe, &inspected, error) ||
        packet.selection.shader != experiment.recipe.key ||
        !packet.selection.scope.title_id ||
        !packet.selection.scope.executable_fingerprint_version ||
        packet.recipe_format_version != experiment.recipe.recipe_format_version ||
        packet.recipe != experiment.recipe.bytes ||
        std::find(experiment.recipe.scopes.begin(),
                  experiment.recipe.scopes.end(), packet.selection.scope) ==
            experiment.recipe.scopes.end()) {
        return Fail(error, "Bundle recipe, shader key, and exact scope differ");
    }
    if (packet.packet_kind != PreviewPacketKind::Synthetic ||
        packet.replay_class != PreviewReplayClass::Synthetic ||
        (packet.selection.backend != PreviewBackend::OpenGL &&
         packet.selection.backend != PreviewBackend::Vulkan) ||
        packet.selection.mode > PreviewMode::Visualize ||
        packet.source_route > Route::Disabled ||
        packet.update_policy > PreviewUpdatePolicy::Continuous ||
        !ValidatePreviewPacket(packet, error) ||
        !(ClampPreviewScene(packet.scene) == packet.scene) ||
        !(ClampPreviewRenderState(packet.render_state) == packet.render_state)) {
        return Fail(error, "Bundle requires a valid bounded synthetic packet");
    }
    if (packet.partner_source.empty() || packet.fixture_bytes.empty()) {
        return Fail(error, "Bundle is missing partner source or fixture bytes");
    }
    PreviewSyntheticFixture decoded_fixture{};
    if (!DecodePreviewSyntheticFixture(packet.fixture_bytes, &decoded_fixture,
                                       error)) {
        return Fail(error, "Bundle synthetic fixture bytes are invalid");
    }
    const GeneratedSourceSnapshot &original = experiment.original;
    if (original.key != packet.selection.shader ||
        !(original.scope == packet.selection.scope) ||
        original.backend != packet.selection.backend ||
        original.stage != HostSourceStage::Fragment ||
        original.route != packet.source_route ||
        original.build_scope_verified ||
        original.generator_abi != packet.generator_abi ||
        original.interface_abi != packet.interface_abi ||
        original.text.empty() ||
        original.text.size() > kPreviewMaxSourceBytes ||
        original.text.find('\0') != std::string::npos ||
        original.digest != ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(original.text.data()),
            original.text.size())) {
        return Fail(error, "Bundle original generated source is inconsistent");
    }
    if (experiment.edited_text.size() > kPreviewMaxSourceBytes ||
        experiment.edited_text.find('\0') != std::string::npos ||
        ((experiment.edited_text.empty() && experiment.edit_revision) ||
         (!experiment.edited_text.empty() && !experiment.edit_revision))) {
        return Fail(error, "Bundle edited source or revision is invalid");
    }
    if (packet.source_variant == PreviewSourceVariant::Original) {
        if (packet.source != original.text ||
            packet.source_resident != original.resident) {
            return Fail(error, "Original packet source differs from generated source");
        }
    } else {
        if (packet.source_resident || !experiment.successful_compile ||
            experiment.successful_compile->draft_id != packet.draft_id ||
            experiment.successful_compile->revision != packet.draft_revision ||
            experiment.successful_compile->submission_id !=
                packet.draft_submission_id ||
            experiment.successful_compile->source != packet.source ||
            experiment.successful_compile->digest != packet.source_digest ||
            experiment.edit_revision < packet.draft_revision) {
            return Fail(error, "Edited packet differs from successful draft compile");
        }
    }
    if (experiment.successful_compile) {
        const FrozenDraftCompile &compiled = *experiment.successful_compile;
        if (!compiled.draft_id || !compiled.revision ||
            !compiled.submission_id ||
            compiled.source.empty() ||
            compiled.source.size() > kPreviewMaxSourceBytes ||
            compiled.source.find('\0') != std::string::npos ||
            compiled.digest != ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(compiled.source.data()),
                compiled.source.size())) {
            return Fail(error, "Bundle successful draft token is invalid");
        }
    }
    const PreviewClockState &clock = experiment.clock;
    if (!std::isfinite(clock.time_seconds) ||
        !std::isfinite(clock.speed) ||
        !std::isfinite(clock.loop_seconds) ||
        clock.time_seconds < 0.0 ||
        clock.speed < 0.05 || clock.speed > 8.0 ||
        clock.loop_seconds < 0.1 || clock.loop_seconds > 3600.0) {
        return Fail(error, "Bundle clock is outside supported bounds");
    }
    size_t texture_bytes = 0;
    for (const auto &texture : experiment.owned_textures) {
        if (texture.size() > kMaxTextureBytes || texture.size() % 4) {
            return Fail(error, "Bundle owned RGBA8 texture bytes are invalid");
        }
        texture_bytes += texture.size();
    }
    if (texture_bytes > kMaxTextureBytes * 4 ||
        experiment.dependencies.size() > 32) {
        return Fail(error, "Bundle texture or dependency count exceeds limit");
    }
    bool overflow = false;
    size_t owned_bytes = PreviewPacketOwnedBytes(packet, &overflow);
    const size_t supplemental = experiment.recipe.bytes.size() +
        original.text.size() + experiment.edited_text.size() +
        (experiment.successful_compile ?
            experiment.successful_compile->source.size() : 0) + texture_bytes;
    if (overflow || supplemental > kPreviewMaxOwnedPacketBytes ||
        owned_bytes > kPreviewMaxOwnedPacketBytes - supplemental) {
        return Fail(error, "Workbench bundle exceeds 32 MiB owned-data limit");
    }
    std::set<std::string> names;
    for (const WorkbenchDependency &dependency : experiment.dependencies) {
        if (dependency.name.empty() || dependency.name.size() > 128 ||
            dependency.name.find_first_of("/\\") != std::string::npos ||
            dependency.name.find("..") != std::string::npos ||
            dependency.reason.size() > 1024 ||
            dependency.status > WorkbenchDependencyStatus::Missing ||
            !names.insert(dependency.name).second) {
            return Fail(error, "Bundle dependency name, status, or reason is invalid");
        }
    }
    for (size_t stage = 0; stage < experiment.owned_textures.size(); ++stage) {
        const std::vector<uint8_t> &owned = experiment.owned_textures[stage];
        if (owned.empty()) continue;
        const PreviewTexturePixels generated =
            GeneratePreviewTexture(decoded_fixture, stage);
        if (owned.size() != generated.size() ||
            !std::equal(owned.begin(), owned.end(), generated.begin())) {
            return Fail(error, "Bundle texture bytes differ from the synthetic fixture");
        }
        const std::string name = "T" + std::to_string(stage);
        const auto dependency = std::find_if(
            experiment.dependencies.begin(), experiment.dependencies.end(),
            [&name](const WorkbenchDependency &candidate) {
                return candidate.name == name;
            });
        if (dependency == experiment.dependencies.end() ||
            dependency->status != WorkbenchDependencyStatus::Synthetic ||
            dependency->reason.empty()) {
            return Fail(error, "Bundle texture lacks synthetic provenance");
        }
    }
    return true;
}

bool SerializeWorkbenchExperiment(const WorkbenchExperiment &experiment,
                                  std::string *json, std::string *error)
{
    if (!json) return Fail(error, "Bundle output is unavailable");
    if (!ValidateWorkbenchExperiment(experiment, error)) return false;
    std::string recipe_json;
    if (!SerializePortableRecipe(experiment.recipe, &recipe_json, error)) {
        return false;
    }
    const PreviewPacket &packet = experiment.packet;
    Json bindings = Json::array();
    for (size_t i = 0; i < packet.binding_count; ++i) {
        bindings.push_back(BindingJson(packet.bindings[i]));
    }
    Json textures = Json::array();
    for (const auto &texture : experiment.owned_textures) {
        textures.push_back(Hex(texture.data(), texture.size()));
    }
    Json dependencies = Json::array();
    for (const WorkbenchDependency &dependency : experiment.dependencies) {
        dependencies.push_back({{"name", dependency.name},
                                {"status", dependency.status},
                                {"reason", dependency.reason}});
    }
    Json source = {{"key_stage", experiment.original.key.stage},
                   {"key_hash_version", experiment.original.key.hash.version},
                   {"key_hash", ShaderHashHex(experiment.original.key.hash)},
                   {"scope", ScopeJson(experiment.original.scope)},
                   {"backend", experiment.original.backend},
                   {"stage", experiment.original.stage},
                   {"route", experiment.original.route},
                   {"resident", experiment.original.resident},
                   {"build_scope_verified", experiment.original.build_scope_verified},
                   {"generator_abi", experiment.original.generator_abi},
                   {"interface_abi", experiment.original.interface_abi},
                   {"digest", DigestHex(experiment.original.digest)},
                   {"text", experiment.original.text}};
    Json compiled = nullptr;
    if (experiment.successful_compile) {
        const FrozenDraftCompile &token = *experiment.successful_compile;
        compiled = {{"draft_id", token.draft_id},
                    {"revision", token.revision},
                    {"submission_id", token.submission_id},
                    {"digest", DigestHex(token.digest)},
                    {"source", token.source}};
    }
    Json payload = {
        {"recipe_export", recipe_json},
        {"selection", {{"scope", ScopeJson(packet.selection.scope)},
                       {"session_epoch", packet.selection.session_epoch},
                       {"renderer_epoch", packet.selection.renderer_epoch},
                       {"backend", packet.selection.backend},
                       {"mode", packet.selection.mode}}},
        {"packet", {{"scene", SceneJson(packet.scene)},
                    {"render_state", RenderJson(packet.render_state)},
                    {"source_route", packet.source_route},
                    {"source_resident", packet.source_resident},
                    {"source_variant", packet.source_variant},
                    {"draft_id", packet.draft_id},
                    {"draft_revision", packet.draft_revision},
                    {"draft_submission_id", packet.draft_submission_id},
                    {"source", packet.source},
                    {"source_digest", DigestHex(packet.source_digest)},
                    {"partner_source", packet.partner_source},
                    {"partner_digest", DigestHex(packet.partner_digest)},
                    {"fixture_bytes", Hex(packet.fixture_bytes.data(),
                                           packet.fixture_bytes.size())},
                    {"fixture_digest", DigestHex(packet.fixture_digest)},
                    {"generator_abi", packet.generator_abi},
                    {"interface_abi", packet.interface_abi},
                    {"replacement_id", packet.replacement_id},
                    {"replacement_revision", packet.replacement_revision},
                    {"input_revision", packet.input_revision},
                    {"bindings", std::move(bindings)},
                    {"view_revision", packet.view_revision},
                    {"width", packet.width}, {"height", packet.height},
                    {"update_policy", packet.update_policy}}},
        {"original", std::move(source)},
        {"edited_text", experiment.edited_text},
        {"edit_revision", experiment.edit_revision},
        {"successful_compile", std::move(compiled)},
        {"clock", ClockJson(experiment.clock)},
        {"owned_textures", std::move(textures)},
        {"dependencies", std::move(dependencies)},
    };
    const std::string payload_bytes = payload.dump();
    PreviewDigest digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(payload_bytes.data()),
        payload_bytes.size());
    Json document = {{"schema", kSchema}, {"label", kLabel},
                     {"payload_size", payload_bytes.size()},
                     {"payload_digest", DigestHex(digest)},
                     {"payload", std::move(payload)}};
    *json = document.dump(2) + '\n';
    if (json->size() > kMaxWorkbenchBundleJsonBytes) {
        json->clear();
        return Fail(error, "Workbench bundle exceeds 24 MiB JSON limit");
    }
    return true;
}

bool ParseWorkbenchExperiment(const std::string &json,
                              WorkbenchExperiment *experiment,
                              std::string *error)
{
    if (!experiment || json.size() > kMaxWorkbenchBundleJsonBytes) {
        return Fail(error, "Workbench bundle output or size is invalid");
    }
    try {
        BundleShapeSax shape;
        if (!Json::sax_parse(json, &shape)) {
            return Fail(error, "Workbench bundle JSON structure exceeds limits");
        }
        const Json document = Json::parse(json);
        if (document.at("schema").get<std::string>() != kSchema ||
            document.at("label").get<std::string>() != kLabel) {
            return Fail(error, "Unsupported workbench test bundle schema or label");
        }
        const Json &payload = document.at("payload");
        const std::string payload_bytes = payload.dump();
        if (document.at("payload_size").get<size_t>() !=
                payload_bytes.size() ||
            ParseDigest(document.at("payload_digest")) !=
                ComputePreviewDigest(
                    reinterpret_cast<const uint8_t *>(payload_bytes.data()),
                    payload_bytes.size())) {
            return Fail(error, "Workbench bundle payload size or digest differs");
        }
        WorkbenchExperiment parsed{};
        const std::string recipe_json =
            payload.at("recipe_export").get<std::string>();
        if (recipe_json.size() > 64U * 1024U ||
            !ParsePortableRecipe(recipe_json, &parsed.recipe, error)) {
            return Fail(error, "Workbench bundle canonical recipe is invalid");
        }
        PreviewPacket &packet = parsed.packet;
        const Json &selection = payload.at("selection");
        packet.selection.scope = ParseScope(selection.at("scope"));
        packet.selection.shader = parsed.recipe.key;
        packet.selection.session_epoch =
            selection.at("session_epoch").get<uint64_t>();
        packet.selection.renderer_epoch =
            selection.at("renderer_epoch").get<uint64_t>();
        packet.selection.backend = static_cast<PreviewBackend>(
            selection.at("backend").get<unsigned>());
        packet.selection.mode = static_cast<PreviewMode>(
            selection.at("mode").get<unsigned>());
        packet.recipe_format_version = parsed.recipe.recipe_format_version;
        packet.recipe = parsed.recipe.bytes;
        const Json &data = payload.at("packet");
        packet.scene = ParseScene(data.at("scene"));
        packet.render_state = ParseRender(data.at("render_state"));
        packet.source_route = static_cast<Route>(
            data.at("source_route").get<unsigned>());
        packet.source_resident = data.at("source_resident").get<bool>();
        packet.source_variant = static_cast<PreviewSourceVariant>(
            data.at("source_variant").get<unsigned>());
        packet.draft_id = data.at("draft_id").get<uint64_t>();
        packet.draft_revision = data.at("draft_revision").get<uint64_t>();
        packet.draft_submission_id =
            data.at("draft_submission_id").get<uint64_t>();
        packet.source = data.at("source").get<std::string>();
        packet.source_digest = ParseDigest(data.at("source_digest"));
        packet.partner_source = data.at("partner_source").get<std::string>();
        packet.partner_digest = ParseDigest(data.at("partner_digest"));
        packet.fixture_bytes = ParseBytes(data.at("fixture_bytes"),
                                         kPreviewMaxOwnedPacketBytes);
        packet.fixture_digest = ParseDigest(data.at("fixture_digest"));
        packet.generator_abi = data.at("generator_abi").get<uint32_t>();
        packet.interface_abi = data.at("interface_abi").get<uint32_t>();
        packet.replacement_id = data.at("replacement_id").get<uint64_t>();
        packet.replacement_revision =
            data.at("replacement_revision").get<uint64_t>();
        packet.input_revision = data.at("input_revision").get<uint64_t>();
        const Json &bindings = data.at("bindings");
        if (!bindings.is_array() ||
            bindings.size() > kPreviewMaxInputBindings) {
            return Fail(error, "Bundle input binding count exceeds limit");
        }
        packet.binding_count = static_cast<uint8_t>(bindings.size());
        for (size_t i = 0; i < bindings.size(); ++i) {
            packet.bindings[i] = ParseBinding(bindings[i]);
        }
        packet.view_revision = data.at("view_revision").get<uint64_t>();
        packet.width = data.at("width").get<uint32_t>();
        packet.height = data.at("height").get<uint32_t>();
        packet.update_policy = static_cast<PreviewUpdatePolicy>(
            data.at("update_policy").get<unsigned>());
        packet.packet_kind = PreviewPacketKind::Synthetic;
        packet.replay_class = PreviewReplayClass::Synthetic;

        const Json &source = payload.at("original");
        parsed.original.key.stage = static_cast<Stage>(
            source.at("key_stage").get<unsigned>());
        parsed.original.key.hash.version =
            source.at("key_hash_version").get<uint32_t>();
        size_t hash_bytes = 0;
        if (!Unhex(source.at("key_hash").get<std::string>(),
                   parsed.original.key.hash.bytes.data(),
                   parsed.original.key.hash.bytes.size(), &hash_bytes) ||
            hash_bytes != parsed.original.key.hash.bytes.size()) {
            return Fail(error, "Bundle original source shader hash is invalid");
        }
        parsed.original.scope = ParseScope(source.at("scope"));
        parsed.original.backend = static_cast<PreviewBackend>(
            source.at("backend").get<unsigned>());
        parsed.original.stage = static_cast<HostSourceStage>(
            source.at("stage").get<unsigned>());
        parsed.original.route = static_cast<Route>(
            source.at("route").get<unsigned>());
        parsed.original.resident = source.at("resident").get<bool>();
        parsed.original.build_scope_verified =
            source.at("build_scope_verified").get<bool>();
        parsed.original.generator_abi =
            source.at("generator_abi").get<uint32_t>();
        parsed.original.interface_abi =
            source.at("interface_abi").get<uint32_t>();
        parsed.original.digest = ParseDigest(source.at("digest"));
        parsed.original.text = source.at("text").get<std::string>();

        parsed.edited_text = payload.at("edited_text").get<std::string>();
        parsed.edit_revision = payload.at("edit_revision").get<uint64_t>();
        if (!payload.at("successful_compile").is_null()) {
            const Json &compiled = payload.at("successful_compile");
            FrozenDraftCompile token{};
            token.draft_id = compiled.at("draft_id").get<uint64_t>();
            token.revision = compiled.at("revision").get<uint64_t>();
            token.submission_id =
                compiled.at("submission_id").get<uint64_t>();
            token.digest = ParseDigest(compiled.at("digest"));
            token.source = compiled.at("source").get<std::string>();
            parsed.successful_compile = std::move(token);
        }
        parsed.clock = ParseClock(payload.at("clock"));
        const Json &textures = payload.at("owned_textures");
        if (!textures.is_array() || textures.size() != 4) {
            return Fail(error, "Bundle owned texture count is invalid");
        }
        for (size_t i = 0; i < 4; ++i) {
            parsed.owned_textures[i] = ParseBytes(textures[i], kMaxTextureBytes);
        }
        const Json &dependencies = payload.at("dependencies");
        if (!dependencies.is_array() || dependencies.size() > 32) {
            return Fail(error, "Bundle dependency count exceeds limit");
        }
        for (const Json &item : dependencies) {
            WorkbenchDependency dependency{};
            dependency.name = item.at("name").get<std::string>();
            dependency.status = static_cast<WorkbenchDependencyStatus>(
                item.at("status").get<unsigned>());
            dependency.reason = item.at("reason").get<std::string>();
            parsed.dependencies.push_back(std::move(dependency));
        }
        if (!ValidateWorkbenchExperiment(parsed, error)) return false;
        *experiment = std::move(parsed);
        return true;
    } catch (const std::exception &) {
        return Fail(error, "Malformed workbench test bundle JSON");
    }
}

bool ExportWorkbenchExperiment(const WorkbenchExperiment &experiment,
                               const std::filesystem::path &config_directory,
                               std::filesystem::path *exported_path,
                               std::string *error)
{
    if (config_directory.empty() || !exported_path) {
        return Fail(error, "Workbench test export path unavailable");
    }
    std::string json;
    if (!SerializeWorkbenchExperiment(experiment, &json, error)) return false;
    WorkbenchExperiment verified{};
    if (!ParseWorkbenchExperiment(json, &verified, error)) return false;
    std::error_code ec;
    const auto root = config_directory / "shader-exports";
    std::filesystem::create_directories(root, ec);
    if (ec) return Fail(error, "Unable to create shader export directory");
    const std::string stem = "synthetic-" +
        ShaderHashHex(experiment.recipe.key.hash) + "-" +
        std::to_string(experiment.packet.selection.scope.title_id);
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        const auto target = root /
            (stem + "-" + std::to_string(suffix) + ".xemu-test.json");
        const auto temporary_dir = root /
            (".workbench-temp-" +
             std::to_string(std::chrono::steady_clock::now()
                 .time_since_epoch().count()) + "-" +
             std::to_string(suffix));
        if (!std::filesystem::create_directory(temporary_dir, ec)) {
            if (ec == std::errc::file_exists) {
                ec.clear();
                continue;
            }
            break;
        }
        const auto temporary = temporary_dir / "bundle.tmp";
        {
            // The parent directory was exclusively created by this export.
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file || !file.write(json.data(), json.size()) ||
                !file.flush()) {
                std::filesystem::remove_all(temporary_dir, ec);
                return Fail(error, "Unable to write workbench test bundle");
            }
        }
        bool collision = false;
        if (PublishNoReplace(temporary, target, &collision)) {
            std::filesystem::remove_all(temporary_dir, ec);
            *exported_path = target;
            return true;
        }
        std::filesystem::remove_all(temporary_dir, ec);
        if (collision) continue;
        break;
    }
    return Fail(error, "Unable to publish a unique workbench test bundle");
}

bool ReadWorkbenchExperiment(const std::filesystem::path &path,
                             WorkbenchExperiment *experiment,
                             std::string *error)
{
    const std::string name = path.filename().u8string();
    constexpr char suffix[] = ".xemu-test.json";
    if (!experiment || name.size() <= sizeof(suffix) - 1 ||
        name.compare(name.size() - (sizeof(suffix) - 1),
                     sizeof(suffix) - 1, suffix) != 0) {
        return Fail(error, "Selected file is not a workbench test bundle");
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxWorkbenchBundleJsonBytes) {
        return Fail(error, "Workbench test bundle file is unavailable or oversized");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail(error, "Unable to open workbench test bundle");
    // The earlier file_size check is advisory: the file can grow before or
    // during this read. Allocate only that checked size, then probe for one
    // extra byte instead of reading to EOF into an unbounded string.
    std::string bytes(static_cast<size_t>(size), '\0');
    if (size) file.read(bytes.data(), static_cast<std::streamsize>(size));
    if (file.gcount() != static_cast<std::streamsize>(size) || file.bad()) {
        return Fail(error, "Unable to read complete workbench test bundle");
    }
    char extra = 0;
    if (file.get(extra) || file.bad()) {
        return Fail(error, "Workbench test bundle changed during read");
    }
    return ParseWorkbenchExperiment(bytes, experiment, error);
}

} // namespace xemu::shader_browser
