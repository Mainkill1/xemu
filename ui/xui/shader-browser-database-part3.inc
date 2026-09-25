    if (!Prepare(db,
        "SELECT shader_id,title_id FROM title_shaders", &stmt, error))
        return false;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto key = keys.find(sqlite3_column_int64(stmt, 0));
        if (key == keys.end()) continue;
        ShaderScope scope{};
        scope.title_id = static_cast<uint32_t>(sqlite3_column_int64(stmt, 1));
        AddScope(&loaded[key->second].scopes, scope);
        if (scope.title_id) titles.insert(scope.title_id);
    }
    if (rc != SQLITE_DONE) {
        if (error) *error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    if (!Prepare(db,
        "SELECT es.shader_id,e.title_id,e.fingerprint_version,e.fingerprint "
        "FROM executable_shaders es JOIN executables e "
        "ON e.executable_id=es.executable_id", &stmt, error)) return false;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto key = keys.find(sqlite3_column_int64(stmt, 0));
        if (key == keys.end()) continue;
        ShaderScope scope{};
        scope.title_id = static_cast<uint32_t>(sqlite3_column_int64(stmt, 1));
        scope.executable_fingerprint_version = sqlite3_column_int(stmt, 2);
        const void *blob = sqlite3_column_blob(stmt, 3);
        int size = sqlite3_column_bytes(stmt, 3);
        if (blob && size == static_cast<int>(scope.executable_fingerprint.size())) {
            std::memcpy(scope.executable_fingerprint.data(), blob,
                        scope.executable_fingerprint.size());
        }
        AddScope(&loaded[key->second].scopes, scope);
        if (scope.title_id) titles.insert(scope.title_id);
    }
    if (rc != SQLITE_DONE) {
        if (error) *error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    std::unordered_set<std::string> sessions;
    std::unordered_set<std::string> session_stats;
    std::unordered_map<std::string, uint64_t> artifacts;
    if (!Prepare(db, "SELECT session_id FROM shader_sessions", &stmt, error))
        return false;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (text) sessions.insert(reinterpret_cast<const char *>(text));
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;

    if (!Prepare(db,
        "SELECT ss.session_id,s.identity_version,s.stage,s.shader_hash "
        "FROM shader_session_stats ss JOIN shaders s "
        "ON s.shader_id=ss.shader_id", &stmt, error)) return false;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (!text) continue;
        ShaderKey key{};
        key.hash.version = sqlite3_column_int(stmt, 1);
        key.stage = StageFromWire(sqlite3_column_int(stmt, 2));
        if (!ReadHash(stmt, 3, &key.hash)) continue;
        session_stats.insert(SessionStatIdentity(
            reinterpret_cast<const char *>(text), key));
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;

    if (!Prepare(db, "SELECT relative_path,byte_size FROM shader_artifacts",
                 &stmt, error))
        return false;
    uint64_t loaded_artifact_bytes = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (!text) continue;
        uint64_t size = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
        artifacts[reinterpret_cast<const char *>(text)] = size;
        if (std::numeric_limits<uint64_t>::max() - loaded_artifact_bytes < size) {
            loaded_artifact_bytes = std::numeric_limits<uint64_t>::max();
        } else {
            loaded_artifact_bytes += size;
        }
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;

    std::lock_guard<std::mutex> lock(cache_mutex);
    records = std::move(loaded);
    known_titles = std::move(titles);
    known_sessions = std::move(sessions);
    known_session_stats = std::move(session_stats);
    known_artifact_paths = std::move(artifacts);
    session_count.store(known_sessions.size());
    session_stat_count.store(known_session_stats.size());
    artifact_count.store(known_artifact_paths.size());
    artifact_bytes.store(loaded_artifact_bytes);
    return true;
}

bool ShaderDatabase::ValidateShaderRecord(const ShaderRecord &record,
                                          std::string *error) const
{
    if (!record.key.hash.version || record.key.stage == Stage::Any ||
        record.key.stage == Stage::Unknown) {
        if (error) *error = "Shader identity is incomplete";
        return false;
    }
    if (!record.recipe_format_version || record.recipe.empty()) {
        if (error) *error = "Canonical shader recipe is required";
        return false;
    }
    if (record.recipe.size() > kMaxCanonicalRecipeBytes) {
        if (error) *error = "Canonical recipe exceeds the 256 KiB safety limit";
        return false;
    }
    ShaderHash expected = ComputeShaderHash(
        record.key.hash.version, record.key.stage,
        record.recipe_format_version, record.recipe.data(), record.recipe.size());
    if (!expected.version || expected != record.key.hash) {
        if (error) {
            *error = "ShaderHash does not match canonical recipe identity";
        }
        return false;
    }
    return true;
}

bool ShaderDatabase::UpsertShader(const ShaderRecord &record,
                                  std::string *error)
{
    std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
    if (!open || !config.enabled) {
        if (error) *error = "Shader database persistence is disabled";
        return false;
    }
    if (!ValidateShaderRecord(record, error)) return false;

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto [it, inserted] = records.try_emplace(record.key);
        StoredRecord &stored = it->second;
        if (inserted) {
            stored.record = record;
            changed = true;
        } else {
            if (stored.record.recipe_format_version !=
                    record.recipe_format_version ||
                stored.record.recipe != record.recipe) {
                if (error) {
                    *error = "Shader hash collision: identical hash/stage has "
                             "different canonical recipe identity";
                }
                return false;
            }
        }
        size_t old_scopes = stored.scopes.size();
        AddScope(&stored.scopes, record.scope);
        changed |= stored.scopes.size() != old_scopes;
        if (record.scope.title_id) known_titles.insert(record.scope.title_id);
    }
    if (!changed) return true;

    ShaderRecord copy = record;
    Enqueue([copy](sqlite3 *db, std::string *job_error) {
        sqlite3_stmt *stmt = nullptr;
        const char *shader_sql =
            "INSERT INTO shaders(identity_version,stage,shader_hash,recipe_version,"
            "canonical_recipe) VALUES(?,?,?,?,?) "
            "ON CONFLICT(identity_version,stage,shader_hash) DO UPDATE SET "
            "recipe_version=excluded.recipe_version," 
            "canonical_recipe=excluded.canonical_recipe";
        if (!Prepare(db, shader_sql, &stmt, job_error)) return false;
        sqlite3_bind_int(stmt, 1, copy.key.hash.version);
        sqlite3_bind_int(stmt, 2, StageToWire(copy.key.stage));
        BindHash(stmt, 3, copy.key.hash);
        sqlite3_bind_int(stmt, 4, copy.recipe_format_version);
        sqlite3_bind_blob(stmt, 5, copy.recipe.data(),
                          static_cast<int>(copy.recipe.size()), SQLITE_TRANSIENT);
        bool ok = StepDone(db, stmt, job_error);
        sqlite3_finalize(stmt);
        if (!ok) return false;

