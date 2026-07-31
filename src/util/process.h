#pragma once

#include <functional>
#include <future>
#include <string>
#include <vector>

struct ProcessResult {
    std::string stdout_str;
    std::string stderr_str;
    int exit_code = -1;
    bool success() const { return exit_code == 0; }
};

// Synchronous -- for fast git operations (<100ms)
ProcessResult run_process(const std::string& working_dir,
                          const std::vector<std::string>& args);

// Asynchronous -- for slow git operations (push, pull, fetch)
std::future<ProcessResult> run_process_async(
    const std::string& working_dir, const std::vector<std::string>& args,
    std::function<void(const std::string&)> on_output = nullptr);
