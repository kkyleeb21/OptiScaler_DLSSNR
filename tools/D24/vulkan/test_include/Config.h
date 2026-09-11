#pragma once
// Standalone tracking test: no application configuration or GPU runtime required.
struct Config
{
    struct Option { int value_or_default() const { return 1; } } DlssNrDiagnostics;
    struct DisabledOption { bool value_or_default() const { return false; } } FGVulkanExperimental;
    static Config* Instance() { static Config config; return &config; }
};
