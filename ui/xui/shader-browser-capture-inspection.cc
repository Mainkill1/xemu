// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-inspection.hh"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <set>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace xemu::shader_browser {
namespace {
std::atomic<uint64_t> next_job{ 1 }, next_track{ 1 };
using Category = CaptureInspectionCategory;
struct Field {
    std::string path;
    Category category;
    CaptureInspectionValue value;
};
using Fields = std::vector<Field>;
bool Valid(const CaptureInspectionSettings &s)
{
    return s.maximum_events && s.maximum_events <= 1000000 && s.maximum_jobs &&
           s.maximum_jobs <= 64 && s.maximum_fields &&
           s.maximum_fields <= 32768 && s.maximum_results &&
           s.maximum_results <= 4096 && s.maximum_tracks &&
           s.maximum_tracks <= 1024 && s.maximum_track_frames &&
           s.maximum_track_frames <= 4096 && s.maximum_track_candidates &&
           s.maximum_track_candidates <= 32768 && s.maximum_candidates &&
           s.maximum_candidates <= 1024 && s.maximum_page &&
           s.maximum_page <= 512 && s.comparison_byte_budget &&
           s.comparison_byte_budget <= 256U * 1024U * 1024U;
}
std::string Name(const std::string &name)
{
    if (name.size() > 256)
        throw std::length_error("Input field name exceeds inspection bound");
    std::string result;
    for (unsigned char c : name) {
        if (c == '/' || c == '%') {
            result += '%';
            result += "0123456789abcdef"[c >> 4];
            result += "0123456789abcdef"[c & 15];
        } else
            result += char(c);
    }
    return result;
}
CaptureInspectionValue Scalar(uint64_t value, size_t bytes = 4)
{
    CaptureInspectionValue result;
    result.present = true;
    for (size_t i = 0; i < bytes; ++i)
        result.bits.push_back(uint8_t(value >> (i * 8)));
    return result;
}
CaptureInspectionValue Bytes(const SharedCaptureBlock &block)
{
    CaptureInspectionValue result;
    result.present = bool(block);
    result.block = block;
    return result;
}
Category StateCategory(const std::string &name)
{
    if (name.find("raster") != std::string::npos ||
        name.find("viewport") != std::string::npos ||
        name.find("scissor") != std::string::npos ||
        name.find("blend") != std::string::npos ||
        name.find("depth") != std::string::npos ||
        name.find("stencil") != std::string::npos)
        return Category::RasterState;
    if (name.find("vertex") != std::string::npos ||
        name.find("indices") != std::string::npos ||
        name.find("ranges") != std::string::npos ||
        name.find("geometry") != std::string::npos)
        return Category::VertexInput;
    if (name.find("texture") != std::string::npos ||
        name.find("sampler") != std::string::npos)
        return Category::Texture;
    return Category::RawState;
}
Fields Flatten(const CaptureOccurrence &e, uint32_t maximum)
{
    Fields fields;
    auto add = [&](std::string path, Category category,
                   CaptureInspectionValue value) {
        if (fields.size() == maximum)
            throw std::length_error("Input field budget exceeded");
        if (value.present)
            fields.push_back({ std::move(path), category, std::move(value) });
    };
    auto scalar = [&](const std::string &path, Category c, uint64_t value,
                      size_t bytes = 4) { add(path, c, Scalar(value, bytes)); };
    scalar("event/type", Category::EventMetadata, uint32_t(e.type));
    scalar("event/emitted", Category::EventMetadata, e.emitted);
    scalar("event/finished", Category::EventMetadata, e.finished);
    scalar("event/pending", Category::EventMetadata, e.pending);
    scalar("event/limitations", Category::EventMetadata, e.limitations);
    scalar("event/inputs_complete", Category::EventMetadata, e.inputs.complete);
    scalar("draw/primitive_mode", Category::VertexInput,
           e.summary.primitive_mode);
    scalar("draw/vertex_count", Category::VertexInput, e.summary.vertex_count);
    scalar("draw/index_count", Category::VertexInput, e.summary.index_count);
    scalar("draw/primitive_count", Category::VertexInput,
           e.summary.primitive_count);
    scalar("draw/shader_count", Category::StageSource, e.summary.shader_count);
    for (size_t i = 0;
         i < std::min<size_t>(e.summary.shader_count, e.summary.shaders.size());
         ++i) {
        const auto &shader = e.summary.shaders[i];
        const auto p = "shader/" + std::to_string(i) + "/";
        scalar(p + "stage", Category::StageSource, uint32_t(shader.stage));
        scalar(p + "hash_version", Category::StageSource, shader.hash.version);
        CaptureInspectionValue hash;
        hash.present = true;
        hash.bits.assign(shader.hash.bytes.begin(), shader.hash.bytes.end());
        add(p + "hash", Category::StageSource, std::move(hash));
    }
    scalar("geometry/position_count", Category::VertexInput,
           e.geometry.position_count, 8);
    scalar("geometry/index_count", Category::VertexInput,
           e.geometry.index_count, 8);
    add("geometry/positions", Category::VertexInput,
        Bytes(e.geometry.positions));
    add("geometry/indices", Category::VertexInput, Bytes(e.geometry.indices));
    for (size_t i = 0; i < e.inputs.sources.size(); ++i)
        add("source/" + std::to_string(i), Category::StageSource,
            Bytes(e.inputs.sources[i]));
    std::map<std::string, size_t> repeats;
    for (const auto &u : e.inputs.uniforms) {
        const auto name = std::to_string(u.stage) + "/" + Name(u.name);
        const auto p =
            "uniform/" + name + "/" + std::to_string(repeats[name]++) + "/";
        scalar(p + "type", Category::Uniform, u.type);
        scalar(p + "components", Category::Uniform, u.components);
        scalar(p + "count", Category::Uniform, u.count);
        add(p + "data", Category::Uniform, Bytes(u.data));
    }
    repeats.clear();
    for (const auto &r : e.inputs.registers) {
        const auto name = Name(r.name);
        scalar("register/" + name + "/" + std::to_string(repeats[name]++),
               StateCategory(r.name), r.value);
    }
    repeats.clear();
    for (const auto &b : e.inputs.blobs) {
        const auto name = Name(b.name);
        const auto p =
            "blob/" + name + "/" + std::to_string(repeats[name]++) + "/";
        const auto c = StateCategory(b.name);
        scalar(p + "slot", c, b.slot);
        scalar(p + "format", c, b.format);
        scalar(p + "components", c, b.components);
        scalar(p + "stride", c, b.stride);
        scalar(p + "count", c, b.count);
        scalar(p + "offset", c, b.offset, 8);
        scalar(p + "normalized", c, b.normalized);
        scalar(p + "integer", c, b.integer);
        add(p + "data", c, Bytes(b.data));
    }
    for (size_t i = 0; i < e.inputs.textures.size(); ++i) {
        const auto &t = e.inputs.textures[i];
        const auto &m = t.metadata;
        const auto p = "texture/" + std::to_string(i) + "/";
        scalar(p + "described", Category::Texture, t.described);
        if (!t.described)
            continue;
        scalar(p + "slot", Category::Texture, m.slot);
        scalar(p + "bound", Category::Texture, uint32_t(m.bound));
        scalar(p + "guest_format", Category::Texture, m.guest_format);
        scalar(p + "host_format", Category::Texture, m.host_format);
        scalar(p + "width", Category::Texture, m.width);
        scalar(p + "height", Category::Texture, m.height);
        scalar(p + "depth", Category::Texture, m.depth);
        scalar(p + "mip_levels", Category::Texture, m.mip_levels);
        scalar(p + "face_count", Category::Texture, m.face_count);
        scalar(p + "min_filter", Category::Texture, m.min_filter);
        scalar(p + "mip_level", Category::Texture, m.mip_level);
        scalar(p + "face", Category::Texture, m.face);
        scalar(p + "mag_filter", Category::Texture, m.mag_filter);
        scalar(p + "wrap_s", Category::Texture, m.wrap_s);
        scalar(p + "wrap_t", Category::Texture, m.wrap_t);
        scalar(p + "wrap_r", Category::Texture, m.wrap_r);
        uint32_t bits;
        std::memcpy(&bits, &m.coordinate_scale, sizeof(bits));
        scalar(p + "coordinate_scale", Category::Texture, bits);
        repeats.clear();
        for (const auto &image : t.images) {
            const auto name = std::to_string(image.mip_level) + "/" +
                              std::to_string(image.face);
            const auto ip = p + "image/" + name + "/" +
                            std::to_string(repeats[name]++) + "/";
            scalar(ip + "width", Category::Texture, image.image.width);
            scalar(ip + "height", Category::Texture, image.image.height);
            add(ip + "rgba", Category::Texture, Bytes(image.image.rgba));
        }
    }
    for (size_t i = 0; i < e.summary.resources.size(); ++i) {
        const auto &r = e.summary.resources[i];
        const auto p = "resource/" + std::to_string(i) + "/";
        scalar(p + "slot", Category::RawState, r.slot);
        scalar(p + "kind", Category::RawState, uint32_t(r.resource.kind));
        scalar(p + "access", Category::RawState, uint32_t(r.access));
        scalar(p + "guest_address", Category::RawState,
               r.resource.guest.address, 8);
        scalar(p + "guest_length", Category::RawState, r.resource.guest.length,
               8);
        scalar(p + "storage_id", Category::RawState, r.resource.storage_id, 8);
        scalar(p + "storage_offset", Category::RawState,
               r.resource.storage_range.address, 8);
        scalar(p + "storage_length", Category::RawState,
               r.resource.storage_range.length, 8);
        scalar(p + "read_version", Category::RawState, r.read_version, 8);
        scalar(p + "write_version", Category::RawState, r.write_version, 8);
        CaptureInspectionValue descriptor, content;
        descriptor.present = content.present = true;
        descriptor.bits.assign(r.resource.descriptor_digest.begin(),
                               r.resource.descriptor_digest.end());
        content.bits.assign(r.resource.content_digest.begin(),
                            r.resource.content_digest.end());
        add(p + "descriptor_digest", Category::RawState, std::move(descriptor));
        add(p + "content_digest", Category::RawState, std::move(content));
    }
    std::sort(fields.begin(), fields.end(),
              [](const auto &a, const auto &b) { return a.path < b.path; });
    return fields;
}
const uint8_t *Data(const CaptureInspectionValue &v)
{
    return v.block ? v.block->bytes.data() : v.bits.data();
}
enum class Equal { Yes, No, Unknown };
Equal Compare(const CaptureInspectionValue &a, const CaptureInspectionValue &b,
              uint64_t &bytes, uint64_t budget)
{
    if (a.present != b.present || a.Size() != b.Size())
        return Equal::No;
    if (!a.present || (a.block && a.block == b.block))
        return Equal::Yes;
    const uint64_t size = a.Size();
    if (size > budget - bytes)
        return Equal::Unknown;
    bytes += size;
    return !size || !std::memcmp(Data(a), Data(b), size) ? Equal::Yes :
                                                           Equal::No;
}
bool CompleteInputs(const CaptureOccurrence &e)
{
    return e.finished && !e.pending && e.inputs.complete &&
           !(e.limitations & (CaptureReadbackFailed | CaptureInvalidated));
}
bool GeometryPresent(const CaptureOccurrence &e)
{
    return bool(e.geometry.positions) ||
           std::any_of(e.inputs.blobs.begin(), e.inputs.blobs.end(),
                       [](const auto &b) {
                           return b.name.find("vertex.attribute") !=
                                      std::string::npos &&
                                  b.data;
                       });
}
bool TexturesPresent(const CaptureOccurrence &e)
{
    return std::any_of(
        e.inputs.textures.begin(), e.inputs.textures.end(), [](const auto &t) {
            return t.described && t.metadata.bound && !t.images.empty();
        });
}
using Json = nlohmann::json;
constexpr size_t kAnnotationBytes = 4U * 1024U * 1024U;
uint64_t Uint(const Json &j, const char *name, uint64_t maximum = UINT64_MAX)
{
    const auto &v = j.at(name);
    if (!v.is_number_unsigned() &&
        (!v.is_number_integer() || v.get<int64_t>() < 0))
        throw std::runtime_error("Annotation integer is not unsigned");
    const auto result = v.get<uint64_t>();
    if (result > maximum)
        throw std::runtime_error("Annotation integer exceeds bounds");
    return result;
}
std::string String(const Json &j, const char *name, size_t maximum)
{
    const auto &v = j.at(name);
    if (!v.is_string() || v.get_ref<const std::string &>().size() > maximum)
        throw std::runtime_error("Annotation string exceeds bounds");
    return v.get<std::string>();
}
bool Boolean(const Json &j, const char *name)
{
    const auto &value = j.at(name);
    if (!value.is_boolean())
        throw std::runtime_error("Annotation flag is not boolean");
    return value.get<bool>();
}
Json ContextJson(const CaptureSessionContext &c)
{
    return {
        { "scope",
          { { "title_id", c.scope.title_id },
            { "fingerprint_version", c.scope.executable_fingerprint_version },
            { "fingerprint", c.scope.executable_fingerprint } } },
        { "scope_generation", c.scope_generation },
        { "session_epoch", c.session_epoch },
        { "renderer_epoch", c.renderer_epoch },
        { "generation", c.generation },
        { "current_frame", c.current_frame },
        { "backend", c.backend }
    };
}
bool SameContextIdentity(const CaptureSessionContext &a,
                         const CaptureSessionContext &b)
{
    return a.scope == b.scope && a.scope_generation == b.scope_generation &&
           a.session_epoch == b.session_epoch &&
           a.renderer_epoch == b.renderer_epoch &&
           a.generation == b.generation && a.backend == b.backend;
}
CaptureSessionContext ReadContext(const Json &j)
{
    CaptureSessionContext c;
    const auto &scope = j.at("scope");
    c.scope.title_id = Uint(scope, "title_id", UINT32_MAX);
    c.scope.executable_fingerprint_version =
        Uint(scope, "fingerprint_version", UINT32_MAX);
    const auto &fingerprint = scope.at("fingerprint");
    if (!fingerprint.is_array() ||
        fingerprint.size() != c.scope.executable_fingerprint.size())
        throw std::runtime_error("Annotation fingerprint extent is invalid");
    for (size_t i = 0; i < fingerprint.size(); ++i)
        c.scope.executable_fingerprint[i] =
            Uint(Json{ { "value", fingerprint[i] } }, "value", 255);
    c.scope_generation = Uint(j, "scope_generation");
    c.session_epoch = Uint(j, "session_epoch");
    c.renderer_epoch = Uint(j, "renderer_epoch");
    c.generation = Uint(j, "generation");
    c.current_frame = Uint(j, "current_frame");
    c.backend = Uint(j, "backend", UINT32_MAX);
    return c;
}
Json RefJson(const CaptureInspectionEventRef &r)
{
    return { { "recording_id", r.recording_id },
             { "event_id", r.event_id },
             { "frame", r.frame },
             { "key",
               { { "session_epoch", r.key.session_epoch },
                 { "renderer_epoch", r.key.renderer_epoch },
                 { "frame", r.key.frame },
                 { "draw", r.key.draw },
                 { "submission", r.key.submission } } } };
}
CaptureInspectionEventRef ReadRef(const Json &j)
{
    CaptureInspectionEventRef r;
    const auto &k = j.at("key");
    r.recording_id = Uint(j, "recording_id");
    r.event_id = Uint(j, "event_id");
    r.frame = Uint(j, "frame");
    r.key = { Uint(k, "session_epoch"), Uint(k, "renderer_epoch"),
              Uint(k, "frame"), uint32_t(Uint(k, "draw", UINT32_MAX)),
              Uint(k, "submission") };
    return r;
}
bool SafeDepth(const std::string &text)
{
    uint32_t depth = 0;
    bool quoted = false, escaped = false;
    for (const char c : text) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 32)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !depth && !quoted;
}
} // namespace

