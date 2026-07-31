#include "process.h"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstring>

extern char** environ;

namespace {

std::string read_fd(int fd) {
    std::string result;
    std::array<char, 4096> buf;
    ssize_t n;
    while ((n = read(fd, buf.data(), buf.size())) > 0) {
        result.append(buf.data(), static_cast<size_t>(n));
    }
    return result;
}

}  // namespace

ProcessResult run_process(const std::string& working_dir,
                          const std::vector<std::string>& args) {
    ProcessResult result;

    if (args.empty()) {
        result.stderr_str = "No command specified";
        return result;
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.stderr_str = "Failed to create pipes";
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    if (!working_dir.empty()) {
        posix_spawn_file_actions_addchdir(&actions, working_dir.c_str());
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid;
    int spawn_err =
        posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (spawn_err != 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        posix_spawn_file_actions_destroy(&actions);
        result.stderr_str =
            std::string("posix_spawnp failed: ") + strerror(spawn_err);
        return result;
    }

    result.stdout_str = read_fd(stdout_pipe[0]);
    result.stderr_str = read_fd(stderr_pipe[0]);

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    posix_spawn_file_actions_destroy(&actions);
    return result;
}

std::future<ProcessResult> run_process_async(
    const std::string& working_dir, const std::vector<std::string>& args,
    std::function<void(const std::string&)> on_output) {
    return std::async(std::launch::async, [working_dir, args, on_output]() {
        auto result = run_process(working_dir, args);
        if (on_output && !result.stdout_str.empty()) {
            on_output(result.stdout_str);
        }
        return result;
    });
}
