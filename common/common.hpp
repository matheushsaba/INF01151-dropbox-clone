#pragma once
#include <string>
#include <filesystem>

namespace common {

/// Ensures ``<base>/<sync_dir_<user>>`` exists and returns an **absolute** path.
/// The call is *idempotent* – it never fails if the directory already exists.
std::string ensure_sync_dir(const std::filesystem::path& base,
                            const std::string& username);

/// Returns a multi-line string with one entry per regular file inside *dir*,
/// including atime/mtime/ctime formatted like the assignment requires.
std::string list_files_with_mac(const std::filesystem::path& dir);
}  // namespace common
