    mutable std::shared_mutex lifecycle_mutex;
    bool installed = false;
    std::string base_path;

    ShaderDatabase database;
    ShaderArtifactStore artifact_store;

    std::mutex discovered_mutex;
    std::unordered_map<ShaderKey, TransientRecord, ShaderKeyHash>
        discovered_records;

    std::atomic<bool> live_collection_enabled{false};
    std::atomic<uint64_t> live_epoch{1};
    std::mutex live_queue_mutex;
    std::vector<QueuedEvent> live_events;
    uint64_t next_event_sequence = 0;
    std::unordered_map<ShaderKey, LiveSessionEntry, ShaderKeyHash> live_entries;
    uint64_t live_consumer_epoch = 1;
    uint64_t current_frame = 0;
    uint64_t snapshot_sequence = 0;

    mutable std::mutex performance_mutex;
    std::string performance_session_id;
    std::unordered_map<ShaderKey, ShaderSessionStats, ShaderKeyHash>
        performance_entries;
    std::mutex maintenance_mutex;
    std::condition_variable maintenance_cv;
    std::thread maintenance_thread;
    bool maintenance_stop = false;

    std::mutex artifact_mutex;
    std::condition_variable artifact_cv;
    std::condition_variable artifact_flush_cv;
    std::deque<ArtifactJob> artifact_jobs;
    std::thread artifact_thread;
    bool artifact_stop = false;
    bool artifact_in_flight = false;
};

SessionProvider provider;

void CopyError(char *destination, size_t destination_size,
               const std::string &error)
{
    if (!destination || !destination_size) return;
    size_t size = std::min(destination_size - 1, error.size());
    std::memcpy(destination, error.data(), size);
    destination[size] = 0;
}

} // namespace

Provider &GetProvider() { return provider; }

} // namespace xemu::shader_browser

extern "C" {

int xemu_shader_browser_session_install(const char *base_path)
{
    return xemu::shader_browser::provider.Install(base_path) ? 1 : 0;
}

void xemu_shader_browser_session_uninstall(void)
{
    xemu::shader_browser::provider.Uninstall();
}

int xemu_shader_browser_compute_shader_hash(
    uint32_t identity_version, uint32_t stage,
    uint32_t recipe_format_version, const uint8_t *recipe_data,
    size_t recipe_size, uint8_t out_hash[XEMU_SHADER_BROWSER_HASH_BYTES])
{
    if (!out_hash) return 0;
    xemu::shader_browser::Stage cpp_stage =
        xemu::shader_browser::ConvertStage(stage);
    xemu::shader_browser::ShaderHash hash =
        xemu::shader_browser::ComputeShaderHash(
            identity_version, cpp_stage, recipe_format_version,
            recipe_data, recipe_size);
    if (!hash.version) return 0;
    std::memcpy(out_hash, hash.bytes.data(), hash.bytes.size());
    return 1;
}

int xemu_shader_browser_database_configure(int enabled,
                                           int record_performance_sessions,
                                           int save_external_artifacts,
                                           char *error, size_t error_size)
{
    std::string message;
    bool ok = xemu::shader_browser::provider.ConfigurePersistence(
        enabled != 0, record_performance_sessions != 0,
        save_external_artifacts != 0, &message);
    xemu::shader_browser::CopyError(error, error_size, message);
    return ok ? 1 : 0;
}

int xemu_shader_browser_publish_shader(
    const XemuShaderBrowserShaderRecord *record)
{
    std::string error;
    return xemu::shader_browser::provider.PublishShader(record, &error) ? 1 : 0;
}

int xemu_shader_browser_session_collection_enabled(void)
{
    return xemu::shader_browser::provider.CollectionRequired() ? 1 : 0;
}

void xemu_shader_browser_publish_observations(
    const XemuShaderBrowserObservation *observations, size_t count)
{
    xemu::shader_browser::provider.PublishObservations(observations, count);
}

void xemu_shader_browser_publish_frame(uint64_t frame)
{
    xemu::shader_browser::provider.PublishFrame(frame);
}

void xemu_shader_browser_session_clear_live(void)
{
    xemu::shader_browser::provider.ClearLiveSession();
}

int xemu_shader_browser_performance_session_begin(
    const XemuShaderBrowserPerformanceSession *session,
    char *error, size_t error_size)
{
    std::string message;
    bool ok = xemu::shader_browser::provider.BeginPerformanceSession(
        session, &message);
    xemu::shader_browser::CopyError(error, error_size, message);
    return ok ? 1 : 0;
}

int xemu_shader_browser_performance_session_end(
    const char *session_id, uint64_t ended_unix_ms, int clean_shutdown,
    char *error, size_t error_size)
{
    std::string message;
    bool ok = xemu::shader_browser::provider.EndPerformanceSession(
        session_id, ended_unix_ms, clean_shutdown != 0, &message);
    xemu::shader_browser::CopyError(error, error_size, message);
    return ok ? 1 : 0;
}

int xemu_shader_browser_publish_external_artifact(
    const XemuShaderBrowserExternalArtifact *artifact)
{
    return xemu::shader_browser::provider.PublishExternalArtifact(artifact) ?
        1 : 0;
}

int xemu_shader_browser_flush_database(char *error, size_t error_size)
{
    std::string message;
    bool ok = xemu::shader_browser::provider.FlushDatabase(&message);
    xemu::shader_browser::CopyError(error, error_size, message);
    return ok ? 1 : 0;
}

} // extern "C"