uint64_t CaptureInspectionValue::Size() const
{
    return block ? block->bytes.size() : bits.size();
}
bool CaptureInspectionEventRef::operator==(
    const CaptureInspectionEventRef &b) const
{
    return recording_id == b.recording_id && event_id == b.event_id &&
           frame == b.frame && key == b.key;
}

struct CaptureInspectionController::Impl {
    struct Job {
        CaptureInspectionPage page;
        bool changed_only = true, endpoint_incomplete = false;
        Fields before, after;
        size_t a = 0, b = 0, cursor = 0;
        std::vector<CaptureInspectionField> fields;
        std::vector<CaptureTrackCandidate> candidates;
        std::vector<CaptureInspectionTrendPoint> trend;
        std::vector<std::shared_ptr<const CaptureOccurrence>> scan;
        std::shared_ptr<const CaptureOccurrence> anchor;
        std::vector<CaptureTrackFrame> frames;
        std::string field_path;
        CaptureInspectionValue previous;
        bool previous_valid = false;
    };
    mutable std::mutex mutex;
    CaptureInspectionSettings settings;
    CaptureInspectionRecording recording;
    std::vector<std::shared_ptr<const CaptureOccurrence>> events;
    std::vector<uint64_t> retained_frames;
    std::unordered_map<uint64_t, std::shared_ptr<const CaptureOccurrence>>
        index;
    std::map<uint64_t, CaptureTemporalTrack> tracks;
    std::map<uint64_t, Job> jobs;
    std::string error;
    bool Restore(const std::vector<CaptureTemporalTrack> &);
    uint64_t NewTrackID()
    {
        for (size_t i = 0; i <= tracks.size(); ++i) {
            const auto id = next_track.fetch_add(1);
            if (!id || id >= kCaptureSessionTokenBit)
                break;
            if (!tracks.count(id))
                return id;
        }
        error = "Temporal track identity namespace exhausted";
        return 0;
    }
    CaptureInspectionEventRef Ref(const CaptureOccurrence &e) const
    {
        return { recording.recording_id, e.event_id, e.summary.key.frame,
                 e.summary.key };
    }
    std::shared_ptr<const CaptureOccurrence> Find(uint64_t id) const
    {
        auto it = index.find(id);
        return it == index.end() ? nullptr : it->second;
    }
    size_t FrameCount() const
    {
        size_t n = 0;
        for (const auto &t : tracks)
            n += t.second.frames.size();
        return n;
    }
    size_t CandidateCount() const
    {
        size_t n = 0;
        for (const auto &t : tracks)
            for (const auto &f : t.second.frames)
                n += f.candidates.size();
        return n;
    }
    CaptureTrackFrame *Frame(CaptureTemporalTrack &track, uint64_t frame)
    {
        auto it = std::lower_bound(
            track.frames.begin(), track.frames.end(), frame,
            [](const auto &f, uint64_t n) { return f.frame < n; });
        if (it != track.frames.end() && it->frame == frame)
            return &*it;
        if (FrameCount() == settings.maximum_track_frames) {
            error = "Confirmed/candidate frame budget exceeded";
            return nullptr;
        }
        CaptureTrackFrame f;
        f.frame = frame;
        return &*track.frames.insert(it, std::move(f));
    }
    Job *New(CaptureInspectionJobKind kind)
    {
        if (!recording.recording_id || jobs.size() == settings.maximum_jobs) {
            error = "Inspection job budget exceeded or recording unavailable";
            return nullptr;
        }
        const uint64_t id = next_job.fetch_add(1);
        auto &job = jobs[id];
        job.page.job_id = id;
        job.page.kind = kind;
        job.page.state = CaptureInspectionJobState::Running;
        error.clear();
        return &job;
    }
    uint64_t Difference(uint64_t a, uint64_t b,
                        CaptureInspectionRelation relation, bool changed_only,
                        uint64_t track = 0)
    {
        const auto before = Find(a), after = Find(b);
        if (!before || !after || before->type != CaptureEventType::Draw ||
            after->type != CaptureEventType::Draw) {
            error = "Draw occurrence identity is unavailable";
            return 0;
        }
        auto *job = New(CaptureInspectionJobKind::Difference);
        if (!job)
            return 0;
        job->page.before = Ref(*before);
        job->page.after = Ref(*after);
        job->page.relation = relation;
        job->page.track_id = track;
        if (track)
            job->page.track_revision = tracks.at(track).revision;
        job->changed_only = changed_only;
        job->endpoint_incomplete =
            !CompleteInputs(*before) || !CompleteInputs(*after);
        try {
            job->before = Flatten(*before, settings.maximum_fields);
            job->after = Flatten(*after, settings.maximum_fields);
        } catch (const std::exception &e) {
            job->page.state = CaptureInspectionJobState::Incomplete;
            job->page.message = e.what();
        }
        job->page.total = job->before.size() + job->after.size();
        return job->page.job_id;
    }
    void Terminal(Job &job, const std::string &reason)
    {
        job.page.state = CaptureInspectionJobState::Incomplete;
        job.page.message = reason;
        if (job.page.kind != CaptureInspectionJobKind::Candidates ||
            !job.page.track_id)
            return;
        auto &track = tracks.at(job.page.track_id);
        auto *frame = Frame(track, job.page.after.frame);
        if (!frame || frame->state == CaptureTrackFrameState::Confirmed)
            return;
        frame->state = CaptureTrackFrameState::Incomplete;
        if (CandidateCount() - frame->candidates.size() +
                job.candidates.size() <=
            settings.maximum_track_candidates)
            frame->candidates = job.candidates;
        else
            frame->candidates.clear();
        frame->note = reason;
        ++track.revision;
    }
    void DifferenceStep(Job &job)
    {
        if (job.a == job.before.size() && job.b == job.after.size()) {
            job.page.state = job.endpoint_incomplete ?
                                 CaptureInspectionJobState::Incomplete :
                                 CaptureInspectionJobState::Ready;
            job.page.message =
                job.endpoint_incomplete ?
                    "Exact retained bits; one occurrence has incomplete "
                    "inputs" :
                    "Exact retained input bits; correspondence is not implied";
            return;
        }
        CaptureInspectionField field;
        const auto *a =
            job.a < job.before.size() ? &job.before[job.a] : nullptr;
        const auto *b = job.b < job.after.size() ? &job.after[job.b] : nullptr;
        if (!b || (a && a->path < b->path)) {
            field = { a->path,
                      a->category,
                      CaptureInspectionChange::Removed,
                      a->value,
                      {} };
            ++job.a;
        } else if (!a || b->path < a->path) {
            field = { b->path,
                      b->category,
                      CaptureInspectionChange::Added,
                      {},
                      b->value };
            ++job.b;
        } else {
            auto equal = Compare(a->value, b->value, job.page.compared_bytes,
                                 settings.comparison_byte_budget);
            field = { a->path, a->category,
                      equal == Equal::Yes ? CaptureInspectionChange::Unchanged :
                      equal == Equal::No  ? CaptureInspectionChange::Changed :
                                            CaptureInspectionChange::Uncompared,
                      a->value, b->value };
            ++job.a;
            ++job.b;
        }
        job.page.processed = job.a + job.b;
        if (field.change != CaptureInspectionChange::Unchanged &&
            field.change != CaptureInspectionChange::Uncompared)
            ++job.page.changed_fields;
        if (!job.changed_only ||
            field.change != CaptureInspectionChange::Unchanged) {
            if (job.fields.size() == settings.maximum_results) {
                ++job.page.omitted_results;
                Terminal(job, "Difference result field budget exceeded; "
                              "remaining fields were not compared");
                return;
            }
            job.fields.push_back(std::move(field));
        }
        if (!job.fields.empty() &&
            job.fields.back().change == CaptureInspectionChange::Uncompared)
            Terminal(job, "Comparison byte budget exceeded; retained field raw "
                          "bytes were not compared");
    }
    void PublishCandidates(Job &job)
    {
        auto &track = tracks.at(job.page.track_id);
        auto *frame = Frame(track, job.page.after.frame);
        if (!frame) {
            Terminal(job, error);
            return;
        }
        if (frame->state != CaptureTrackFrameState::Confirmed) {
            if (CandidateCount() - frame->candidates.size() +
                    job.candidates.size() >
                settings.maximum_track_candidates) {
                Terminal(job, "Track candidate storage budget exceeded");
                return;
            }
            frame->candidates = job.candidates;
            frame->state =
                job.page.omitted_results ? CaptureTrackFrameState::Incomplete :
                job.candidates.empty()   ? CaptureTrackFrameState::Missing :
                job.candidates.size() == 1 ? CaptureTrackFrameState::Candidate :
                                             CaptureTrackFrameState::Ambiguous;
            frame->note = "Input evidence proposes correspondence; explicit "
                          "user confirmation is required";
            ++track.revision;
        }
        job.page.state = job.page.omitted_results ?
                             CaptureInspectionJobState::Incomplete :
                             CaptureInspectionJobState::Ready;
        job.page.message = "Candidates are input evidence, not object "
                           "identity; duplicate inputs remain ambiguous";
    }
    void CandidateStep(Job &job)
    {
        if (job.cursor == job.scan.size()) {
            PublishCandidates(job);
            return;
        }
        const auto &event = *job.scan[job.cursor++];
        ++job.page.processed;
        if (!CompleteInputs(event)) {
            ++job.page.omitted_results;
            return;
        }
        if (!event.emitted)
            return;
        Fields candidate;
        try {
            candidate = Flatten(event, settings.maximum_fields);
        } catch (const std::exception &e) {
            Terminal(job, e.what());
            return;
        }
        std::array<bool, 7> equal{};
        equal.fill(true);
        std::array<bool, 7> seen{};
        size_t a = 0, b = 0;
        while (a < job.before.size() || b < candidate.size()) {
            const auto *af = a < job.before.size() ? &job.before[a] : nullptr;
            const auto *bf = b < candidate.size() ? &candidate[b] : nullptr;
            if (!bf || (af && af->path < bf->path)) {
                equal[size_t(af->category)] = false;
                ++a;
            } else if (!af || bf->path < af->path) {
                equal[size_t(bf->category)] = false;
                ++b;
            } else {
                const auto category = size_t(af->category);
                seen[category] = true;
                if (af->category == Category::VertexInput ||
                    af->category == Category::Uniform ||
                    af->category == Category::Texture ||
                    af->category == Category::RasterState) {
                    const auto result =
                        Compare(af->value, bf->value, job.page.compared_bytes,
                                settings.comparison_byte_budget);
                    if (result == Equal::Unknown) {
                        Terminal(job,
                                 "Candidate comparison byte budget exceeded; "
                                 "correspondence remains unresolved");
                        return;
                    }
                    if (result != Equal::Yes)
                        equal[category] = false;
                }
                ++a;
                ++b;
            }
        }
        uint32_t evidence = 0, independent = 0;
        if (seen[size_t(Category::VertexInput)] &&
            equal[size_t(Category::VertexInput)] &&
            GeometryPresent(*job.anchor) && GeometryPresent(event)) {
            evidence |= CaptureTrackSameGeometry;
            ++independent;
        }
        if (seen[size_t(Category::Uniform)] &&
            equal[size_t(Category::Uniform)] &&
            !job.anchor->inputs.uniforms.empty() &&
            !event.inputs.uniforms.empty()) {
            evidence |= CaptureTrackSameUniformBits;
            ++independent;
        }
        if (seen[size_t(Category::Texture)] &&
            equal[size_t(Category::Texture)] && TexturesPresent(*job.anchor) &&
            TexturesPresent(event)) {
            evidence |= CaptureTrackSameTextureBits;
            ++independent;
        }
        if (seen[size_t(Category::RasterState)] &&
            equal[size_t(Category::RasterState)])
            evidence |= CaptureTrackSameRasterBits;
        // Shader/hash/address/draw order carry no correspondence weight.
        if (independent < 2)
            return;
        if (job.candidates.size() == settings.maximum_candidates) {
            ++job.page.omitted_results;
            Terminal(
                job,
                "Candidate result budget exceeded; frame remains unresolved");
            return;
        }
        job.candidates.push_back({ Ref(event), evidence });
    }
    void TrendStep(Job &job)
    {
        if (job.cursor == job.frames.size()) {
            job.page.state = CaptureInspectionJobState::Ready;
            job.page.message =
                "Only confirmed frame selections define the trend; "
                "candidate/ambiguous gaps remain explicit";
            return;
        }
        const auto &frame = job.frames[job.cursor++];
        ++job.page.processed;
        CaptureInspectionTrendPoint point;
        point.frame = frame.frame;
        point.state = frame.state;
        if (frame.state == CaptureTrackFrameState::Confirmed) {
            point.event = frame.selected;
            auto event = Find(frame.selected.event_id);
            if (!event || !CompleteInputs(*event))
                point.state = CaptureTrackFrameState::Incomplete;
            else {
                Fields fields;
                try {
                    fields = Flatten(*event, settings.maximum_fields);
                } catch (const std::exception &e) {
                    Terminal(job, e.what());
                    return;
                }
                auto found = std::lower_bound(
                    fields.begin(), fields.end(), job.field_path,
                    [](const auto &f, const auto &name) {
                        return f.path < name;
                    });
                if (found != fields.end() && found->path == job.field_path)
                    point.value = found->value;
                if (point.value.present && job.previous_valid) {
                    const auto equal = Compare(job.previous, point.value,
                                               job.page.compared_bytes,
                                               settings.comparison_byte_budget);
                    if (equal == Equal::Unknown) {
                        Terminal(job, "Trend comparison byte budget exceeded");
                        return;
                    }
                    point.comparable_to_previous = true;
                    point.changed_from_previous = equal != Equal::Yes;
                }
            }
        }
        job.previous = point.value;
        job.previous_valid = point.state == CaptureTrackFrameState::Confirmed &&
                             point.value.present;
        if (job.trend.size() == settings.maximum_results) {
            ++job.page.omitted_results;
            Terminal(job, "Trend result budget exceeded");
            return;
        }
        job.trend.push_back(std::move(point));
    }
};

