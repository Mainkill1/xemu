    writer_stop = false;
    write_in_flight = false;
    write_failed = false;
    write_error.clear();
    open = true;
    writer_thread = std::thread(&ShaderDatabase::WriterMain, this);
    return true;
}

void ShaderDatabase::Close()
{
    {
        std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
        if (!open && !writer_thread.joinable()) {
            return;
        }
    }
    std::string ignored;
    Flush(&ignored);
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        writer_stop = true;
    }
    queue_cv.notify_all();
    if (writer_thread.joinable()) writer_thread.join();

    std::unique_lock<std::shared_mutex> lock(lifecycle_mutex);
    if (writer_db) {
        sqlite3_close(writer_db);
        writer_db = nullptr;
    }
    open = false;
}

bool ShaderDatabase::InitializeSchema(sqlite3 *db, std::string *error)
{
    const char *sql = R"SQL(
CREATE TABLE IF NOT EXISTS schema_info(
    schema_version INTEGER NOT NULL
);
INSERT INTO schema_info(schema_version)
SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM schema_info);

CREATE TABLE IF NOT EXISTS shaders(
    shader_id INTEGER PRIMARY KEY,
    identity_version INTEGER NOT NULL,
    stage INTEGER NOT NULL,
    shader_hash BLOB NOT NULL,
    recipe_version INTEGER NOT NULL,
    canonical_recipe BLOB NOT NULL,
    first_seen_ms INTEGER NOT NULL DEFAULT 0,
    last_seen_ms INTEGER NOT NULL DEFAULT 0,
    UNIQUE(identity_version, stage, shader_hash)
);
CREATE TABLE IF NOT EXISTS titles(
    title_id INTEGER PRIMARY KEY,
    title_name TEXT,
    first_seen_ms INTEGER NOT NULL DEFAULT 0,
    last_seen_ms INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS executables(
    executable_id INTEGER PRIMARY KEY,
    title_id INTEGER NOT NULL,
    fingerprint_version INTEGER NOT NULL,
    fingerprint BLOB NOT NULL,
    first_seen_ms INTEGER NOT NULL DEFAULT 0,
    last_seen_ms INTEGER NOT NULL DEFAULT 0,
    UNIQUE(title_id, fingerprint_version, fingerprint),
    FOREIGN KEY(title_id) REFERENCES titles(title_id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS title_shaders(
    title_id INTEGER NOT NULL,
    shader_id INTEGER NOT NULL,
    first_seen_ms INTEGER NOT NULL DEFAULT 0,
    last_seen_ms INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(title_id, shader_id),
    FOREIGN KEY(title_id) REFERENCES titles(title_id) ON DELETE CASCADE,
    FOREIGN KEY(shader_id) REFERENCES shaders(shader_id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_title_shaders_shader ON title_shaders(shader_id);

CREATE TABLE IF NOT EXISTS executable_shaders(
    executable_id INTEGER NOT NULL,
    shader_id INTEGER NOT NULL,
    first_seen_ms INTEGER NOT NULL DEFAULT 0,
    last_seen_ms INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(executable_id, shader_id),
    FOREIGN KEY(executable_id) REFERENCES executables(executable_id) ON DELETE CASCADE,
    FOREIGN KEY(shader_id) REFERENCES shaders(shader_id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_executable_shaders_shader
    ON executable_shaders(shader_id);

CREATE TABLE IF NOT EXISTS shader_sessions(
    session_id TEXT PRIMARY KEY,
    title_id INTEGER NOT NULL,
    executable_fingerprint_version INTEGER NOT NULL,
    executable_fingerprint BLOB NOT NULL,
    started_ms INTEGER NOT NULL,
    ended_ms INTEGER NOT NULL DEFAULT 0,
    xemu_revision TEXT,
    renderer TEXT,
    host_os TEXT,
    cpu_model TEXT,
    gpu_name TEXT,
    gpu_vendor_id INTEGER NOT NULL DEFAULT 0,
    gpu_device_id INTEGER NOT NULL DEFAULT 0,
    gpu_driver TEXT,
    internal_resolution_scale INTEGER NOT NULL DEFAULT 1,
    ubershader_mode TEXT,
    shader_cache_enabled INTEGER NOT NULL DEFAULT 0,
    telemetry_version INTEGER NOT NULL DEFAULT 1,
    clean_shutdown INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_shader_sessions_title_time
    ON shader_sessions(title_id, started_ms DESC);

CREATE TABLE IF NOT EXISTS shader_session_stats(
    session_id TEXT NOT NULL,
    shader_id INTEGER NOT NULL,
    draw_count INTEGER NOT NULL DEFAULT 0,
    first_frame INTEGER NOT NULL DEFAULT 0,
    last_frame INTEGER NOT NULL DEFAULT 0,
    specialized_draws INTEGER NOT NULL DEFAULT 0,
    uber_draws INTEGER NOT NULL DEFAULT 0,
    replacement_draws INTEGER NOT NULL DEFAULT 0,
    compile_failures INTEGER NOT NULL DEFAULT 0,
    compile_samples INTEGER NOT NULL DEFAULT 0,
    compile_total_ns INTEGER NOT NULL DEFAULT 0,
    compile_min_ns INTEGER NOT NULL DEFAULT 0,
    compile_max_ns INTEGER NOT NULL DEFAULT 0,
    pipeline_samples INTEGER NOT NULL DEFAULT 0,
    pipeline_total_ns INTEGER NOT NULL DEFAULT 0,
    pipeline_min_ns INTEGER NOT NULL DEFAULT 0,
    pipeline_max_ns INTEGER NOT NULL DEFAULT 0,
    prepare_samples INTEGER NOT NULL DEFAULT 0,
    prepare_total_ns INTEGER NOT NULL DEFAULT 0,
    prepare_min_ns INTEGER NOT NULL DEFAULT 0,
    prepare_max_ns INTEGER NOT NULL DEFAULT 0,
    stall_samples INTEGER NOT NULL DEFAULT 0,
    stall_total_ns INTEGER NOT NULL DEFAULT 0,
    stall_min_ns INTEGER NOT NULL DEFAULT 0,
    stall_max_ns INTEGER NOT NULL DEFAULT 0,
    gpu_samples INTEGER NOT NULL DEFAULT 0,
    gpu_total_ns INTEGER NOT NULL DEFAULT 0,
    gpu_min_ns INTEGER NOT NULL DEFAULT 0,
    gpu_max_ns INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(session_id, shader_id),
    FOREIGN KEY(session_id) REFERENCES shader_sessions(session_id) ON DELETE CASCADE,
    FOREIGN KEY(shader_id) REFERENCES shaders(shader_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS shader_artifacts(
    artifact_id INTEGER PRIMARY KEY,
    shader_id INTEGER NOT NULL,
    title_id INTEGER NOT NULL DEFAULT 0,
    backend TEXT NOT NULL,
    route TEXT NOT NULL,
    kind TEXT NOT NULL,
    generator_abi INTEGER NOT NULL DEFAULT 0,
    xemu_revision TEXT,
    relative_path TEXT NOT NULL UNIQUE,
    byte_size INTEGER NOT NULL,
    content_hash TEXT,
    created_ms INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(shader_id) REFERENCES shaders(shader_id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_shader_artifacts_shader ON shader_artifacts(shader_id);
)SQL";
    if (!Exec(db, sql, error)) return false;
    sqlite3_stmt *stmt = nullptr;
    if (!Prepare(db, "SELECT schema_version FROM schema_info LIMIT 1",
                 &stmt, error)) return false;
    int rc = sqlite3_step(stmt);
    int version = rc == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    if (version != static_cast<int>(kShaderDatabaseSchemaVersion)) {
        if (error) *error = "Unsupported shader database schema version " +
                            std::to_string(version);
        return false;
    }
    return true;
}

bool ShaderDatabase::LoadCache(sqlite3 *db, std::string *error)
{
    std::unordered_map<sqlite3_int64, ShaderKey> keys;
    std::unordered_map<ShaderKey, StoredRecord, ShaderKeyHash> loaded;
    std::unordered_set<uint32_t> titles;

    sqlite3_stmt *stmt = nullptr;
    if (!Prepare(db,
        "SELECT shader_id,identity_version,stage,shader_hash,recipe_version,"
        "canonical_recipe FROM shaders", &stmt, error)) return false;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ShaderRecord record{};
        sqlite3_int64 shader_id = sqlite3_column_int64(stmt, 0);
        record.key.hash.version = sqlite3_column_int(stmt, 1);
        record.key.stage = StageFromWire(sqlite3_column_int(stmt, 2));
        if (!ReadHash(stmt, 3, &record.key.hash)) {
            sqlite3_finalize(stmt);
            if (error) *error = "Shader database contains an invalid hash";
            return false;
        }
        record.recipe_format_version = sqlite3_column_int(stmt, 4);
        const uint8_t *recipe = static_cast<const uint8_t *>(
            sqlite3_column_blob(stmt, 5));
        int recipe_size = sqlite3_column_bytes(stmt, 5);
        if (recipe_size < 0 || static_cast<size_t>(recipe_size) >
                                 kMaxCanonicalRecipeBytes) {
            sqlite3_finalize(stmt);
            if (error) *error = "Shader database contains an invalid recipe";
            return false;
        }
        if (recipe_size) record.recipe.assign(recipe, recipe + recipe_size);
        StoredRecord stored{};
        stored.record = record;
        loaded.emplace(record.key, std::move(stored));
        keys.emplace(shader_id, record.key);
    }
    if (rc != SQLITE_DONE) {
        if (error) *error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

