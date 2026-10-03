#pragma once

// "Update ready: restart Hanabi to finish updating" (the reference's
// ManagedInstallUpdateWatcher, 0.8.9; puffin_gaps.md D44). The reference reads
// Managed Software Center's install report; Hanabi is not an MSC package, so
// its analogue is the honest one for an app built from git: the executable
// on disk is no longer the one running -- an install replaced the bundle, or
// a rebuild replaced the binary. Detected by the file's identity (inode,
// modification time, size) against what it was at launch: cheap enough to
// check every half minute, and it cannot be fooled by a clock.
//
// Later dismisses THAT on-disk build only; a newer one shows again.

#include <cstdint>
#include <optional>
#include <string>

#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

extern char** environ;

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <climits>
#include <cstdlib>

namespace hanabi::update_ready {

struct FileId {
    std::uint64_t ino = 0;
    std::int64_t mtime_ns = 0;
    std::int64_t size = -1;
    [[nodiscard]] bool valid() const { return size >= 0; }
    bool operator==(const FileId&) const = default;
};

inline FileId identity_of(const std::string& path) {
    struct stat st {};
    if (path.empty() || ::stat(path.c_str(), &st) != 0) return {};
    FileId id;
    id.ino = static_cast<std::uint64_t>(st.st_ino);
#if defined(__APPLE__)
    id.mtime_ns = static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1000000000LL +
                  st.st_mtimespec.tv_nsec;
#else
    id.mtime_ns = static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
#endif
    id.size = static_cast<std::int64_t>(st.st_size);
    return id;
}

// This process's executable, resolved through symlinks ("" when unknown).
inline std::string running_executable() {
#if defined(__APPLE__)
    char buf[PATH_MAX];
    std::uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0) return {};
    char real[PATH_MAX];
    if (::realpath(buf, real) == nullptr) return buf;
    return real;
#else
    char real[PATH_MAX];
    if (::realpath("/proc/self/exe", real) == nullptr) return {};
    return real;
#endif
}

// The .app bundle an executable lives in ("" when it is a bare binary).
inline std::string bundle_of(const std::string& exe) {
    const std::string marker = ".app/Contents/MacOS/";
    const auto at = exe.rfind(marker);
    if (at == std::string::npos) return {};
    return exe.substr(0, at + 4);
}

// The decision, pure: what was on disk at launch, what is there now, and the
// build the reader said Later to.
struct Watch {
    std::string path;
    FileId at_launch;
    std::optional<FileId> dismissed;
    FileId pending_id;  // the on-disk build the strip is about

    [[nodiscard]] bool pending(const FileId& now) const {
        if (!at_launch.valid() || !now.valid()) return false;  // missing mid-install: say nothing
        if (now == at_launch) return false;
        return !(dismissed && *dismissed == now);
    }
};

// Start the on-disk build once this process has gone: a detached shell that
// waits a second (the quit path writes settings first) and then opens the
// bundle, or runs the bare binary. False when it could not be started.
inline bool relaunch_after_exit(const std::string& exe) {
    if (exe.empty()) return false;
    const std::string bundle = bundle_of(exe);
    std::string quoted = bundle.empty() ? exe : bundle;
    std::string q = "'";
    for (char c : quoted) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    q += "'";
    const std::string cmd = "sleep 1; " + (bundle.empty() ? q + " >/dev/null 2>&1 &" : "/usr/bin/open " + q);
    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
#if defined(POSIX_SPAWN_SETSID)
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
#endif
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, "/bin/sh", nullptr, &attr, const_cast<char* const*>(argv), environ);
    posix_spawnattr_destroy(&attr);
    return rc == 0;
}

}  // namespace hanabi::update_ready