CaptureInspectionController::CaptureInspectionController()
    : impl_(std::make_unique<Impl>())
{
}
CaptureInspectionController::~CaptureInspectionController() = default;
bool CaptureInspectionController::Open(
    uint64_t id, const CaptureSessionSnapshot &snapshot,
    const CaptureInspectionSettings &settings)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    s.jobs.clear();
    s.tracks.clear();
    s.events.clear();
    s.retained_frames.clear();
    s.index.clear();
    s.recording = {};
    s.error.clear();
    if (!id || !Valid(settings) ||
        snapshot.events.size() > settings.maximum_events ||
        !snapshot.context.scope.title_id) {
        s.error = "Invalid recording identity or inspection bounds";
        return false;
    }
    for (const auto &event : snapshot.events) {
        if (!event || !event->event_id ||
            !(event->summary.scope == snapshot.context.scope) ||
            event->summary.key.session_epoch !=
                snapshot.context.session_epoch ||
            event->summary.key.renderer_epoch !=
                snapshot.context.renderer_epoch ||
            !s.index.emplace(event->event_id, event).second) {
            s.index.clear();
            s.error = "Invalid, duplicate or mismatched capture event identity";
            return false;
        }
    }
    s.settings = settings;
    s.recording = { id, snapshot.context };
    s.events = snapshot.events;
    std::stable_sort(
        s.events.begin(), s.events.end(),
        [](const auto &a, const auto &b) { return a->event_id < b->event_id; });
    for (const auto &event : s.events)
        s.retained_frames.push_back(event->summary.key.frame);
    std::sort(s.retained_frames.begin(), s.retained_frames.end());
    s.retained_frames.erase(
        std::unique(s.retained_frames.begin(), s.retained_frames.end()),
        s.retained_frames.end());
    return true;
}
CaptureInspectionEventRef
CaptureInspectionController::Reference(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto e = impl_->Find(id);
    return e ? impl_->Ref(*e) : CaptureInspectionEventRef{};
}
bool CaptureInspectionController::Refresh(
    uint64_t id, const CaptureSessionSnapshot &snapshot)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (id != s.recording.recording_id ||
        !SameContextIdentity(snapshot.context, s.recording.context) ||
        snapshot.events.size() > s.settings.maximum_events) {
        s.error = "Refresh belongs to another recording or exceeds bounds";
        return false;
    }
    decltype(s.index) index;
    for (const auto &e : snapshot.events)
        if (!e || !e->event_id ||
            !(e->summary.scope == snapshot.context.scope) ||
            e->summary.key.session_epoch != snapshot.context.session_epoch ||
            e->summary.key.renderer_epoch != snapshot.context.renderer_epoch ||
            !index.emplace(e->event_id, e).second) {
            s.error = "Refresh contains invalid capture event identities";
            return false;
        }
    s.jobs.clear();
    s.index = std::move(index);
    s.events = snapshot.events;
    s.retained_frames.clear();
    s.recording.context = snapshot.context;
    std::sort(
        s.events.begin(), s.events.end(),
        [](const auto &a, const auto &b) { return a->event_id < b->event_id; });
    for (const auto &e : s.events)
        s.retained_frames.push_back(e->summary.key.frame);
    std::sort(s.retained_frames.begin(), s.retained_frames.end());
    s.retained_frames.erase(
        std::unique(s.retained_frames.begin(), s.retained_frames.end()),
        s.retained_frames.end());
    for (auto &entry : s.tracks)
        for (auto &frame : entry.second.frames) {
            if (frame.state == CaptureTrackFrameState::Confirmed) {
                auto e = s.Find(frame.selected.event_id);
                frame.evidence_retained = e && s.Ref(*e) == frame.selected;
            } else {
                frame.evidence_retained = true;
                for (auto &candidate : frame.candidates) {
                    auto e = s.Find(candidate.event.event_id);
                    candidate.evidence_retained =
                        e && s.Ref(*e) == candidate.event;
                    frame.evidence_retained &= candidate.evidence_retained;
                }
            }
        }
    s.error.clear();
    return true;
}
uint64_t
CaptureInspectionController::BeginWithinFrameComparison(uint64_t a, uint64_t b,
                                                        bool changed_only)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto lhs = s.Find(a), rhs = s.Find(b);
    if (!lhs || !rhs || lhs->summary.key.frame != rhs->summary.key.frame) {
        s.error = "Within-frame comparison requires two retained events in the "
                  "same frame";
        return 0;
    }
    return s.Difference(a, b, CaptureInspectionRelation::WithinFrame,
                        changed_only);
}
uint64_t CaptureInspectionController::BeginPriorShaderUseComparison(
    uint64_t id, const ShaderKey &shader, bool changed_only)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto current = s.Find(id);
    if (!current || current->type != CaptureEventType::Draw ||
        !DrawUsesShader(current->summary, shader)) {
        s.error = "Selected occurrence does not use the requested shader";
        return 0;
    }
    uint64_t previous = 0;
    for (const auto &event : s.events) {
        if (event->event_id == id) {
            if (previous)
                return s.Difference(previous, id,
                                    CaptureInspectionRelation::PriorShaderUse,
                                    changed_only);
            break;
        }
        if (event->type == CaptureEventType::Draw &&
            DrawUsesShader(event->summary, shader))
            previous = event->event_id;
    }
    s.error = "Prior shader use is unavailable";
    return 0;
}
uint64_t CaptureInspectionController::BeginConfirmedTrackComparison(
    uint64_t id, uint64_t frame, bool changed_only)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto it = s.tracks.find(id);
    if (it == s.tracks.end()) {
        s.error = "Temporal track identity is unavailable";
        return 0;
    }
    uint64_t previous = 0;
    for (const auto &f : it->second.frames) {
        if (f.frame == frame) {
            if (f.state == CaptureTrackFrameState::Confirmed && previous)
                return s.Difference(previous, f.selected.event_id,
                                    CaptureInspectionRelation::ConfirmedTrack,
                                    changed_only, id);
            break;
        }
        if (f.frame < frame && f.state == CaptureTrackFrameState::Confirmed)
            previous = f.selected.event_id;
    }
    s.error = "A current and previous explicit confirmed frame selection are "
              "required";
    return 0;
}
uint64_t CaptureInspectionController::CreateTrack(uint64_t event_id,
                                                  const std::string &label,
                                                  bool confirmed)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto event = s.Find(event_id);
    if (!confirmed || !event || event->type != CaptureEventType::Draw ||
        s.tracks.size() == s.settings.maximum_tracks ||
        s.FrameCount() == s.settings.maximum_track_frames ||
        label.size() > 256) {
        s.error = "Explicit user confirmation and bounded retained draw "
                  "identity are required";
        return 0;
    }
    CaptureTemporalTrack track;
    track.track_id = s.NewTrackID();
    if (!track.track_id)
        return 0;
    track.revision = 1;
    track.recording = s.recording;
    track.label = label;
    track.frames.push_back({ event->summary.key.frame,
                             CaptureTrackFrameState::Confirmed,
                             s.Ref(*event),
                             {},
                             "User-confirmed occurrence" });
    const auto id = track.track_id;
    s.tracks.emplace(id, std::move(track));
    s.error.clear();
    return id;
}
bool CaptureInspectionController::Confirm(uint64_t id, uint64_t event_id,
                                          bool confirmed)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto it = s.tracks.find(id);
    auto event = s.Find(event_id);
    if (!confirmed || it == s.tracks.end() || !event ||
        event->type != CaptureEventType::Draw) {
        s.error = "Explicit user confirmation and retained track/event "
                  "identity are required";
        return false;
    }
    auto *frame = s.Frame(it->second, event->summary.key.frame);
    if (!frame)
        return false;
    if (frame->state == CaptureTrackFrameState::Confirmed &&
        frame->selected.event_id != event_id) {
        s.error = "This frame already has a confirmed selection; create an "
                  "explicit branch";
        return false;
    }
    frame->state = CaptureTrackFrameState::Confirmed;
    frame->selected = s.Ref(*event);
    frame->candidates.clear();
    frame->note = "User-confirmed occurrence";
    ++it->second.revision;
    s.error.clear();
    return true;
}
uint64_t CaptureInspectionController::Branch(uint64_t id, uint64_t event_id,
                                             const std::string &label,
                                             bool confirmed)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto parent = s.tracks.find(id);
    auto event = s.Find(event_id);
    if (!confirmed || parent == s.tracks.end() || !event ||
        event->type != CaptureEventType::Draw || label.size() > 256 ||
        s.tracks.size() == s.settings.maximum_tracks) {
        s.error = "Explicit branch confirmation, retained identity and "
                  "available budget are required";
        return 0;
    }
    CaptureTemporalTrack branch;
    branch.track_id = s.NewTrackID();
    if (!branch.track_id)
        return 0;
    branch.parent_track_id = id;
    branch.branch_frame = event->summary.key.frame;
    branch.revision = 1;
    branch.label = label;
    branch.recording = s.recording;
    size_t candidates = 0;
    for (const auto &f : parent->second.frames)
        if (f.frame < branch.branch_frame) {
            branch.frames.push_back(f);
            candidates += f.candidates.size();
        }
    if (s.FrameCount() + branch.frames.size() + 1 >
            s.settings.maximum_track_frames ||
        s.CandidateCount() + candidates > s.settings.maximum_track_candidates) {
        s.error = "Branch frame/candidate storage budget exceeded";
        return 0;
    }
    branch.frames.push_back({ branch.branch_frame,
                              CaptureTrackFrameState::Confirmed,
                              s.Ref(*event),
                              {},
                              "User-confirmed branch occurrence" });
    const auto result = branch.track_id;
    s.tracks.emplace(result, std::move(branch));
    s.error.clear();
    return result;
}
uint64_t CaptureInspectionController::BeginCandidates(uint64_t id,
                                                      uint64_t frame)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto track = s.tracks.find(id);
    if (track == s.tracks.end()) {
        s.error = "Temporal track identity is unavailable";
        return 0;
    }
    std::shared_ptr<const CaptureOccurrence> anchor;
    for (const auto &f : track->second.frames)
        if (f.frame < frame && f.state == CaptureTrackFrameState::Confirmed)
            anchor = s.Find(f.selected.event_id);
    if (!anchor || !CompleteInputs(*anchor)) {
        s.error =
            "A previous confirmed occurrence with finalized inputs is required";
        return 0;
    }
    auto *job = s.New(CaptureInspectionJobKind::Candidates);
    if (!job)
        return 0;
    job->page.track_id = id;
    job->page.track_revision = track->second.revision;
    job->page.before = s.Ref(*anchor);
    job->page.after.recording_id = s.recording.recording_id;
    job->page.after.frame = frame;
    job->anchor = anchor;
    try {
        job->before = Flatten(*anchor, s.settings.maximum_fields);
    } catch (const std::exception &e) {
        s.Terminal(*job, e.what());
    }
    for (const auto &event : s.events)
        if (event->summary.key.frame == frame &&
            event->type == CaptureEventType::Draw)
            job->scan.push_back(event);
    job->page.total = job->scan.size();
    return job->page.job_id;
}
uint64_t CaptureInspectionController::BeginTrend(uint64_t id,
                                                 const std::string &path,
                                                 uint64_t first, uint64_t last)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto track = s.tracks.find(id);
    if (track == s.tracks.end() || path.empty() || path.size() > 512 ||
        first > last) {
        s.error = "Invalid track, field path or trend range";
        return 0;
    }
    auto *job = s.New(CaptureInspectionJobKind::Trend);
    if (!job)
        return 0;
    job->page.track_id = id;
    job->page.track_revision = track->second.revision;
    job->field_path = path;
    job->page.relation = CaptureInspectionRelation::ConfirmedTrack;
    auto a = std::lower_bound(s.retained_frames.begin(),
                              s.retained_frames.end(), first);
    auto b = std::lower_bound(
        track->second.frames.begin(), track->second.frames.end(), first,
        [](const auto &f, uint64_t n) { return f.frame < n; });
    while ((a != s.retained_frames.end() && *a <= last) ||
           (b != track->second.frames.end() && b->frame <= last)) {
        const bool has_a = a != s.retained_frames.end() && *a <= last;
        const bool has_b = b != track->second.frames.end() && b->frame <= last;
        const auto frame = !has_b || (has_a && *a < b->frame) ? *a : b->frame;
        CaptureTrackFrame point;
        point.frame = frame;
        if (has_b && b->frame == frame) {
            point.state = b->state;
            point.selected = b->selected;
            ++b;
        }
        if (has_a && *a == frame)
            ++a;
        ++job->page.total;
        if (job->frames.size() < s.settings.maximum_track_frames)
            job->frames.push_back(std::move(point));
        else
            ++job->page.omitted_results;
    }
    if (job->page.omitted_results)
        s.Terminal(*job, "Trend frame budget exceeded; requested frame "
                         "coverage is counted explicitly");
    return job->page.job_id;
}
void CaptureInspectionController::Run(uint64_t id, uint32_t work)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto it = s.jobs.find(id);
    if (it == s.jobs.end())
        return;
    auto &job = it->second;
    if (job.page.state != CaptureInspectionJobState::Running)
        return;
    if (job.page.track_id &&
        s.tracks.at(job.page.track_id).revision != job.page.track_revision) {
        job.page.state = CaptureInspectionJobState::Cancelled;
        job.page.message = "Track selections changed; this job is stale";
        return;
    }
    for (uint32_t i = 0; i < std::min<uint32_t>(work, 4096) &&
                         job.page.state == CaptureInspectionJobState::Running;
         ++i) {
        if (job.page.kind == CaptureInspectionJobKind::Difference)
            s.DifferenceStep(job);
        else if (job.page.kind == CaptureInspectionJobKind::Candidates)
            s.CandidateStep(job);
        else
            s.TrendStep(job);
    }
}
CaptureInspectionPage CaptureInspectionController::Page(uint64_t id,
                                                        uint64_t offset,
                                                        uint32_t count) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto it = s.jobs.find(id);
    if (it == s.jobs.end())
        return {};
    const auto &job = it->second;
    auto page = job.page;
    page.offset = offset;
    count = std::min(count, s.settings.maximum_page);
    page.result_count = job.page.kind == CaptureInspectionJobKind::Difference ?
                            job.fields.size() :
                        job.page.kind == CaptureInspectionJobKind::Candidates ?
                            job.candidates.size() :
                            job.trend.size();
    const auto n = std::min<uint64_t>(
        count, offset < page.result_count ? page.result_count - offset : 0);
    for (uint64_t i = 0; i < n; ++i) {
        if (page.kind == CaptureInspectionJobKind::Difference)
            page.fields.push_back(job.fields[offset + i]);
        else if (page.kind == CaptureInspectionJobKind::Candidates)
            page.candidates.push_back(job.candidates[offset + i]);
        else
            page.trend.push_back(job.trend[offset + i]);
    }
    page.has_more =
        offset < page.result_count && n < page.result_count - offset;
    return page;
}
void CaptureInspectionController::Cancel(uint64_t id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->jobs.find(id);
    if (it != impl_->jobs.end() &&
        it->second.page.state == CaptureInspectionJobState::Running) {
        it->second.page.state = CaptureInspectionJobState::Cancelled;
        it->second.page.message = "Inspection job cancelled";
    }
}
void CaptureInspectionController::Drop(uint64_t id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->jobs.erase(id);
}
std::vector<CaptureTemporalTrack> CaptureInspectionController::Tracks() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<CaptureTemporalTrack> result;
    for (const auto &track : impl_->tracks)
        result.push_back(track.second);
    return result;
}
bool CaptureInspectionController::Impl::Restore(
    const std::vector<CaptureTemporalTrack> &tracks)
{
    auto &s = *this;
    auto fail = [&] {
        s.error = "Archived track annotations contain invalid identities or "
                  "exceed bounds";
        return false;
    };
    if (!s.recording.recording_id || tracks.size() > s.settings.maximum_tracks)
        return fail();
    std::map<uint64_t, CaptureTemporalTrack> restored;
    uint64_t frames = 0, candidates = 0;
    const auto &context = s.recording.context;
    auto valid_ref = [&](const CaptureInspectionEventRef &ref, uint64_t frame,
                         bool retained) {
        auto event = s.Find(ref.event_id);
        const bool domain = ref.recording_id == s.recording.recording_id &&
                            ref.event_id && ref.frame == frame &&
                            ref.key.frame == frame &&
                            ref.key.session_epoch == context.session_epoch &&
                            ref.key.renderer_epoch == context.renderer_epoch;
        if (!domain)
            return false;
        if (!retained)
            return !event || (event->type == CaptureEventType::Draw &&
                              ref == s.Ref(*event));
        return event && event->type == CaptureEventType::Draw &&
               ref == s.Ref(*event);
    };
    for (const auto &track : tracks) {
        const auto &c = track.recording.context;
        if (!track.track_id || track.track_id >= kCaptureSessionTokenBit ||
            !track.revision || track.revision == UINT64_MAX ||
            track.label.size() > 256 ||
            track.recording.recording_id != s.recording.recording_id ||
            !(c.scope == context.scope) ||
            c.scope_generation != context.scope_generation ||
            c.session_epoch != context.session_epoch ||
            c.renderer_epoch != context.renderer_epoch ||
            c.generation != context.generation ||
            c.backend != context.backend || restored.count(track.track_id) ||
            track.frames.empty() ||
            track.frames.size() > s.settings.maximum_track_frames - frames)
            return fail();
        uint64_t previous = 0;
        bool first = true;
        for (const auto &frame : track.frames) {
            if ((!first && frame.frame <= previous) ||
                frame.note.size() > 1024 ||
                uint32_t(frame.state) >
                    uint32_t(CaptureTrackFrameState::Incomplete))
                return fail();
            previous = frame.frame;
            first = false;
            ++frames;
            candidates += frame.candidates.size();
            if (frames > s.settings.maximum_track_frames ||
                candidates > s.settings.maximum_track_candidates ||
                frame.candidates.size() > s.settings.maximum_candidates)
                return fail();
            if (frame.state == CaptureTrackFrameState::Confirmed) {
                if (!valid_ref(frame.selected, frame.frame,
                               frame.evidence_retained) ||
                    !frame.candidates.empty())
                    return fail();
            } else if (frame.selected.event_id || frame.selected.recording_id ||
                       (frame.state == CaptureTrackFrameState::Candidate &&
                        frame.candidates.size() != 1) ||
                       (frame.state == CaptureTrackFrameState::Ambiguous &&
                        frame.candidates.size() < 2) ||
                       (frame.state == CaptureTrackFrameState::Missing &&
                        !frame.candidates.empty()))
                return fail();
            std::set<uint64_t> ids;
            for (const auto &candidate : frame.candidates)
                if (!valid_ref(candidate.event, frame.frame,
                               candidate.evidence_retained) ||
                    !ids.insert(candidate.event.event_id).second ||
                    (candidate.evidence &
                     ~(CaptureTrackSameGeometry | CaptureTrackSameUniformBits |
                       CaptureTrackSameTextureBits |
                       CaptureTrackSameRasterBits)))
                    return fail();
        }
        restored.emplace(track.track_id, track);
    }
    for (const auto &entry : restored) {
        const auto &annotation = entry.second;
        if (annotation.parent_track_id) {
            auto branch =
                std::find_if(annotation.frames.begin(), annotation.frames.end(),
                             [&](const auto &f) {
                                 return f.frame == annotation.branch_frame;
                             });
            if (branch == annotation.frames.end() ||
                branch->state != CaptureTrackFrameState::Confirmed)
                return fail();
        } else if (annotation.branch_frame)
            return fail();
        std::set<uint64_t> path;
        const auto *track = &entry.second;
        while (track->parent_track_id) {
            if (!path.insert(track->track_id).second)
                return fail();
            auto parent = restored.find(track->parent_track_id);
            if (parent == restored.end())
                return fail();
            track = &parent->second;
        }
    }
    // Archived IDs never advance a process-wide allocator. NewTrackID skips
    // any restored collision within the bounded track set.
    s.tracks = std::move(restored);
    for (auto &entry : s.jobs)
        if (entry.second.page.track_id &&
            entry.second.page.state == CaptureInspectionJobState::Running) {
            entry.second.page.state = CaptureInspectionJobState::Cancelled;
            entry.second.page.message =
                "Track annotations were restored; this job is stale";
        }
    s.error.clear();
    return true;
}
bool CaptureInspectionController::RestoreTracks(
    const std::vector<CaptureTemporalTrack> &tracks)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->Restore(tracks);
}
bool CaptureInspectionController::ExportAnnotations(std::string *output,
                                                    std::string *error) const
{
    if (error)
        error->clear();
    if (output)
        output->clear();
    auto fail = [&](const std::string &message) {
        if (error)
            *error = message.substr(0, 1024);
        return false;
    };
    if (!output)
        return fail("Annotation output destination is missing");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto &s = *impl_;
    if (!s.recording.recording_id)
        return fail("Annotation recording identity is unavailable");
    try {
        Json document = {
            { "format", "xemu-capture-annotations" },
            { "version", 1 },
            { "recording",
              { { "id", s.recording.recording_id },
                { "context", ContextJson(s.recording.context) } } },
            { "tracks", Json::array() }
        };
        for (const auto &entry : s.tracks) {
            const auto &t = entry.second;
            Json track = { { "id", t.track_id },
                           { "parent", t.parent_track_id },
                           { "branch_frame", t.branch_frame },
                           { "revision", t.revision },
                           { "context", ContextJson(t.recording.context) },
                           { "label", t.label },
                           { "frames", Json::array() } };
            for (const auto &f : t.frames) {
                Json frame = { { "frame", f.frame },
                               { "state", uint32_t(f.state) },
                               { "evidence_retained", f.evidence_retained },
                               { "selected", RefJson(f.selected) },
                               { "note", f.note },
                               { "candidates", Json::array() } };
                for (const auto &c : f.candidates)
                    frame["candidates"].push_back(
                        { { "event", RefJson(c.event) },
                          { "evidence", c.evidence },
                          { "evidence_retained", c.evidence_retained } });
                track["frames"].push_back(std::move(frame));
            }
            document["tracks"].push_back(std::move(track));
        }
        auto text = document.dump();
        if (text.size() > kAnnotationBytes)
            return fail("Annotation JSON exceeds the 4 MiB archive limit");
        *output = std::move(text);
        return true;
    } catch (const std::exception &e) {
        return fail(e.what());
    }
}
bool CaptureInspectionController::RestoreAnnotations(const std::string &text,
                                                     std::string *error)
{
    if (error)
        error->clear();
    auto fail = [&](const std::string &message) {
        if (error)
            *error = message.substr(0, 1024);
        return false;
    };
    if (text.empty() || text.size() > kAnnotationBytes || !SafeDepth(text))
        return fail(
            "Annotation JSON is empty, too large or excessively nested");
    try {
        const auto document = Json::parse(text);
        if (String(document, "format", 64) != "xemu-capture-annotations" ||
            Uint(document, "version") != 1)
            return fail("Annotation archive format or version is unsupported");
        CaptureInspectionRecording recording;
        recording.recording_id = Uint(document.at("recording"), "id");
        recording.context = ReadContext(document.at("recording").at("context"));
        const auto &array = document.at("tracks");
        if (!array.is_array() || array.size() > 1024)
            return fail("Annotation track count exceeds bounds");
        std::vector<CaptureTemporalTrack> tracks;
        size_t frames = 0, candidates = 0;
        for (const auto &node : array) {
            CaptureTemporalTrack track;
            track.recording = recording;
            track.recording.context = ReadContext(node.at("context"));
            track.track_id = Uint(node, "id");
            track.parent_track_id = Uint(node, "parent");
            track.branch_frame = Uint(node, "branch_frame");
            track.revision = Uint(node, "revision");
            track.label = String(node, "label", 256);
            const auto &fa = node.at("frames");
            if (!fa.is_array() || fa.size() > 4096 - frames)
                return fail("Annotation frame count exceeds bounds");
            frames += fa.size();
            for (const auto &fn : fa) {
                CaptureTrackFrame frame;
                frame.frame = Uint(fn, "frame");
                frame.state = CaptureTrackFrameState(Uint(
                    fn, "state", uint32_t(CaptureTrackFrameState::Incomplete)));
                frame.selected = ReadRef(fn.at("selected"));
                frame.evidence_retained = Boolean(fn, "evidence_retained");
                frame.note = String(fn, "note", 1024);
                const auto &ca = fn.at("candidates");
                if (!ca.is_array() || ca.size() > 1024 ||
                    ca.size() > 32768 - candidates)
                    return fail("Annotation candidate count exceeds bounds");
                candidates += ca.size();
                for (const auto &cn : ca)
                    frame.candidates.push_back(
                        { ReadRef(cn.at("event")),
                          uint32_t(Uint(cn, "evidence", UINT32_MAX)),
                          Boolean(cn, "evidence_retained") });
                track.frames.push_back(std::move(frame));
            }
            tracks.push_back(std::move(track));
        }
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto &s = *impl_;
        // The top-level owner is checked even when the archive has no tracks.
        if (recording.recording_id != s.recording.recording_id ||
            !SameContextIdentity(recording.context, s.recording.context))
            return fail("Annotation archive belongs to a different recording "
                        "or context");
        if (!s.Restore(tracks))
            return fail(s.error);
        return true;
    } catch (const std::exception &e) {
        return fail(e.what());
    }
}
std::string CaptureInspectionController::Error() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

} // namespace xemu::shader_browser
