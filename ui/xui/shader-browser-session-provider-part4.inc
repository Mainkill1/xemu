    bool PublishExternalArtifact(
        const XemuShaderBrowserExternalArtifact *wire)
    {
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed || !wire || !database.SaveExternalArtifacts() ||
            !wire->data || !wire->size ||
            !wire->title_id || !wire->backend || !wire->route ||
            !wire->kind || !wire->extension) {
            return false;
        }
        if (wire->size > kMaxExternalArtifactBytes) {
            rejected_artifacts.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        ArtifactJob job{};
        job.write.key.hash = ConvertHash(wire->identity_version,
                                         wire->identity_hash);
        job.write.key.stage = ConvertStage(wire->stage);
        job.write.title_id = wire->title_id;
        job.write.backend = wire->backend;
        job.write.route = wire->route;
        job.write.kind = wire->kind;
        job.write.extension = wire->extension;
        job.write.generator_abi = wire->generator_abi;
        job.write.xemu_revision = wire->xemu_revision ? wire->xemu_revision : "";
        job.write.content_hash = wire->content_hash ? wire->content_hash : "";
        {
            std::lock_guard<std::mutex> lock(artifact_mutex);
            if (artifact_pending_jobs >= kMaxPendingArtifactJobs ||
                wire->size > kMaxPendingArtifactBytes -
                                 artifact_pending_bytes) {
                rejected_artifacts.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            job.write.bytes.assign(wire->data, wire->data + wire->size);
            artifact_pending_bytes += wire->size;
            ++artifact_pending_jobs;
            artifact_jobs.push_back(std::move(job));
        }
        artifact_cv.notify_one();
        return true;
    }

private:
    void ApplyLiveEpoch(uint64_t epoch)
    {
        if (epoch <= live_consumer_epoch) return;
        live_entries.clear();
        live_binding_variants.clear();
        live_stage_profiles.clear();
        current_frame = 0;
        live_consumer_epoch = epoch;
    }

    void DrainPerformanceSamples()
    {
        QueuedPerformanceSample queued;
        const uint64_t generation =
            scope_generation.load(std::memory_order_acquire);
        const uint64_t record_epoch =
            performance_epoch.load(std::memory_order_acquire);
        const bool recording =
            performance_active.load(std::memory_order_acquire);
        std::unique_lock<std::mutex> performance_lock(performance_mutex,
                                                       std::defer_lock);
        if (recording) performance_lock.lock();
        while (sample_queue.Pop(&queued)) {
            const auto &sample = queued.sample;
            if (sample.scope_generation != generation) continue;
            std::array<ShaderKey, 3> members{};
            bool valid = true;
            for (uint32_t i = 0; i < sample.identity_count; ++i) {
                const auto &identity = sample.identities[i];
                members[i] = {ConvertHash(identity.version, identity.hash),
                              ConvertStage(identity.stage)};
                if (!identity.version || members[i].stage == Stage::Unknown) {
                    valid = false;
                }
            }
            if (!valid) continue;
            if (recording && queued.performance_epoch == record_epoch) {
                std::vector<ShaderKey> record_members(
                    members.begin(), members.begin() + sample.identity_count);
                PerformanceKey key{sample.owner, sample.backend,
                                   sample.route, sample.variant_id,
                                   sample.metric, sample.flags,
                                   record_members};
                PerformanceAggregateEntry &record =
                    performance_aggregates[key];
                PerformanceAggregate &aggregate = record.aggregate;
                if (!aggregate.duration.samples) {
                    aggregate.owner = sample.owner;
                    aggregate.backend = sample.backend;
                    aggregate.route = sample.route;
                    aggregate.variant_id = sample.variant_id;
                    aggregate.metric = sample.metric;
                    aggregate.flags = sample.flags;
                    aggregate.members = std::move(record_members);
                    aggregate.first_frame = sample.frame;
                }
                aggregate.last_frame = std::max(aggregate.last_frame,
                                                sample.frame);
                aggregate.first_frame = std::min(aggregate.first_frame,
                                                 sample.frame);
                aggregate.represented_draws += sample.represented_draws;
                aggregate.duration.AddSample(sample.duration_ns);
                record.needs_enqueue = true;
            }
            if (!queued.live_requested ||
                queued.live_epoch != live_consumer_epoch) continue;
            if (sample.owner == XEMU_SHADER_BROWSER_PERF_STAGE) {
                StageProfileKey profile_key{members[0], sample.backend,
                                            sample.route, sample.flags};
                StageProfile &profile = live_stage_profiles[profile_key];
                profile.key = members[0];
                profile.backend = sample.backend;
                profile.route = ConvertRoute(sample.route);
                profile.flags = sample.flags;
                profile.timing[sample.metric].AddSample(sample.duration_ns);
                auto [it, inserted] = live_entries.try_emplace(members[0]);
                Entry &entry = it->second.entry;
                if (inserted) entry.key = members[0];
                DurationStats *duration =
                    sample.metric == XEMU_SHADER_BROWSER_PERF_SOURCE_CPU ?
                        &entry.source_cpu :
                    sample.metric == XEMU_SHADER_BROWSER_PERF_COMPILE_CPU ?
                        &entry.compile_cpu : &entry.module_cpu;
                duration->AddSample(sample.duration_ns);
            } else {
                VariantKey key{sample.backend, sample.route,
                               sample.variant_id, sample.flags};
                BindingVariant &variant = live_binding_variants[key];
                variant.backend = sample.backend;
                variant.route = ConvertRoute(sample.route);
                variant.variant_id = sample.variant_id;
                if (variant.members.empty()) {
                    variant.members.assign(members.begin(),
                                           members.begin() +
                                           sample.identity_count);
                    variant.first_frame = sample.frame;
                }
                if (!std::equal(variant.members.begin(),
                                variant.members.end(), members.begin(),
                                members.begin() + sample.identity_count)) {
                    dropped_performance_samples.fetch_add(
                        1, std::memory_order_relaxed);
                    continue;
                }
                variant.first_frame = std::min(variant.first_frame,
                                               sample.frame);
                variant.last_frame = std::max(variant.last_frame,
                                              sample.frame);
                variant.flags |= sample.flags;
                if (sample.metric ==
                    XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU) {
                    variant.sampled_cpu_draws += sample.represented_draws;
                } else if (sample.metric ==
                           XEMU_SHADER_BROWSER_PERF_DRAW_GPU) {
                    variant.sampled_gpu_draws += sample.represented_draws;
                }
                variant.timing[sample.metric].AddSample(sample.duration_ns);
            }
        }
    }

    static void AggregateObservationIntoEntry(
        Entry *entry, const XemuShaderBrowserObservation &observation)
    {
        bool observed = observation.flags & XEMU_SHADER_BROWSER_OBSERVED;
        if (observed) {
            if (!entry->observed_in_session) {
                entry->first_frame = observation.first_frame;
            } else {
                entry->first_frame = std::min(entry->first_frame,
                                              observation.first_frame);
            }
            entry->last_frame = std::max(entry->last_frame,
                                         observation.last_frame);
            entry->draw_count += observation.draw_count_delta;
            entry->observed_in_session = true;
        }
        entry->specialized_draws += observation.specialized_draw_delta;
        entry->uber_draws += observation.uber_draw_delta;
        entry->replacement_draws += observation.replacement_draw_delta;
        entry->compile_failures += observation.compile_failure_delta;
        entry->compile_cpu.Merge(ConvertDuration(observation.compile_cpu));
        entry->pipeline_cpu.Merge(ConvertDuration(observation.pipeline_cpu));
        entry->prepare_cpu.Merge(ConvertDuration(observation.prepare_cpu));
        entry->foreground_stall_cpu.Merge(
            ConvertDuration(observation.foreground_stall_cpu));
        entry->gpu_execution.Merge(ConvertDuration(observation.gpu_execution));
        entry->pipeline_variant_count = std::max(
            entry->pipeline_variant_count, observation.pipeline_variant_count);
        entry->status = ConvertStatus(observation.status);
        entry->route = ConvertRoute(observation.route);
        entry->readiness = ConvertReadiness(observation.readiness);
    }

    void AggregateLive(const QueuedEvent &event)
    {
        const XemuShaderBrowserObservation &observation = event.observation;
        if (!observation.identity_version) return;
        ShaderKey key{ConvertHash(observation.identity_version,
                                  observation.identity_hash),
                      ConvertStage(observation.stage)};
        auto [it, inserted] = live_entries.try_emplace(key);
        LiveSessionEntry &live = it->second;
        if (inserted) live.entry.key = key;
        AggregateObservationIntoEntry(&live.entry, observation);
        current_frame = std::max(current_frame, observation.last_frame);
        live.last_event_sequence = std::max(live.last_event_sequence,
                                            event.sequence);
    }

    void AggregatePerformance(const XemuShaderBrowserObservation &observation)
    {
        if (!observation.identity_version) return;
        ShaderKey key{ConvertHash(observation.identity_version,
                                  observation.identity_hash),
                      ConvertStage(observation.stage)};
        PerformanceEntry &entry = performance_entries[key];
        ShaderSessionStats &stats = entry.stats;
        entry.needs_enqueue = true;
        stats.key = key;
        if (observation.flags & XEMU_SHADER_BROWSER_OBSERVED) {
            if (!stats.draw_count) stats.first_frame = observation.first_frame;
            else stats.first_frame = std::min(stats.first_frame,
                                              observation.first_frame);
            stats.last_frame = std::max(stats.last_frame,
                                        observation.last_frame);
            stats.draw_count += observation.draw_count_delta;
        }
        stats.specialized_draws += observation.specialized_draw_delta;
        stats.uber_draws += observation.uber_draw_delta;
        stats.replacement_draws += observation.replacement_draw_delta;
        stats.compile_failures += observation.compile_failure_delta;
        stats.compile_cpu.Merge(ConvertDuration(observation.compile_cpu));
        stats.pipeline_cpu.Merge(ConvertDuration(observation.pipeline_cpu));
        stats.prepare_cpu.Merge(ConvertDuration(observation.prepare_cpu));
        stats.foreground_stall_cpu.Merge(
            ConvertDuration(observation.foreground_stall_cpu));
        stats.gpu_execution.Merge(ConvertDuration(observation.gpu_execution));
    }

    void PersistPerformanceSnapshotLocked()
    {
        if (!database.RecordPerformanceSessions()) return;
        if (performance_session_id.empty()) return;
        for (auto &pair : performance_entries) {
            PerformanceEntry &entry = pair.second;
            if (!entry.needs_enqueue) continue;
            std::string ignored;
            if (!database.UpsertPerformanceStats(performance_session_id,
                                                 entry.stats, &ignored)) {
                break;
            }
            entry.needs_enqueue = false;
        }
        for (auto &pair : performance_aggregates) {
            PerformanceAggregateEntry &entry = pair.second;
            if (!entry.needs_enqueue) continue;
            std::string ignored;
            if (!database.UpsertPerformanceAggregate(
                    performance_session_id, entry.aggregate, &ignored)) break;
            entry.needs_enqueue = false;
        }
    }

    void PersistPerformanceSnapshot()
    {
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        {
            std::lock_guard<std::mutex> sample_state(sample_state_mutex);
            DrainPerformanceSamples();
        }
        std::lock_guard<std::mutex> lock(performance_mutex);
        PersistPerformanceSnapshotLocked();
    }

    void MaintenanceMain()
    {
        std::unique_lock<std::mutex> lock(maintenance_mutex);
        while (!maintenance_stop) {
            maintenance_cv.wait_for(lock, std::chrono::seconds(5));
            if (maintenance_stop) break;
            lock.unlock();
            PersistPerformanceSnapshot();
            lock.lock();
        }
    }

    bool FlushArtifactQueue(std::string *error)
    {
        std::unique_lock<std::mutex> lock(artifact_mutex);
        artifact_flush_cv.wait(lock, [&] {
            return artifact_jobs.empty() && !artifact_in_flight;
        });
        if (failed_artifact_jobs) {
            if (error) *error = last_artifact_error;
            return false;
        }
        return true;
    }

    void ArtifactMain()
    {
        while (true) {
            ArtifactJob job;
            {
                std::unique_lock<std::mutex> lock(artifact_mutex);
                artifact_cv.wait(lock, [&] {
                    return artifact_stop || !artifact_jobs.empty();
                });
                if (artifact_stop && artifact_jobs.empty()) break;
                job = std::move(artifact_jobs.front());
                artifact_jobs.pop_front();
                artifact_in_flight = true;
            }
            ArtifactMetadata metadata{};
            std::string error;
            bool ok = artifact_store.Write(job.write, &metadata, &error);
            if (ok) ok = database.RegisterArtifact(metadata, &error);
            {
                std::lock_guard<std::mutex> lock(artifact_mutex);
                if (!ok) {
                    ++failed_artifact_jobs;
                    last_artifact_error = error.empty() ?
                        "Unable to save shader artifact" : error;
                }
                artifact_pending_bytes -= job.write.bytes.size();
                --artifact_pending_jobs;
                artifact_in_flight = false;
            }
            artifact_flush_cv.notify_all();
        }
        artifact_flush_cv.notify_all();
    }
