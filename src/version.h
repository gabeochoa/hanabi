#pragma once

// Single source of truth for the app version string. Used by the `--version`
// CLI flag and shown in the Settings footer so the in-app "About" line and the
// CLI never drift. Bump here only.
namespace hanabi {
inline constexpr const char* kVersion = "0.1.0";
}  // namespace hanabi
