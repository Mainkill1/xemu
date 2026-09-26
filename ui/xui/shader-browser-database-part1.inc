// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-database.hh"

#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>

namespace xemu::shader_browser {
namespace {

bool Exec(sqlite3 *db, const char *sql, std::string *error)
{
    char *message = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (rc == SQLITE_OK) {
        return true;
    }
    if (error) {
        *error = message ? message : sqlite3_errmsg(db);
    }
    sqlite3_free(message);
    return false;
}

bool Prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt,
             std::string *error)
{
    int rc = sqlite3_prepare_v2(db, sql, -1, stmt, nullptr);
    if (rc == SQLITE_OK) {
        return true;
    }
    if (error) {
        *error = sqlite3_errmsg(db);
    }
    return false;
}

bool StepDone(sqlite3 *db, sqlite3_stmt *stmt, std::string *error)
{
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return true;
    }
    if (error) {
        *error = sqlite3_errmsg(db);
    }
    return false;
}

int StageToWire(Stage stage)
{
    switch (stage) {
    case Stage::Vertex: return 1;
    case Stage::Pixel: return 2;
    case Stage::Geometry: return 3;
    case Stage::FixedFunction: return 4;
    case Stage::Unknown: return 5;
    case Stage::Any: return 0;
    }
    return 0;
}

Stage StageFromWire(int stage)
{
    switch (stage) {
    case 1: return Stage::Vertex;
    case 2: return Stage::Pixel;
    case 3: return Stage::Geometry;
    case 4: return Stage::FixedFunction;
    default: return Stage::Unknown;
    }
}

bool IsZeroScope(const ShaderScope &scope)
{
    return scope.title_id == 0 &&
           scope.executable_fingerprint_version == 0 &&
           std::all_of(scope.executable_fingerprint.begin(),
                       scope.executable_fingerprint.end(),
                       [](uint8_t byte) { return byte == 0; });
}

void AddScope(std::vector<ShaderScope> *scopes, const ShaderScope &scope)
{
    if (IsZeroScope(scope)) {
        return;
    }
    if (std::find(scopes->begin(), scopes->end(), scope) == scopes->end()) {
        scopes->push_back(scope);
        std::sort(scopes->begin(), scopes->end());
    }
}

uint64_t FileSize(const std::string &path)
{
    std::error_code ec;
    uint64_t value = std::filesystem::file_size(
        std::filesystem::u8path(path), ec);
    return ec ? 0 : value;
}

void BindHash(sqlite3_stmt *stmt, int index, const ShaderHash &hash)
{
    sqlite3_bind_blob(stmt, index, hash.bytes.data(),
                      static_cast<int>(hash.bytes.size()), SQLITE_TRANSIENT);
}

bool ReadHash(sqlite3_stmt *stmt, int column, ShaderHash *hash)
{
    const void *blob = sqlite3_column_blob(stmt, column);
    int size = sqlite3_column_bytes(stmt, column);
    if (!blob || size != static_cast<int>(hash->bytes.size())) {
        return false;
    }
    std::memcpy(hash->bytes.data(), blob, hash->bytes.size());
    return true;
}

std::string SessionStatIdentity(const std::string &session_id,
                                const ShaderKey &key)
{
    std::string value = session_id;
    value.push_back('\0');
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        value.push_back(static_cast<char>((key.hash.version >> shift) & 0xffU));
    }
    value.push_back(static_cast<char>(key.stage));
    value.append(reinterpret_cast<const char *>(key.hash.bytes.data()),
                 key.hash.bytes.size());
    return value;
}

bool FindShaderId(sqlite3 *db, const ShaderKey &key, sqlite3_int64 *shader_id,
                  std::string *error)
{
    sqlite3_stmt *stmt = nullptr;
    if (!Prepare(db,
        "SELECT shader_id FROM shaders WHERE identity_version=? AND stage=? "
        "AND shader_hash=?", &stmt, error)) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, static_cast<int>(key.hash.version));
    sqlite3_bind_int(stmt, 2, StageToWire(key.stage));
    BindHash(stmt, 3, key.hash);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *shader_id = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return true;
    }
    if (error) {
        *error = rc == SQLITE_DONE ? "Shader is not present in the database" :
                                     sqlite3_errmsg(db);
    }
    sqlite3_finalize(stmt);
    return false;
}

