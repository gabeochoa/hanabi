#pragma once

// Running `meta` for the Knots filer (knots_reporter.h), and the filing flow:
// create, and on a membership refusal join the namespace once and retry.
//
// THE SEAM. `runner_override()` replaces the process entirely. The mock
// backend, the scripted-UI harness and the unit tests install one; an E2E
// build with no override REFUSES to spawn (a suite that ran the real CLI
// would file real issues under a real person's name -- the reference lost 13
// knots to its board that way in under an hour). The real run is argv-only
// (posix_spawn, never a shell), bounded (30 s, then SIGTERM, then SIGKILL),
// and finds `meta` by absolute path because an app launched from the Finder
// has no login PATH.

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "client.h"
#include "knots_reporter.h"

extern char** environ;

namespace api::knots {

struct RunResult {
    bool spawned = false;
    int exitCode = -1;
    bool timedOut = false;
    std::string out;
    std::string err;
    std::string spawnError;  // why nothing ran
};

using RunFn = std::function<RunResult(const std::vector<std::string>& args)>;

inline RunFn& runner_override() {
    static RunFn fn;
    return fn;
}

inline constexpr int kTimeoutSecs = 30;

inline std::string meta_path() {
    for (const char* p : {"/opt/facebook/bin/meta", "/usr/local/bin/meta"})
        if (::access(p, X_OK) == 0) return p;
    return {};
}

inline RunResult run_real(const std::vector<std::string>& args) {
    RunResult r;
#ifdef AFTER_HOURS_ENABLE_E2E_TESTING
    (void)args;
    r.spawnError = "a test build never runs meta";
    return r;
#else
    const std::string tool = meta_path();
    if (tool.empty()) {
        r.spawnError = "meta is not installed (looked in /opt/facebook/bin and /usr/local/bin)";
        return r;
    }
    int outp[2], errp[2];
    if (::pipe(outp) != 0 || ::pipe(errp) != 0) {
        r.spawnError = "could not open pipes";
        return r;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, outp[1], 1);
    posix_spawn_file_actions_adddup2(&fa, errp[1], 2);
    posix_spawn_file_actions_addclose(&fa, outp[0]);
    posix_spawn_file_actions_addclose(&fa, errp[0]);
    std::vector<std::string> argvS{tool};
    argvS.insert(argvS.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : argvS) argv.push_back(s.data());
    argv.push_back(nullptr);
    // USER set explicitly: without it meta falls back to the certificate
    // identity with a warning.
    std::vector<std::string> envS;
    bool hasUser = false;
    for (char** e = environ; e && *e; ++e) {
        envS.emplace_back(*e);
        hasUser = hasUser || envS.back().rfind("USER=", 0) == 0;
    }
    if (!hasUser)
        if (const char* login = ::getlogin()) envS.push_back(std::string("USER=") + login);
    std::vector<char*> envp;
    for (auto& s : envS) envp.push_back(s.data());
    envp.push_back(nullptr);
    pid_t pid = 0;
    const int rc = ::posix_spawn(&pid, tool.c_str(), &fa, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    ::close(outp[1]);
    ::close(errp[1]);
    if (rc != 0) {
        ::close(outp[0]);
        ::close(errp[0]);
        r.spawnError = "could not start meta";
        return r;
    }
    r.spawned = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTimeoutSecs);
    bool outOpen = true, errOpen = true, termSent = false;
    auto killAt = deadline;
    char buf[4096];
    while (outOpen || errOpen) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline && !termSent) {
            ::kill(pid, SIGTERM);
            termSent = true;
            r.timedOut = true;
            killAt = now + std::chrono::seconds(2);
        }
        if (termSent && now >= killAt) {
            ::kill(pid, SIGKILL);
            break;
        }
        pollfd fds[2];
        int n = 0;
        if (outOpen) fds[n++] = {outp[0], POLLIN, 0};
        if (errOpen) fds[n++] = {errp[0], POLLIN, 0};
        if (::poll(fds, static_cast<nfds_t>(n), 200) <= 0) continue;
        for (int i = 0; i < n; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP))) continue;
            const ssize_t got = ::read(fds[i].fd, buf, sizeof buf);
            const bool isOut = fds[i].fd == outp[0];
            if (got <= 0) {
                (isOut ? outOpen : errOpen) = false;
            } else {
                (isOut ? r.out : r.err).append(buf, static_cast<std::size_t>(got));
            }
        }
    }
    ::close(outp[0]);
    ::close(errp[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
#endif
}

inline RunResult run(const std::vector<std::string>& args) {
    if (runner_override()) return runner_override()(args);
    return run_real(args);
}

inline std::string namespace_for(Board b) {
    const char* env = std::getenv(b == Board::App ? "HANABI_KNOTS_NAMESPACE"
                                                  : "HANABI_KNOTS_BACKEND_NAMESPACE");
    if (env != nullptr && *env != '\0') return env;
    return b == Board::App ? kDefaultAppNamespace : kDefaultBackendNamespace;
}

// One report, filed: create; on a membership refusal join once and retry.
// The failure text is the reader's sentence (the CLI's own words follow it,
// since nothing else will ever have them).
// The window capture, uploaded as the reporter; the line the description
// carries either way.
inline std::string upload_capture(const std::string& path) {
    if (path.empty()) return {};
    const std::string name = "hanabi-window.png";
    const RunResult r = run(upload_args(path, name));
    if (!r.spawned) return attachment_line(name, "", r.spawnError);
    if (r.timedOut) return attachment_line(name, "", "the upload did not finish in time");
    if (r.exitCode != 0) return attachment_line(name, "", "the upload was refused");
    const auto url = parse_upload_url(r.out);
    if (!url) return attachment_line(name, "", "the upload answered without a file");
    return attachment_line(name, *url, "");
}

inline Result<Filed> file(const std::string& raw, const Context& ctx, Board board,
                          const std::string& capturePath = {}) {
    const auto cap = capture(raw);
    if (!cap) return Result<Filed>::failure("Type what needs doing first.");
    const std::string ns = namespace_for(board);
    const std::string attachment = upload_capture(capturePath);
    const auto args = create_args(ns, cap->title, description(cap->detail, ctx, attachment),
                                  metadata(ctx, board), labels(ctx));
    const auto words = [](const RunResult& r) {
        std::string t = trim(r.err);
        const std::string o = trim(r.out);
        if (!o.empty()) t += (t.empty() ? "" : "\n") + o;
        return t.empty() ? "meta exited with status " + std::to_string(r.exitCode) : t;
    };
    RunResult r = run(args);
    if (!r.spawned) return Result<Filed>::failure("Nothing was filed: " + r.spawnError + ".");
    if (r.timedOut)
        return Result<Filed>::failure(
            "meta did not answer within 30 seconds, so nothing was filed. Your text is still here.");
    if (r.exitCode != 0 && is_permission_refusal(words(r))) {
        const RunResult j = run(join_args(ns));
        if (!j.spawned || j.exitCode != 0)
            return Result<Filed>::failure(not_a_member(ns) + ", and joining it was refused.");
        r = run(args);
        if (!r.spawned || r.timedOut || r.exitCode != 0)
            return Result<Filed>::failure(not_a_member(ns) + " -- joined, and the retry was refused.");
    }
    if (r.exitCode != 0) return Result<Filed>::failure("Knots refused it: " + words(r));
    auto filed = parse_created(r.out, ns);
    if (!filed)
        return Result<Filed>::failure(
            "meta answered without an issue id; it may have filed. Check the board before retrying.");
    return Result<Filed>::success(std::move(*filed));
}

// The fake the mock backend and the harness install: "kt-mock<n>" on the
// board asked, or a refusal shaped by HANABI_MOCK_KNOTS=member|fail. Records
// what it was asked, so a test can read the argv back.
struct MockRunner {
    static std::vector<std::vector<std::string>>& calls() {
        static std::vector<std::vector<std::string>> c;
        return c;
    }
    static RunResult run(const std::vector<std::string>& args) {
        calls().push_back(args);
        RunResult r;
        r.spawned = true;
        const char* mode = std::getenv("HANABI_MOCK_KNOTS");
        const std::string m = mode ? mode : "";
        if (args.size() >= 2 && args[0] == "phabricator.file" && args[1] == "upload") {
            if (m == "upload-fail") {
                r.exitCode = 1;
                r.err = "upload refused";
                return r;
            }
            r.exitCode = 0;
            r.out = json{{"f_handle", "F1000001"}, {"url", "https://www.internalfb.com/F1000001"}}
                        .dump();
            return r;
        }
        if (args.size() >= 2 && args[0] == "knots.namespace" && args[1] == "join") {
            r.exitCode = m == "member-stuck" ? 1 : 0;
            return r;
        }
        std::size_t creates = 0;
        for (const auto& c : calls()) creates += (c.size() >= 2 && c[1] == "create");
        if (m == "fail") {
            r.exitCode = 1;
            r.err = "knots backend unavailable";
            return r;
        }
        if ((m == "member" && creates == 1) || m == "member-stuck") {
            r.exitCode = 1;
            r.err = "PERMISSION_DENIED: not a member";
            return r;
        }
        std::string ns = "?";
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "--namespace") ns = args[i + 1];
        const std::string id = "kt-mock" + std::to_string(creates);
        r.exitCode = 0;
        r.out = json{{"id", id}, {"status", "open"}, {"labels_failed", ""},
                     {"url", "https://knots.internalmeta.com/" + ns + "/issues/" + id}}
                    .dump();
        return r;
    }
};

}  // namespace api::knots
