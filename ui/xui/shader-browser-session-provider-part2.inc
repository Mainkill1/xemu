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
        snapshot->binding_variants.clear();
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) return false;
        std::lock_guard<std::mutex> sample_state(sample_state_mutex);

        // XUI is the sole snapshot consumer; reuse both queue allocations.
        auto &events = snapshot_events;
        events.clear();
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
        DrainPerformanceSamples();

        uint32_t title_id = 0;
        {
            std::lock_guard<std::mutex> lock(scope_mutex);
            title_id = current_scope.title_id;
        }
        std::string selected_session;
        {
            std::lock_guard<std::mutex> lock(timing_source_mutex);
            selected_session = selected_timing_session;
        }
        snapshot->timing_sessions.clear();
        if (database.Enabled() && title_id) {
            snapshot->timing_sessions = database.CopySessions(title_id, 64);
        }
        if (!selected_session.empty() &&
            std::none_of(snapshot->timing_sessions.begin(),
                         snapshot->timing_sessions.end(),
                         [&](const SessionSummary &session) {
                return session.session_id == selected_session;
            })) selected_session.clear();
        snapshot->timing_session_id = selected_session;

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
        if (selected_session.empty()) for (const auto &pair : live_entries) {
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
            entry.source_cpu = live.source_cpu;
            entry.module_cpu = live.module_cpu;
            entry.pipeline_cpu = live.pipeline_cpu;
            entry.prepare_cpu = live.prepare_cpu;
            entry.foreground_stall_cpu = live.foreground_stall_cpu;
            entry.gpu_execution = live.gpu_execution;
            entry.specialized_draws = live.specialized_draws;
            entry.uber_draws = live.uber_draws;
            entry.replacement_draws = live.replacement_draws;
            entry.compile_failures = live.compile_failures;
        }
        if (!selected_session.empty()) {
            {
                std::lock_guard<std::mutex> lock(performance_mutex);
                if (selected_session == performance_session_id) {
                    PersistPerformanceSnapshotLocked();
                }
            }
            for (const ShaderSessionStats &row :
                 database.CopySessionStats(selected_session)) {
                auto [it, inserted] = merged.try_emplace(row.key);
                Entry &entry = it->second;
                if (inserted) entry.key = row.key;
                entry.draw_count = row.draw_count;
                entry.first_frame = row.first_frame;
                entry.last_frame = row.last_frame;
                entry.observed_in_session = row.draw_count != 0;
                entry.specialized_draws = row.specialized_draws;
                entry.uber_draws = row.uber_draws;
                entry.replacement_draws = row.replacement_draws;
                entry.compile_failures = row.compile_failures;
            }
            std::map<VariantKey, BindingVariant> recorded_variants;
            for (const PerformanceAggregate &row :
                 database.CopyPerformanceAggregates(selected_session)) {
                if (row.owner == XEMU_SHADER_BROWSER_PERF_STAGE &&
                    row.members.size() == 1 && row.metric >= 1 &&
                    row.metric <= 3) {
                    auto [it, inserted] = merged.try_emplace(row.members[0]);
                    Entry &entry = it->second;
                    if (inserted) entry.key = row.members[0];
                    DurationStats *duration = row.metric ==
                        XEMU_SHADER_BROWSER_PERF_SOURCE_CPU ?
                            &entry.source_cpu : row.metric ==
                        XEMU_SHADER_BROWSER_PERF_COMPILE_CPU ?
                            &entry.compile_cpu : &entry.module_cpu;
                    duration->Merge(row.duration);
                } else if (row.owner ==
                               XEMU_SHADER_BROWSER_PERF_BINDING &&
                           row.metric >= 4 && row.metric <= 8) {
                    VariantKey key{row.backend, row.route, row.variant_id,
                                   row.flags};
                    BindingVariant &variant = recorded_variants[key];
                    variant.backend = row.backend;
                    variant.route = ConvertRoute(row.route);
                    variant.variant_id = row.variant_id;
                    variant.members = row.members;
                    if (!variant.first_frame ||
                        row.first_frame < variant.first_frame) {
                        variant.first_frame = row.first_frame;
                    }
                    variant.last_frame = std::max(variant.last_frame,
                                                  row.last_frame);
                    variant.flags |= row.flags;
                    if (row.metric ==
                        XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU) {
                        variant.sampled_cpu_draws += row.represented_draws;
                    } else if (row.metric ==
                               XEMU_SHADER_BROWSER_PERF_DRAW_GPU) {
                        variant.sampled_gpu_draws += row.represented_draws;
                    }
                    variant.timing[row.metric].Merge(row.duration);
                }
            }
            for (const auto &pair : recorded_variants) {
                snapshot->binding_variants.push_back(pair.second);
            }
        }

        DatabaseStats db_stats = database.GetStats();
        snapshot->snapshot_sequence = ++snapshot_sequence;
        snapshot->session_epoch = live_consumer_epoch;
        snapshot->current_frame = current_frame;
        if (!selected_session.empty()) {
            snapshot->current_frame = 0;
            for (const auto &pair : merged) {
                snapshot->current_frame = std::max(
                    snapshot->current_frame, pair.second.last_frame);
            }
        }
        snapshot->dropped_performance_samples =
            dropped_performance_samples.load(std::memory_order_relaxed);
        snapshot->pending_performance_samples = sample_queue.Pending();
        snapshot->gl_gpu_supported =
            gl_gpu_supported.load(std::memory_order_relaxed);
        snapshot->vk_gpu_supported =
            vk_gpu_supported.load(std::memory_order_relaxed);
        snapshot->pending_gl_gpu_queries =
            gl_gpu_pending.load(std::memory_order_relaxed);
        snapshot->pending_vk_gpu_queries =
            vk_gpu_pending.load(std::memory_order_relaxed);
        if (selected_session.empty()) {
            for (const auto &pair : live_binding_variants) {
                snapshot->binding_variants.push_back(pair.second);
            }
        }
        snapshot->current_title_id = title_id;
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
        snapshot->rejected_artifact_count =
            rejected_artifacts.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock(artifact_mutex);
            snapshot->failed_artifact_count = failed_artifact_jobs;
            snapshot->last_artifact_error = last_artifact_error;
        }
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

    bool CopyCanonicalRecipe(const ShaderKey &key,
                             CanonicalRecipe *recipe) override
    {
        if (!recipe) return false;
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) return false;
        ShaderRecord record{};
        std::vector<ShaderScope> scopes;
        {
            std::lock_guard<std::mutex> lock(discovered_mutex);
            auto it = discovered_records.find(key);
            if (it != discovered_records.end()) {
                record = it->second.record;
                scopes = it->second.scopes;
            }
        }
        if (record.recipe.empty() &&
            !database.CopyRecord(key, &record, &scopes)) return false;
        recipe->key = record.key;
        recipe->recipe_format_version = record.recipe_format_version;
        recipe->bytes = std::move(record.recipe);
        recipe->scopes = std::move(scopes);
        return true;
    }

    void SetLiveCollectionEnabled(bool enabled) override
    {
        std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
        live_collection_enabled.store(installed && enabled,
                                      std::memory_order_release);
    }

    void SetTimingSession(const std::string &session_id) override
    {
        std::lock_guard<std::mutex> lock(timing_source_mutex);
        selected_timing_session = session_id;
    }

    void ClearLiveSession() override
    {
        std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
        if (installed) live_epoch.fetch_add(1, std::memory_order_acq_rel);
    }

    bool FlushDatabase(std::string *error) override
    {
        PersistPerformanceSnapshot();
        std::string artifact_error;
        std::string database_error;
        bool artifacts_ok = FlushArtifactQueue(&artifact_error);
        bool database_ok = database.Flush(&database_error);
        if (error) {
            *error = artifacts_ok ? std::string{} : artifact_error;
            if (!database_ok) {
                if (!error->empty()) *error += "; ";
                *error += database_error;
            }
        }
        return artifacts_ok && database_ok;
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
