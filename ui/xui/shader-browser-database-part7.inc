
bool ShaderDatabase::RecordPerformanceSessions() const
{
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
    return open && config.record_performance_sessions;
}

bool ShaderDatabase::SaveExternalArtifacts() const
{
    std::shared_lock<std::shared_mutex> lock(lifecycle_mutex);
    return open && config.save_external_artifacts;
}

} // namespace xemu::shader_browser
