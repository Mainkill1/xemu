    bool ReconcileArtifacts(size_t *removed, std::string *error) override
    {
        return database.ReconcileExternalArtifacts(removed, error);
    }

    bool PublishShader(const XemuShaderBrowserShaderRecord *wire,
                       std::string *error)
    {
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) {
            if (error) *error = "Shader Browser provider is not installed";
            return false;
        }
        if (!wire || !wire->identity_version ||
            wire->stage == XEMU_SHADER_BROWSER_STAGE_UNKNOWN ||
            !wire->recipe_format_version || !wire->recipe_data ||
            !wire->recipe_size || wire->recipe_size > kMaxCanonicalRecipeBytes) {
            if (error) *error = "Invalid canonical shader publication";
            return false;
        }
        ShaderRecord record{};
        record.key.stage = ConvertStage(wire->stage);
        record.recipe_format_version = wire->recipe_format_version;
        record.recipe.assign(wire->recipe_data,
                             wire->recipe_data + wire->recipe_size);
        record.scope = ConvertScope(wire->scope);
        ShaderHash expected = ComputeShaderHash(
            wire->identity_version, record.key.stage,
            wire->recipe_format_version, wire->recipe_data, wire->recipe_size);
        record.key.hash = ConvertHash(wire->identity_version,
                                      wire->identity_hash);
        if (!expected.version || record.key.hash != expected) {
            if (error) {
                *error = "Published ShaderHash does not match the canonical "
                         "recipe identity frame";
            }
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(discovered_mutex);
            auto [it, inserted] = discovered_records.try_emplace(record.key);
            TransientRecord &stored = it->second;
            if (inserted) {
                stored.record = record;
            } else if (stored.record.recipe_format_version !=
                           record.recipe_format_version ||
                       stored.record.recipe != record.recipe) {
                if (error) {
                    *error = "Shader hash collision: same 96-bit hash/stage "
                             "has different canonical recipe identity";
                }
                return false;
            }
            AddScope(&stored.scopes, record.scope);
        }
        if (database.Enabled()) return database.UpsertShader(record, error);
        return true;
    }

    bool CollectionRequired() const
    {
        if (live_collection_enabled.load(std::memory_order_acquire)) return true;
        std::lock_guard<std::mutex> lock(performance_mutex);
        return database.RecordPerformanceSessions() &&
               !performance_session_id.empty();
    }

    bool ExternalArtifactsEnabled() const
    {
        return database.SaveExternalArtifacts();
    }

    void PublishObservations(const XemuShaderBrowserObservation *observations,
                             size_t count)
    {
        if (!observations || !count) return;
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed || !CollectionRequired()) return;

        bool live = live_collection_enabled.load(std::memory_order_acquire);
        if (database.RecordPerformanceSessions()) {
            std::lock_guard<std::mutex> lock(performance_mutex);
            if (!performance_session_id.empty()) {
                for (size_t i = 0; i < count; ++i) {
                    AggregatePerformance(observations[i]);
                }
            }
        }
        if (!live) return;

        uint64_t epoch = live_epoch.load(std::memory_order_acquire);
        std::lock_guard<std::mutex> lock(live_queue_mutex);
        size_t old_size = live_events.size();
        if (count > live_events.max_size() - old_size) return;
        live_events.resize(old_size + count);
        for (size_t i = 0; i < count; ++i) {
            QueuedEvent &event = live_events[old_size + i];
            event.kind = EventKind::Observation;
            event.epoch = epoch;
            event.sequence = ++next_event_sequence;
            event.observation = observations[i];
        }
    }

    void PublishFrame(uint64_t frame)
    {
        if (!live_collection_enabled.load(std::memory_order_acquire)) return;
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) return;
        QueuedEvent event{};
        event.kind = EventKind::Frame;
        event.epoch = live_epoch.load(std::memory_order_acquire);
        event.frame = frame;
        std::lock_guard<std::mutex> lock(live_queue_mutex);
        event.sequence = ++next_event_sequence;
        live_events.push_back(event);
    }

    bool BeginPerformanceSession(
        const XemuShaderBrowserPerformanceSession *wire, std::string *error)
    {
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!wire || !wire->session_id || !wire->session_id[0] ||
            !wire->title_id) {
            if (error) *error = "Invalid performance session";
            return false;
        }
        if (!database.RecordPerformanceSessions()) {
            if (error) *error = "Per-session shader statistics are disabled";
            return false;
        }
        SessionDescriptor session{};
        session.session_id = wire->session_id;
        session.title_id = wire->title_id;
        session.executable_fingerprint_version =
            wire->executable_fingerprint_version;
        std::memcpy(session.executable_fingerprint.data(),
                    wire->executable_fingerprint,
                    session.executable_fingerprint.size());
        session.started_unix_ms = wire->started_unix_ms;
        session.xemu_revision = wire->xemu_revision ? wire->xemu_revision : "";
        session.renderer = wire->renderer ? wire->renderer : "";
        session.host_os = wire->host_os ? wire->host_os : "";
        session.cpu_model = wire->cpu_model ? wire->cpu_model : "";
        session.gpu_name = wire->gpu_name ? wire->gpu_name : "";
        session.gpu_vendor_id = wire->gpu_vendor_id;
        session.gpu_device_id = wire->gpu_device_id;
        session.gpu_driver = wire->gpu_driver ? wire->gpu_driver : "";
        session.internal_resolution_scale = wire->internal_resolution_scale;
        session.ubershader_mode =
            wire->ubershader_mode ? wire->ubershader_mode : "";
        session.shader_cache_enabled = wire->shader_cache_enabled != 0;
        session.telemetry_version = wire->telemetry_version;
        std::lock_guard<std::mutex> lock(performance_mutex);
        if (!database.BeginPerformanceSession(session, error)) return false;
        performance_entries.clear();
        performance_session_id = session.session_id;
        return true;
    }

    bool EndPerformanceSession(const char *session_id, uint64_t ended_unix_ms,
                               bool clean_shutdown, std::string *error)
    {
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!session_id || !session_id[0]) {
            if (error) *error = "Performance session ID is empty";
            return false;
        }
        std::lock_guard<std::mutex> lock(performance_mutex);
        if (performance_session_id != session_id) {
            if (error) *error = "Performance session is not active";
            return false;
        }
        PersistPerformanceSnapshotLocked();
        bool ok = database.EndPerformanceSession(session_id, ended_unix_ms,
                                                  clean_shutdown, error);
        performance_session_id.clear();
        performance_entries.clear();
        return ok;
    }
