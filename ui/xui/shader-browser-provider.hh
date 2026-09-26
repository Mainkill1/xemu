// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace xemu::shader_browser {

struct Snapshot {
    uint64_t snapshot_sequence = 0;
    uint64_t session_epoch = 0;
    uint64_t current_frame = 0;
    uint32_t current_title_id = 0;
    uint32_t capabilities = 0;
    bool live_collection_enabled = false;
    bool renderer_collection_required = false;
    size_t pending_observation_count = 0;

    bool database_enabled = false;
    bool database_open = false;
    bool record_performance_sessions = false;
    bool save_external_artifacts = false;
    bool database_write_failed = false;
    uint32_t database_schema_version = 0;
    size_t stored_shader_count = 0;
    size_t title_count = 0;
    size_t session_count = 0;
    size_t session_stat_count = 0;
    size_t artifact_count = 0;
    uint64_t rejected_artifact_count = 0;
    uint64_t failed_artifact_count = 0;
    std::string last_artifact_error;
    size_t pending_database_writes = 0;
    uint64_t database_bytes = 0;
    uint64_t database_wal_bytes = 0;
    uint64_t external_artifact_bytes = 0;
    std::string database_path;
    std::string artifact_root;
    std::string database_status;
    std::string database_journal_mode;
    std::vector<Entry> entries;
};

class Provider
{
public:
    virtual ~Provider() = default;

    virtual bool CopySnapshot(Snapshot *snapshot) = 0;
    virtual void SetLiveCollectionEnabled(bool enabled) = 0;
    virtual void ClearLiveSession() = 0;

    virtual bool ConfigurePersistence(bool database_enabled,
                                      bool record_performance_sessions,
                                      bool save_external_artifacts,
                                      std::string *error) = 0;
    virtual bool FlushDatabase(std::string *error) = 0;
    virtual bool QuickCheckDatabase(std::string *result,
                                    std::string *error) = 0;
    virtual bool OptimizeDatabase(std::string *error) = 0;
    virtual bool ClearPerformanceHistory(std::string *error) = 0;
    virtual bool ReconcileArtifacts(size_t *removed,
                                    std::string *error) = 0;
};

// Process-lifetime singleton. Renderers never own this object and XUI never
// borrows renderer-owned cache pointers through it.
Provider &GetProvider();

} // namespace xemu::shader_browser
