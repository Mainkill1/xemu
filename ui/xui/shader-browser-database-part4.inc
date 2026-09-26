        if (!copy.scope.title_id) return true;
        stmt = nullptr;
        if (!Prepare(db,
            "INSERT INTO titles(title_id) VALUES(?) "
            "ON CONFLICT(title_id) DO NOTHING", &stmt, job_error)) return false;
        sqlite3_bind_int64(stmt, 1, copy.scope.title_id);
        ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        if (!ok) return false;

        sqlite3_int64 executable_id = 0;
        bool has_exec = copy.scope.executable_fingerprint_version != 0;
        if (has_exec) {
            stmt = nullptr;
            if (!Prepare(db,
                "INSERT INTO executables(title_id,fingerprint_version,fingerprint) "
                "VALUES(?,?,?) ON CONFLICT(title_id,fingerprint_version,fingerprint) "
                "DO NOTHING", &stmt, job_error)) return false;
            sqlite3_bind_int64(stmt, 1, copy.scope.title_id);
            sqlite3_bind_int(stmt, 2,
                             copy.scope.executable_fingerprint_version);
            sqlite3_bind_blob(stmt, 3,
                copy.scope.executable_fingerprint.data(),
                static_cast<int>(copy.scope.executable_fingerprint.size()),
                SQLITE_TRANSIENT);
            ok = StepDone(db, stmt, job_error);
            sqlite3_finalize(stmt);
            if (!ok) return false;

            if (!Prepare(db,
                "SELECT executable_id FROM executables WHERE title_id=? AND "
                "fingerprint_version=? AND fingerprint=?", &stmt, job_error))
                return false;
            sqlite3_bind_int64(stmt, 1, copy.scope.title_id);
            sqlite3_bind_int(stmt, 2,
                             copy.scope.executable_fingerprint_version);
            sqlite3_bind_blob(stmt, 3,
                copy.scope.executable_fingerprint.data(),
                static_cast<int>(copy.scope.executable_fingerprint.size()),
                SQLITE_TRANSIENT);
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) executable_id = sqlite3_column_int64(stmt, 0);
            else {
                if (job_error) *job_error = sqlite3_errmsg(db);
                sqlite3_finalize(stmt);
                return false;
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_int64 shader_id = 0;
        if (!FindShaderId(db, copy.key, &shader_id, job_error)) return false;
        if (!Prepare(db,
            "INSERT INTO title_shaders(title_id,shader_id) VALUES(?,?) "
            "ON CONFLICT(title_id,shader_id) DO NOTHING",
            &stmt, job_error)) return false;
        sqlite3_bind_int64(stmt, 1, copy.scope.title_id);
        sqlite3_bind_int64(stmt, 2, shader_id);
        ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        if (!ok || !has_exec) return ok;

        if (!Prepare(db,
            "INSERT INTO executable_shaders(executable_id,shader_id) "
            "VALUES(?,?) ON CONFLICT(executable_id,shader_id) DO NOTHING",
            &stmt, job_error)) return false;
        sqlite3_bind_int64(stmt, 1, executable_id);
        sqlite3_bind_int64(stmt, 2, shader_id);
        ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        return ok;
    }, error);
}

bool ShaderDatabase::CopyRecord(const ShaderKey &key,
                                ShaderRecord *record) const
{
    if (!record) return false;
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = records.find(key);
    if (it == records.end()) return false;
    *record = it->second.record;
    if (!it->second.scopes.empty()) record->scope = it->second.scopes.front();
    return true;
}

std::vector<DatabaseShaderMetadata> ShaderDatabase::CopyMetadata() const
{
    std::lock_guard<std::mutex> lock(cache_mutex);
    std::vector<DatabaseShaderMetadata> out;
    out.reserve(records.size());
    for (const auto &pair : records) {
        DatabaseShaderMetadata meta{};
        meta.key = pair.first;
        meta.recipe_format_version = pair.second.record.recipe_format_version;
        meta.recipe_size = pair.second.record.recipe.size();
        meta.scopes = pair.second.scopes;
        out.push_back(std::move(meta));
    }
    return out;
}

bool ShaderDatabase::BeginPerformanceSession(const SessionDescriptor &session,
                                             std::string *error)
{
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
    if (!open || !config.record_performance_sessions) {
        if (error) *error = "Per-session shader statistics are disabled";
        return false;
    }
    if (session.session_id.empty() || !session.title_id) {
        if (error) *error = "Performance session requires ID and title";
        return false;
    }
    if (!Enqueue([session](sqlite3 *db, std::string *job_error) {
        sqlite3_stmt *stmt = nullptr;
        const char *sql =
            "INSERT OR REPLACE INTO shader_sessions(session_id,title_id,"
            "executable_fingerprint_version,executable_fingerprint,started_ms,"
            "xemu_revision,renderer,host_os,cpu_model,gpu_name,gpu_vendor_id,"
            "gpu_device_id,gpu_driver,internal_resolution_scale,ubershader_mode,"
            "shader_cache_enabled,telemetry_version,clean_shutdown) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0)";
        if (!Prepare(db, sql, &stmt, job_error)) return false;
        int i = 1;
        sqlite3_bind_text(stmt, i++, session.session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, i++, session.title_id);
        sqlite3_bind_int(stmt, i++, session.executable_fingerprint_version);
        sqlite3_bind_blob(stmt, i++, session.executable_fingerprint.data(),
                          static_cast<int>(session.executable_fingerprint.size()),
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, i++, session.started_unix_ms);
        sqlite3_bind_text(stmt, i++, session.xemu_revision.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, i++, session.renderer.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, i++, session.host_os.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, i++, session.cpu_model.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, i++, session.gpu_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, i++, session.gpu_vendor_id);
        sqlite3_bind_int64(stmt, i++, session.gpu_device_id);
        sqlite3_bind_text(stmt, i++, session.gpu_driver.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, i++, session.internal_resolution_scale);
        sqlite3_bind_text(stmt, i++, session.ubershader_mode.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, i++, session.shader_cache_enabled ? 1 : 0);
        sqlite3_bind_int(stmt, i++, session.telemetry_version);
        bool ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        return ok;
    }, error)) return false;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        known_sessions.insert(session.session_id);
        session_count.store(known_sessions.size());
    }
    return true;
}

