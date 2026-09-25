    bool PublishExternalArtifact(
        const XemuShaderBrowserExternalArtifact *wire)
    {
        if (!wire || !database.SaveExternalArtifacts() ||
            !wire->data || !wire->size || wire->size > kMaxExternalArtifactBytes ||
            !wire->title_id || !wire->backend || !wire->route ||
            !wire->kind || !wire->extension) {
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
        job.write.bytes.assign(wire->data, wire->data + wire->size);
        {
            std::lock_guard<std::mutex> lock(artifact_mutex);
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
        current_frame = 0;
        live_consumer_epoch = epoch;
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
        ShaderSessionStats &stats = performance_entries[key];
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

    void PersistPerformanceSnapshot()
    {
        if (!database.RecordPerformanceSessions()) return;
        std::string session_id;
        std::vector<ShaderSessionStats> copy;
        {
            std::lock_guard<std::mutex> lock(performance_mutex);
            if (performance_session_id.empty()) return;
            session_id = performance_session_id;
            copy.reserve(performance_entries.size());
            for (const auto &pair : performance_entries) copy.push_back(pair.second);
        }
        for (const ShaderSessionStats &stats : copy) {
            std::string ignored;
            database.UpsertPerformanceStats(session_id, stats, &ignored);
        }
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

    void FlushArtifactQueue()
    {
        std::unique_lock<std::mutex> lock(artifact_mutex);
        artifact_flush_cv.wait(lock, [&] {
            return artifact_jobs.empty() && !artifact_in_flight;
        });
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
            std::string ignored;
            if (artifact_store.Write(job.write, &metadata, &ignored)) {
                database.RegisterArtifact(metadata, &ignored);
            }
            {
                std::lock_guard<std::mutex> lock(artifact_mutex);
                artifact_in_flight = false;
            }
            artifact_flush_cv.notify_all();
        }
        artifact_flush_cv.notify_all();
    }