void BindDuration(sqlite3_stmt *stmt, int first, const DurationStats &stats)
{
    sqlite3_bind_int64(stmt, first + 0, stats.samples);
    sqlite3_bind_int64(stmt, first + 1, stats.total_ns);
    sqlite3_bind_int64(stmt, first + 2, stats.min_ns);
    sqlite3_bind_int64(stmt, first + 3, stats.max_ns);
}

DurationStats ReadDuration(sqlite3_stmt *stmt, int first)
{
    DurationStats stats{};
    stats.samples = sqlite3_column_int64(stmt, first + 0);
    stats.total_ns = sqlite3_column_int64(stmt, first + 1);
    stats.min_ns = sqlite3_column_int64(stmt, first + 2);
    stats.max_ns = sqlite3_column_int64(stmt, first + 3);
    return stats;
}

std::vector<uint8_t> EncodePerformanceMembers(
    const std::vector<ShaderKey> &members)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(members.size() * 17);
    for (const ShaderKey &member : members) {
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            bytes.push_back((member.hash.version >> shift) & 0xffU);
        }
        bytes.push_back(StageToWire(member.stage));
        for (uint8_t byte : member.hash.bytes) bytes.push_back(byte);
    }
    return bytes;
}

bool DecodePerformanceMembers(const void *blob, int length,
                              std::vector<ShaderKey> *members)
{
    if (!blob || length < 17 || length > 51 || length % 17) return false;
    const uint8_t *bytes = static_cast<const uint8_t *>(blob);
    members->clear();
    for (int offset = 0; offset < length; offset += 17) {
        ShaderKey member{};
        member.hash.version = 0;
        for (int i = 0; i < 4; ++i) {
            member.hash.version |= (uint32_t)bytes[offset + i] << (8 * i);
        }
        member.stage = StageFromWire(bytes[offset + 4]);
        if (!member.hash.version || member.stage == Stage::Unknown) {
            members->clear();
            return false;
        }
        std::memcpy(member.hash.bytes.data(), bytes + offset + 5, 12);
        members->push_back(member);
    }
    return true;
}

} // namespace

ShaderDatabase::ShaderDatabase() = default;
ShaderDatabase::~ShaderDatabase() { Close(); }

bool ShaderDatabase::Configure(const DatabaseConfig &new_config,
                               std::string *error)
{
    std::unique_lock<std::shared_mutex> lock(lifecycle_mutex);
    if (!new_config.enabled) {
        lock.unlock();
        Close();
        lock.lock();
        config = new_config;
        database_path = (std::filesystem::u8path(new_config.base_path) /
                         "shader-browser.db").u8string();
        artifact_root = (std::filesystem::u8path(new_config.base_path) /
                         "shader-artifacts").u8string();
        return true;
    }

    if (open && config.base_path == new_config.base_path) {
        config = new_config;
        return true;
    }

    lock.unlock();
    Close();
    lock.lock();
    return OpenLocked(new_config, error);
}

bool ShaderDatabase::OpenLocked(const DatabaseConfig &new_config,
                                std::string *error)
{
    if (new_config.base_path.empty()) {
        if (error) *error = "xemu configuration directory is unavailable";
        return false;
    }
    config = new_config;
    std::filesystem::path base = std::filesystem::u8path(config.base_path);
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) {
        if (error) *error = "Unable to create xemu configuration directory: " +
                            ec.message();
        return false;
    }
    database_path = (base / "shader-browser.db").u8string();
    artifact_root = (base / "shader-artifacts").u8string();

    int rc = sqlite3_open_v2(database_path.c_str(), &writer_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                             SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = writer_db ? sqlite3_errmsg(writer_db) :
                                       "Unable to open SQLite database";
        if (writer_db) sqlite3_close(writer_db);
        writer_db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(writer_db, 2500);
    if (!Exec(writer_db, "PRAGMA foreign_keys=ON;", error) ||
        !Exec(writer_db, "PRAGMA journal_mode=WAL;", error) ||
        !Exec(writer_db, "PRAGMA synchronous=NORMAL;", error) ||
        !InitializeSchema(writer_db, error) || !LoadCache(writer_db, error)) {
        sqlite3_close(writer_db);
        writer_db = nullptr;
        return false;
    }
