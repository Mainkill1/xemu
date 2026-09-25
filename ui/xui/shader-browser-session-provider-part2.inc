        if (db_config.enabled) {
            std::vector<ShaderRecord> discovered;
            {
                std::lock_guard<std::mutex> lock(discovered_mutex);
                for (const auto &pair : discovered_records) {
                    const TransientRecord &stored = pair.second;
                    if (stored.scopes.empty()) {
                        discovered.push_back(stored.record);
                    } else {
                        for (const ShaderScope &scope : stored.scopes) {
                            ShaderRecord copy = stored.record;
                            copy.scope = scope;
                            discovered.push_back(std::move(copy));
                        }
                    }
                }
            }
            for (const ShaderRecord &record : discovered) {
                std::string ignored;
                database.UpsertShader(record, &ignored);
            }
        } else {
            std::lock_guard<std::mutex> lock(performance_mutex);
            performance_entries.clear();
            performance_session_id.clear();
        }
        return true;
    }

    bool CopySnapshot(Snapshot *snapshot) override
    {
        if (!snapshot) return false;
        snapshot->entries.clear();
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) return false;

        std::vector<QueuedEvent> events;
        {
            std::lock_guard<std::mutex> lock(live_queue_mutex);
            events.swap(live_events);
        }
        ApplyLiveEpoch(live_epoch.load(std::memory_order_acquire));
        for (const QueuedEvent &event : events) {
            if (event.epoch < live_consumer_epoch) continue;
            if (event.epoch > live_consumer_epoch) ApplyLiveEpoch(event.epoch);
            if (event.kind == EventKind::Frame) {
                current_frame = std::max(current_frame, event.frame);
            } else {
                AggregateLive(event);
            }
        }

        std::unordered_map<ShaderKey, Entry, ShaderKeyHash> merged;
        if (database.Enabled()) {
            for (DatabaseShaderMetadata &metadata : database.CopyMetadata()) {
                Entry entry{};
                entry.key = metadata.key;
                entry.recipe_format_version = metadata.recipe_format_version;
                entry.recipe_size = metadata.recipe_size;
                entry.scopes = std::move(metadata.scopes);
                entry.stored_in_database = true;
                merged.emplace(entry.key, std::move(entry));
            }
        }
        {
            std::lock_guard<std::mutex> lock(discovered_mutex);
            for (const auto &pair : discovered_records) {
                auto [it, inserted] = merged.try_emplace(pair.first);
                Entry &entry = it->second;
                if (inserted) entry.key = pair.first;
                entry.recipe_format_version = pair.second.record.recipe_format_version;
                entry.recipe_size = pair.second.record.recipe.size();
                for (const ShaderScope &scope : pair.second.scopes) {
                    AddScope(&entry.scopes, scope);
                }
                if (database.Enabled()) entry.stored_in_database = true;
            }
        }
        for (const auto &pair : live_entries) {
            auto [it, inserted] = merged.try_emplace(pair.first);
            Entry &entry = it->second;
            if (inserted) entry.key = pair.first;
            Entry live = pair.second.entry;
            entry.draw_count = live.draw_count;
            entry.first_frame = live.first_frame;
            entry.last_frame = live.last_frame;
            entry.pipeline_variant_count = live.pipeline_variant_count;
            entry.observed_in_session = live.observed_in_session;
            entry.status = live.status;
            entry.route = live.route;
            entry.readiness = live.readiness;
            entry.compile_cpu = live.compile_cpu;
            entry.pipeline_cpu = live.pipeline_cpu;
            entry.prepare_cpu = live.prepare_cpu;
            entry.foreground_stall_cpu = live.foreground_stall_cpu;
            entry.gpu_execution = live.gpu_execution;
            entry.specialized_draws = live.specialized_draws;
            entry.uber_draws = live.uber_draws;
            entry.replacement_draws = live.replacement_draws;
            entry.compile_failures = live.compile_failures;
        }

        DatabaseStats db_stats = database.GetStats();
        snapshot->snapshot_sequence = ++snapshot_sequence;
        snapshot->session_epoch = live_consumer_epoch;
        snapshot->current_frame = current_frame;
        snapshot->capabilities = CapabilityDetails;
        snapshot->live_collection_enabled =
            live_collection_enabled.load(std::memory_order_acquire);
        snapshot->renderer_collection_required = CollectionRequired();
        {
            std::lock_guard<std::mutex> lock(live_queue_mutex);
            snapshot->pending_observation_count = live_events.size();
        }
        snapshot->database_enabled = db_stats.enabled;
        snapshot->database_open = db_stats.open;
        snapshot->record_performance_sessions =
            db_stats.record_performance_sessions;
        snapshot->save_external_artifacts = db_stats.save_external_artifacts;
        snapshot->database_write_failed = db_stats.write_failed;
        snapshot->database_schema_version = db_stats.schema_version;
        snapshot->stored_shader_count = db_stats.shader_count;
        snapshot->title_count = db_stats.title_count;
        snapshot->session_count = db_stats.session_count;
        snapshot->session_stat_count = db_stats.session_stat_count;
        snapshot->artifact_count = db_stats.artifact_count;
        snapshot->pending_database_writes = db_stats.pending_writes;
        snapshot->database_bytes = db_stats.database_bytes;
        snapshot->database_wal_bytes = db_stats.wal_bytes;
        snapshot->external_artifact_bytes = db_stats.external_artifact_bytes;
        snapshot->database_path = db_stats.database_path;
        snapshot->artifact_root = db_stats.artifact_root;
        snapshot->database_status = db_stats.status;
        snapshot->database_journal_mode = db_stats.journal_mode;
        snapshot->entries.reserve(merged.size());
        for (auto &pair : merged) snapshot->entries.push_back(std::move(pair.second));
        return true;
    }

    void SetLiveCollectionEnabled(bool enabled) override
    {
        std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
        live_collection_enabled.store(installed && enabled,
                                      std::memory_order_release);
    }

    void ClearLiveSession() override
    {
        std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
        if (installed) live_epoch.fetch_add(1, std::memory_order_acq_rel);
    }

    bool FlushDatabase(std::string *error) override
    {
        PersistPerformanceSnapshot();
        FlushArtifactQueue();
        return database.Flush(error);
    }

    bool QuickCheckDatabase(std::string *result,
                            std::string *error) override
    {
        return database.QuickCheck(result, error);
    }

    bool OptimizeDatabase(std::string *error) override
    {
        return database.Optimize(error);
    }

    bool ClearPerformanceHistory(std::string *error) override
    {
        {
            std::lock_guard<std::mutex> lock(performance_mutex);
            if (!performance_session_id.empty()) {
                if (error) {
                    *error = "End the active performance session before "
                             "deleting performance history";
                }
                return false;
            }
        }
        return database.ClearPerformanceHistory(error);
    }