bool ShaderDatabase::UpsertPerformanceStats(const std::string &session_id,
                                            const ShaderSessionStats &stats,
                                            std::string *error)
{
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
    if (!open || !config.record_performance_sessions) {
        if (error) *error = "Per-session shader statistics are disabled";
        return false;
    }
    if (session_id.empty()) {
        if (error) *error = "Performance session ID is empty";
        return false;
    }
    ShaderSessionStats copy = stats;
    if (!Enqueue([session_id, copy](sqlite3 *db, std::string *job_error) {
        sqlite3_int64 shader_id = 0;
        if (!FindShaderId(db, copy.key, &shader_id, job_error)) return false;
        sqlite3_stmt *stmt = nullptr;
        const char *sql =
            "INSERT INTO shader_session_stats("
            "session_id,shader_id,draw_count,first_frame,last_frame,"
            "specialized_draws,uber_draws,replacement_draws,compile_failures,"
            "compile_samples,compile_total_ns,compile_min_ns,compile_max_ns,"
            "pipeline_samples,pipeline_total_ns,pipeline_min_ns,pipeline_max_ns,"
            "prepare_samples,prepare_total_ns,prepare_min_ns,prepare_max_ns,"
            "stall_samples,stall_total_ns,stall_min_ns,stall_max_ns,"
            "gpu_samples,gpu_total_ns,gpu_min_ns,gpu_max_ns) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(session_id,shader_id) DO UPDATE SET "
            "draw_count=excluded.draw_count,first_frame=excluded.first_frame,"
            "last_frame=excluded.last_frame,specialized_draws=excluded.specialized_draws,"
            "uber_draws=excluded.uber_draws,replacement_draws=excluded.replacement_draws,"
            "compile_failures=excluded.compile_failures,"
            "compile_samples=excluded.compile_samples,compile_total_ns=excluded.compile_total_ns,"
            "compile_min_ns=excluded.compile_min_ns,compile_max_ns=excluded.compile_max_ns,"
            "pipeline_samples=excluded.pipeline_samples,pipeline_total_ns=excluded.pipeline_total_ns,"
            "pipeline_min_ns=excluded.pipeline_min_ns,pipeline_max_ns=excluded.pipeline_max_ns,"
            "prepare_samples=excluded.prepare_samples,prepare_total_ns=excluded.prepare_total_ns,"
            "prepare_min_ns=excluded.prepare_min_ns,prepare_max_ns=excluded.prepare_max_ns,"
            "stall_samples=excluded.stall_samples,stall_total_ns=excluded.stall_total_ns,"
            "stall_min_ns=excluded.stall_min_ns,stall_max_ns=excluded.stall_max_ns,"
            "gpu_samples=excluded.gpu_samples,gpu_total_ns=excluded.gpu_total_ns,"
            "gpu_min_ns=excluded.gpu_min_ns,gpu_max_ns=excluded.gpu_max_ns";
        if (!Prepare(db, sql, &stmt, job_error)) return false;
        int i = 1;
        sqlite3_bind_text(stmt, i++, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, i++, shader_id);
        sqlite3_bind_int64(stmt, i++, copy.draw_count);
        sqlite3_bind_int64(stmt, i++, copy.first_frame);
        sqlite3_bind_int64(stmt, i++, copy.last_frame);
        sqlite3_bind_int64(stmt, i++, copy.specialized_draws);
        sqlite3_bind_int64(stmt, i++, copy.uber_draws);
        sqlite3_bind_int64(stmt, i++, copy.replacement_draws);
        sqlite3_bind_int64(stmt, i++, copy.compile_failures);
        BindDuration(stmt, i, copy.compile_cpu); i += 4;
        BindDuration(stmt, i, copy.pipeline_cpu); i += 4;
        BindDuration(stmt, i, copy.prepare_cpu); i += 4;
        BindDuration(stmt, i, copy.foreground_stall_cpu); i += 4;
        BindDuration(stmt, i, copy.gpu_execution); i += 4;
        bool ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        return ok;
    }, error)) return false;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        known_session_stats.insert(SessionStatIdentity(session_id, stats.key));
        session_stat_count.store(known_session_stats.size());
    }
    return true;
}
