
bool ShaderDatabase::ReconcileExternalArtifacts(size_t *removed,
                                                std::string *error)
{
    if (removed) *removed = 0;
    if (!Flush(error)) return false;
    sqlite3 *db = nullptr;
    if (!OpenAuxConnection(&db, false, error)) return false;
    sqlite3_stmt *stmt = nullptr;
    if (!Prepare(db, "SELECT artifact_id,relative_path FROM shader_artifacts",
                 &stmt, error)) { sqlite3_close(db); return false; }
    std::vector<std::pair<sqlite3_int64, std::string>> missing;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        sqlite3_int64 id = sqlite3_column_int64(stmt, 0);
        const unsigned char *text = sqlite3_column_text(stmt, 1);
        if (!text) continue;
        std::filesystem::path file = std::filesystem::u8path(artifact_root) /
            reinterpret_cast<const char *>(text);
        std::error_code ec;
        bool exists = std::filesystem::exists(file, ec);
        if (ec) {
            if (error) *error = "Unable to inspect shader artifact: " +
                                ec.message();
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return false;
        }
        if (!exists) {
            missing.emplace_back(id, reinterpret_cast<const char *>(text));
        }
    }
    if (rc != SQLITE_DONE) {
        if (error) *error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return false;
    }
    sqlite3_finalize(stmt);
    if (!Exec(db, "BEGIN IMMEDIATE", error)) { sqlite3_close(db); return false; }
    bool ok = true;
    for (const auto &item : missing) {
        sqlite3_int64 id = item.first;
        if (!Prepare(db, "DELETE FROM shader_artifacts WHERE artifact_id=?",
                     &stmt, error)) { ok = false; break; }
        sqlite3_bind_int64(stmt, 1, id);
        ok = StepDone(db, stmt, error);
        sqlite3_finalize(stmt);
        if (!ok) break;
    }
    if (ok) ok = Exec(db, "COMMIT", error);
    if (!ok && !sqlite3_get_autocommit(db)) {
        Exec(db, "ROLLBACK", nullptr);
    }
    sqlite3_close(db);
    if (ok) {
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            uint64_t total = artifact_bytes.load();
            for (const auto &item : missing) {
                auto it = known_artifact_paths.find(item.second);
                if (it != known_artifact_paths.end()) {
                    total = total >= it->second ? total - it->second : 0;
                    known_artifact_paths.erase(it);
                }
            }
            artifact_bytes.store(total);
            artifact_count.store(known_artifact_paths.size());
        }
        if (removed) *removed = missing.size();
    }
    return ok;
}

bool ShaderDatabase::AcceptingWrites(std::string *error) const
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    if (write_failed) {
        if (error) *error = write_error;
        return false;
    }
    return true;
}

bool ShaderDatabase::Enqueue(WriteJob job, std::string *error)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (write_failed) {
            if (error) *error = write_error;
            return false;
        }
        write_queue.push_back(std::move(job));
    }
    queue_cv.notify_one();
    return true;
}

void ShaderDatabase::WriterMain()
{
    while (true) {
        std::vector<WriteJob> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [&] { return writer_stop || !write_queue.empty(); });
            if (writer_stop && write_queue.empty()) break;
            size_t count = std::min<size_t>(256, write_queue.size());
            batch.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                batch.push_back(std::move(write_queue.front()));
                write_queue.pop_front();
            }
            write_in_flight = true;
        }

        std::string error;
        bool ok = Exec(writer_db, "BEGIN IMMEDIATE", &error);
        if (ok) {
            for (WriteJob &job : batch) {
                if (!job(writer_db, &error)) { ok = false; break; }
            }
        }
        if (ok) ok = Exec(writer_db, "COMMIT", &error);
        if (!ok) {
            if (!sqlite3_get_autocommit(writer_db)) {
                Exec(writer_db, "ROLLBACK", nullptr);
            }
            SetWriteError(error);
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            write_in_flight = false;
        }
        flush_cv.notify_all();
    }
    flush_cv.notify_all();
}

void ShaderDatabase::SetWriteError(const std::string &message)
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    write_failed = true;
    write_error = message;
    write_queue.clear();
}

bool ShaderDatabase::Flush(std::string *error)
{
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
    if (!open) return true;
    std::unique_lock<std::mutex> lock(queue_mutex);
    flush_cv.wait(lock, [&] { return write_queue.empty() && !write_in_flight; });
    if (write_failed) {
        if (error) *error = write_error;
        return false;
    }
    return true;
}

bool ShaderDatabase::QuickCheck(std::string *result, std::string *error)
{
    if (!Flush(error)) return false;
    sqlite3 *db = nullptr;
    if (!OpenAuxConnection(&db, true, error)) return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = Prepare(db, "PRAGMA quick_check", &stmt, error);
    if (ok && sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        *result = text ? reinterpret_cast<const char *>(text) : "unknown";
        ok = *result == "ok";
        if (!ok && error) *error = *result;
    } else if (ok) {
        ok = false;
        if (error) *error = sqlite3_errmsg(db);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool ShaderDatabase::Optimize(std::string *error)
{
    if (!Flush(error)) return false;
    sqlite3 *db = nullptr;
    if (!OpenAuxConnection(&db, false, error)) return false;
    bool ok = Exec(db, "PRAGMA optimize;", error) &&
              Exec(db, "PRAGMA wal_checkpoint(TRUNCATE);", error);
    sqlite3_close(db);
    return ok;
}

bool ShaderDatabase::ClearPerformanceHistory(std::string *error)
{
    if (!Flush(error)) return false;
    sqlite3 *db = nullptr;
    if (!OpenAuxConnection(&db, false, error)) return false;
    bool ok = Exec(db, "BEGIN IMMEDIATE;", error) &&
              Exec(db, "DELETE FROM shader_session_stats;", error) &&
              Exec(db, "DELETE FROM shader_sessions;", error);
    if (ok) {
        ok = Exec(db, "COMMIT;", error);
    } else {
        Exec(db, "ROLLBACK;", nullptr);
    }
    sqlite3_close(db);
    if (!ok) return false;

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        known_sessions.clear();
        known_session_stats.clear();
        session_count.store(0);
        session_stat_count.store(0);
    }
    return true;
}

DatabaseStats ShaderDatabase::GetStats() const
{
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
    DatabaseStats stats{};
    stats.enabled = config.enabled;
    stats.open = open;
    stats.record_performance_sessions = config.record_performance_sessions;
    stats.save_external_artifacts = config.save_external_artifacts;
    stats.schema_version = open ? kShaderDatabaseSchemaVersion : 0;
    stats.database_path = database_path;
    stats.artifact_root = artifact_root;
    stats.journal_mode = open ? "WAL" : "Closed";
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        stats.shader_count = records.size();
        stats.title_count = known_titles.size();
    }
    stats.session_count = session_count.load();
    stats.session_stat_count = session_stat_count.load();
    stats.artifact_count = artifact_count.load();
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stats.pending_writes = write_queue.size() + (write_in_flight ? 1 : 0);
        stats.write_failed = write_failed;
        stats.status = !config.enabled ? "Disabled" :
                       !open ? "Unavailable" :
                       write_failed ? write_error : "Healthy";
    }
    stats.database_bytes = FileSize(database_path);
    stats.wal_bytes = FileSize(database_path + "-wal");
    stats.external_artifact_bytes = artifact_bytes.load();
    return stats;
}

bool ShaderDatabase::Enabled() const
{
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
    return open && config.enabled;
}
