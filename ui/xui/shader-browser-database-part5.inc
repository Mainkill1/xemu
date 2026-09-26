
bool ShaderDatabase::EndPerformanceSession(const std::string &session_id,
                                           uint64_t ended_unix_ms,
                                           bool clean_shutdown,
                                           std::string *error)
{
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
    if (!open || !config.record_performance_sessions || session_id.empty()) {
        if (error) *error = "Performance session is unavailable";
        return false;
    }
    return Enqueue([session_id, ended_unix_ms, clean_shutdown](
                sqlite3 *db, std::string *job_error) {
        sqlite3_stmt *stmt = nullptr;
        if (!Prepare(db,
            "UPDATE shader_sessions SET ended_ms=?,clean_shutdown=? "
            "WHERE session_id=?", &stmt, job_error)) return false;
        sqlite3_bind_int64(stmt, 1, ended_unix_ms);
        sqlite3_bind_int(stmt, 2, clean_shutdown ? 1 : 0);
        sqlite3_bind_text(stmt, 3, session_id.c_str(), -1, SQLITE_TRANSIENT);
        bool ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        return ok;
    }, error);
}

bool ShaderDatabase::OpenAuxConnection(sqlite3 **db, bool read_only,
                                       std::string *error) const
{
    *db = nullptr;
    int flags = read_only ? SQLITE_OPEN_READONLY :
                            (SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX);
    int rc = sqlite3_open_v2(database_path.c_str(), db, flags, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = *db ? sqlite3_errmsg(*db) : "Unable to open SQLite database";
        if (*db) sqlite3_close(*db);
        *db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(*db, 2500);
    return true;
}

std::vector<SessionSummary> ShaderDatabase::CopySessions(uint32_t title_id,
                                                         size_t limit)
{
    std::string ignored;
    Flush(&ignored);
    std::vector<SessionSummary> out;
    sqlite3 *db = nullptr;
    if (!OpenAuxConnection(&db, true, &ignored)) return out;
    sqlite3_stmt *stmt = nullptr;
    if (!Prepare(db,
        "SELECT session_id,title_id,started_ms,ended_ms,xemu_revision,renderer,"
        "gpu_name,clean_shutdown FROM shader_sessions WHERE title_id=? "
        "ORDER BY started_ms DESC LIMIT ?", &stmt, &ignored)) {
        sqlite3_close(db); return out;
    }
    sqlite3_bind_int64(stmt, 1, title_id);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionSummary row{};
        row.session_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        row.title_id = static_cast<uint32_t>(sqlite3_column_int64(stmt, 1));
        row.started_unix_ms = sqlite3_column_int64(stmt, 2);
        row.ended_unix_ms = sqlite3_column_int64(stmt, 3);
        const unsigned char *text = sqlite3_column_text(stmt, 4);
        if (text) row.xemu_revision = reinterpret_cast<const char *>(text);
        text = sqlite3_column_text(stmt, 5);
        if (text) row.renderer = reinterpret_cast<const char *>(text);
        text = sqlite3_column_text(stmt, 6);
        if (text) row.gpu_name = reinterpret_cast<const char *>(text);
        row.clean_shutdown = sqlite3_column_int(stmt, 7) != 0;
        out.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return out;
}

std::vector<ShaderSessionStats> ShaderDatabase::CopySessionStats(
    const std::string &session_id)
{
    std::string ignored;
    Flush(&ignored);
    std::vector<ShaderSessionStats> out;
    sqlite3 *db = nullptr;
    if (!OpenAuxConnection(&db, true, &ignored)) return out;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT s.identity_version,s.stage,s.shader_hash,ss.draw_count,ss.first_frame,"
        "ss.last_frame,ss.specialized_draws,ss.uber_draws,ss.replacement_draws,"
        "ss.compile_failures,ss.compile_samples,ss.compile_total_ns,ss.compile_min_ns,"
        "ss.compile_max_ns,ss.pipeline_samples,ss.pipeline_total_ns,ss.pipeline_min_ns,"
        "ss.pipeline_max_ns,ss.prepare_samples,ss.prepare_total_ns,ss.prepare_min_ns,"
        "ss.prepare_max_ns,ss.stall_samples,ss.stall_total_ns,ss.stall_min_ns,"
        "ss.stall_max_ns,ss.gpu_samples,ss.gpu_total_ns,ss.gpu_min_ns,ss.gpu_max_ns "
        "FROM shader_session_stats ss JOIN shaders s ON s.shader_id=ss.shader_id "
        "WHERE ss.session_id=?";
    if (!Prepare(db, sql, &stmt, &ignored)) { sqlite3_close(db); return out; }
    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ShaderSessionStats row{};
        row.key.hash.version = sqlite3_column_int(stmt, 0);
        row.key.stage = StageFromWire(sqlite3_column_int(stmt, 1));
        if (!ReadHash(stmt, 2, &row.key.hash)) continue;
        row.draw_count = sqlite3_column_int64(stmt, 3);
        row.first_frame = sqlite3_column_int64(stmt, 4);
        row.last_frame = sqlite3_column_int64(stmt, 5);
        row.specialized_draws = sqlite3_column_int64(stmt, 6);
        row.uber_draws = sqlite3_column_int64(stmt, 7);
        row.replacement_draws = sqlite3_column_int64(stmt, 8);
        row.compile_failures = sqlite3_column_int64(stmt, 9);
        row.compile_cpu = ReadDuration(stmt, 10);
        row.pipeline_cpu = ReadDuration(stmt, 14);
        row.prepare_cpu = ReadDuration(stmt, 18);
        row.foreground_stall_cpu = ReadDuration(stmt, 22);
        row.gpu_execution = ReadDuration(stmt, 26);
        out.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return out;
}

bool ShaderDatabase::RegisterArtifact(const ArtifactMetadata &artifact,
                                      std::string *error)
{
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
    if (!open || !config.save_external_artifacts) {
        if (error) *error = "External shader artifact storage is disabled";
        return false;
    }
    if (artifact.relative_path.empty() ||
        std::filesystem::u8path(artifact.relative_path).is_absolute() ||
        artifact.relative_path.find("..") != std::string::npos) {
        if (error) *error = "Artifact path must be relative to shader-artifacts";
        return false;
    }
    ArtifactMetadata copy = artifact;
    if (!Enqueue([copy](sqlite3 *db, std::string *job_error) {
        sqlite3_int64 shader_id = 0;
        if (!FindShaderId(db, copy.key, &shader_id, job_error)) return false;
        sqlite3_stmt *stmt = nullptr;
        if (!Prepare(db,
            "INSERT INTO shader_artifacts(shader_id,title_id,backend,route,kind,"
            "generator_abi,xemu_revision,relative_path,byte_size,content_hash) "
            "VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(relative_path) DO UPDATE SET "
            "byte_size=excluded.byte_size,content_hash=excluded.content_hash,"
            "xemu_revision=excluded.xemu_revision", &stmt, job_error)) return false;
        int i = 1;
        sqlite3_bind_int64(stmt, i++, shader_id);
        sqlite3_bind_int64(stmt, i++, copy.title_id);
        sqlite3_bind_text(stmt, i++, copy.backend.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, i++, copy.route.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, i++, copy.kind.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, i++, copy.generator_abi);
        sqlite3_bind_text(stmt, i++, copy.xemu_revision.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, i++, copy.relative_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, i++, copy.byte_size);
        sqlite3_bind_text(stmt, i++, copy.content_hash.c_str(), -1, SQLITE_TRANSIENT);
        bool ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        return ok;
    }, error)) return false;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = known_artifact_paths.find(artifact.relative_path);
        uint64_t total = artifact_bytes.load();
        if (it != known_artifact_paths.end()) {
            uint64_t previous = it->second;
            total = total >= previous ? total - previous : 0;
            it->second = artifact.byte_size;
        } else {
            known_artifact_paths.emplace(artifact.relative_path,
                                         artifact.byte_size);
        }
        if (std::numeric_limits<uint64_t>::max() - total < artifact.byte_size) {
            total = std::numeric_limits<uint64_t>::max();
        } else {
            total += artifact.byte_size;
        }
        artifact_bytes.store(total);
        artifact_count.store(known_artifact_paths.size());
    }
    return true;
}
